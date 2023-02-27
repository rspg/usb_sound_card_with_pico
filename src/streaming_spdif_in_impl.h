#include <array>
#include <algorithm>
#include <hardware/dma.h>
#include <hardware/clocks.h>
#include <hardware/structs/systick.h>
#include <hardware/structs/bus_ctrl.h>
#include "support.h"
#include "profiler.h"
#include "spdif_in.pio.h"


#define SPDIF_IN_LOG(...)   TU_LOG1("[SPDIF_IN] " __VA_ARGS__)

namespace streaming
{

    using namespace support;

    namespace sync
    {
        struct work_data
        {
            PIO         pio;
            uint8_t     pio_program_offset;
            uint8_t     data_sm;
            uint8_t     raw_sm;
            int         dma_ctrl_ch;
            uint32_t**  dma_ctrl_block;
        };

        template<uint8_t Patttern> bool detect_first_preamble(const work_data& workdata, uint32_t maxtrycount);
    }

    constexpr uint32_t spdif_input_cycles_per_bit = 8;

    enum task_process_spdif_input_notify
    {
        NOTIFY_DMA_RAW_DATA_TRANSFAR_COMPLETED,
        NOTIFY_SAMPLE_ERROR
    };

    
    void spdif_in::init(const init_config &config)
    {
        m_config = config;

        // SPDIF data input
        {
            auto sm = m_config.spdif_in_sm;
            auto pio = get_pio(m_config.spdif_in_pio);

            spdif_in_program_init(pio, sm, m_config.spdif_in_pio_program_offset, m_config.spdif_in_data_pin);

            m_dma_ch = dma_claim_unused_channel(true);
            m_dma_ctrl_ch = dma_claim_unused_channel(true);
            setup_dma_circular_write_config(
                m_dma_ch, m_dma_ctrl_ch,
                pio_get_dreq(pio, sm, false),
                &pio->rxf[sm],
                0,
                true);
        }

        // SPDIF raw bit read
        {
            auto sm = m_config.spdif_in_raw_sm;
            auto pio = get_pio(m_config.spdif_in_pio);

            spdif_in_raw_program_init(pio, sm, m_config.spdif_in_raw_pio_program_offset, m_config.spdif_in_data_pin);

            m_raw_dma_ch = dma_claim_unused_channel(true);
            {
                auto c = dma_channel_get_default_config(m_raw_dma_ch);
                channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
                channel_config_set_read_increment(&c, false);
                channel_config_set_write_increment(&c, true);
                channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));
                dma_channel_configure(m_raw_dma_ch, &c, nullptr, &pio->rxf[sm], 0, false);
            }

            dma_irqn_set_channel_enabled(m_config.dma_irq_n, m_raw_dma_ch, true);
        }

        m_stream_buffer = { config.buffer_begin, config.buffer_end };
        m_stream_buffer_read_addr = m_stream_buffer.begin();
        m_dma_control_blocks[0] = config.buffer_begin;

        SPDIF_IN_LOG("initialized. buffer size=%u\n", config.buffer_end - config.buffer_begin);
    }

    
    void spdif_in::set_output_format(uint32_t freq, uint8_t bits)
    {
        SPDIF_IN_LOG("set output format freq=%d, bits=%d\n", freq, bits);
        m_output_frequency = freq;
        m_output_resolution_bits = bits;

        if(is_signal_active())
            update_convert_context();
    }

    void spdif_in::set_sampling_frequency(uint32_t freq)
    {
        m_sampling_frequency = freq;

        const uint16_t blocks = (m_config.buffer_end - m_config.buffer_begin)*freq/(spdif::block_samples*max_sampling_frequency);
        m_stream_buffer.resize(blocks*spdif::block_samples);

        auto pio = get_pio(m_config.spdif_in_pio);

        // configure spdif_in
        const float clkdiv = (float)(clock_get_hz(clk_sys) / (double)(freq * device_input_channels * 32));
        pio_sm_set_clkdiv(pio, m_config.spdif_in_sm, (float)clkdiv / spdif_input_cycles_per_bit);

        // configure spdif_bit_in
        pio->sm[m_config.spdif_in_raw_sm].shiftctrl =
            (pio->sm[m_config.spdif_in_raw_sm].shiftctrl & ~PIO_SM0_SHIFTCTRL_PUSH_THRESH_BITS) | (1 << PIO_SM0_SHIFTCTRL_PUSH_THRESH_LSB);
        pio_sm_set_clkdiv(pio, m_config.spdif_in_raw_sm, (float)clkdiv);

        dma_channel_set_trans_count(m_dma_ch, m_stream_buffer.size(), false);
    }
    
    uint32_t spdif_in::get_sampling_frequency(bool indicated_by_status)
    {
        if(indicated_by_status)
        {
            if (is_signal_active())
            {
                return spdif::get_status_frequency(m_status_bits);
            }
            return 0;
        }
        else
        {
            return m_sampling_frequency;
        }
    }

    
    uint8_t spdif_in::get_resolution_bits()
    {
        return m_status_bits[32] ? 24 : 20;
    }

    size_t spdif_in::get_available_samples() const
    {
        if(!is_signal_active()) 
            return 0;
        return get_available_samples_internal();
    }

    size_t spdif_in::get_available_samples_internal() const
    {
        auto spdif_dma = dma_channel_hw_addr(m_dma_ch);
        return m_stream_buffer.distance((uint32_t*)spdif_dma->write_addr, m_stream_buffer_read_addr);
    }

    bool spdif_in::is_enough_available_samples(size_t fetch_require_samples) const
    {
        return get_available_samples() > m_converter.get_requirement_src_samples(fetch_require_samples);
    }

    size_t spdif_in::fetch_stream_data(uint8_t *buffer_begin, uint8_t *buffer_end)
    {
        if (!m_running || !is_signal_active())
            return 0;

        const auto preamble_w = spdif::preamble_w ^ (m_signal_inverted ? spdif::preamble_mask : 0);
        auto check_preamble = [&](uint32_t value) {
            return !m_preamble_phase_w || (((value >> spdif::preamble_shift_lsb) & spdif::preamble_mask) == preamble_w);
        };

        auto spdif_dma = dma_channel_hw_addr(m_dma_ch);
        auto dst = buffer_begin;
        auto dst_end = buffer_begin + round_frame_bytes(buffer_end - buffer_begin, m_output_resolution_bits, device_input_channels);

        auto write_addr = (uint32_t*)spdif_dma->write_addr;
        while(m_stream_buffer_read_addr != write_addr && dst < dst_end && is_signal_active())
        {
            std::move(m_config.temp_begin + m_fetch_temp_top, m_config.temp_begin + m_fetch_temp_tail, m_config.temp_begin);
            m_fetch_temp_tail -= m_fetch_temp_top;
            m_fetch_temp_top = 0;

            size_t copy_size = (m_config.temp_end - m_config.temp_begin) - m_fetch_temp_tail;
            m_stream_buffer_read_addr = m_stream_buffer.copy_to(write_addr, m_stream_buffer_read_addr, m_config.temp_begin + m_fetch_temp_tail, copy_size);

            for(int i = 0; i < copy_size; ++i)
            {
                auto& value = m_config.temp_begin[m_fetch_temp_tail++];
                if(!check_preamble(value))
                {
                    raise_error();
                    break;
                }
                m_preamble_phase_w = !m_preamble_phase_w;
                value >>= spdif::data_shift_lsb;
            }

            auto result = m_converter.apply((uint8_t*)(m_config.temp_begin + m_fetch_temp_top), (uint8_t*)(m_config.temp_begin + m_fetch_temp_tail), dst, dst_end);
            dst += result.dst_advanced_bytes;
            m_fetch_temp_top += result.src_advanced_bytes/4;
        }

        dbg_assert((dst - buffer_begin)%(m_converter.get_config().dst_stride*device_input_channels) == 0);

        return dst - buffer_begin;
    }

    void spdif_in::job_init()
    {
        if(m_job_work.first_call)
        {
            SPDIF_IN_LOG("init job\n");
        }

        m_signal_inverted = !m_signal_inverted;
        m_job_work.interval_sample_num = 0;

        if(m_running)
            change_job_method(&spdif_in::job_detect_kick_dma_raw_data, true);
        else
            set_pending_job(100*1000);
    }

    void spdif_in::job_detect_kick_dma_raw_data()
    {
        if(m_job_work.first_call)
        {
            SPDIF_IN_LOG("kick DMA to get raw data\n");
        }

        auto sm = m_config.spdif_in_raw_sm;
        auto pio = get_pio(m_config.spdif_in_pio);

        pio_sm_restart(pio, sm);
        pio_sm_set_clkdiv(pio, sm, 1.f);
        pio->sm[m_config.spdif_in_raw_sm].shiftctrl =
            (pio->sm[m_config.spdif_in_raw_sm].shiftctrl & ~PIO_SM0_SHIFTCTRL_PUSH_THRESH_BITS);

        bool timeouted = false;
        auto wait_sm_exec = [&]()
        {
            uint32_t time = time_us_32() + 200;
            while(pio_sm_is_exec_stalled(pio, sm) && time_us_32() < time) ;
            timeouted |= (time_us_32() >= time);
        };

        pio_sm_clear_fifos(pio, sm);
        for(int i = 0; i < 3; ++i)
        {
            pio_sm_exec(pio, sm, pio_encode_wait_pin(1, 0));
            wait_sm_exec();
            pio_sm_exec(pio, sm, pio_encode_wait_pin(0, 0));
            wait_sm_exec();
        }

        if(timeouted)
        {
            m_job_work.set_pending_delay_us(100*1000);
        }
        else
        {
            change_job_method(&spdif_in::job_detect_finish_dma_raw_data, false);
            pio_sm_set_enabled(pio, sm, true);
            dma_channel_transfer_to_buffer_now(m_raw_dma_ch, m_config.buffer_begin, spdif::block_samples);
        }
    }

    void spdif_in::job_detect_finish_dma_raw_data()
    {
        if(m_job_work.first_call)
        {
            SPDIF_IN_LOG("wait DMA completion\n");
        }

        auto sm = m_config.spdif_in_raw_sm;
        auto pio = get_pio(m_config.spdif_in_pio);
        pio_sm_set_enabled(pio, sm, false);

        change_job_method(&spdif_in::job_detect_signal_clock_interval, true);
    }
    
    void spdif_in::job_detect_signal_clock_interval()
    {
        if(m_job_work.first_call)
        {
            SPDIF_IN_LOG("detect clock interval\n");
        }

        uint32_t minval = (uint32_t)-1;
        uint32_t maxval = 0;

        bool detection = false;
        uint32_t ticks = 0;
        for (auto w = m_config.buffer_begin; w != m_config.buffer_begin + spdif::block_samples; ++w)
        {
            for (auto i = 0; i < 32; ++i)
            {
                auto bit = (*w >> (31 - i)) & 1;

                if (!detection && bit)
                    continue;
                detection = true;

                if (bit)
                    ++ticks;
                else if (ticks)
                {
                    if (minval > ticks) minval = ticks;
                    if (maxval < ticks) maxval = ticks;
                    ticks = 0;
                }
            }
        }

        const auto assum = minval * 3;
        if (std::abs((int)(maxval - assum)) <= assum / 10)
        {
            auto& interval_min_samples = m_job_work.interval_min_samples;
            auto& interval_max_samples = m_job_work.interval_max_samples;
            auto& interval_sample_num = m_job_work.interval_sample_num;

            interval_min_samples[interval_sample_num] = minval;
            interval_max_samples[interval_sample_num] = maxval;
            interval_sample_num++;

            if (interval_sample_num >= std::size(interval_min_samples))
            {
                std::sort(std::begin(interval_min_samples), std::end(interval_min_samples), std::less<uint32_t>());
                m_job_work.interval_min = interval_min_samples[std::size(interval_min_samples) / 2];

                std::sort(std::begin(interval_max_samples), std::end(interval_max_samples), std::less<uint32_t>());
                m_job_work.interval_max = interval_max_samples[std::size(interval_max_samples) / 2];

                std::fill(m_config.buffer_begin, m_config.buffer_end, 0);

                SPDIF_IN_LOG("clock min=%d, max=%d\n", m_job_work.interval_min, m_job_work.interval_max);

                change_job_method(&spdif_in::job_sync_spdif_frame, true);
                return;
            }
        }

        change_job_method(&spdif_in::job_detect_kick_dma_raw_data, true);
    }

    
    void spdif_in::job_sync_spdif_frame()
    {
        if(m_job_work.first_call)
        {
            SPDIF_IN_LOG("sync frame\n");
        }

        const auto interval_min = m_job_work.interval_min;
        const auto interval_max = m_job_work.interval_max;

        auto clock_differential = [&](uint32_t freq) { return std::abs(interval_min - ((float)(clock_get_hz(clk_sys) / (double)(freq * device_input_channels * 32 * 2)))); };

        const float tolerance = 2;
        if (clock_differential(48000) <= tolerance)
            set_sampling_frequency(48000);
        else if (clock_differential(96000) <= tolerance)
            set_sampling_frequency(96000);
        else
        {
            return;
        }

        auto pio = get_pio(m_config.spdif_in_pio);

        bool detected = false;
        {
            constexpr uint8_t pattern_bits = 0b0001;

            sync::work_data workdata = {
                .pio = pio,
                .pio_program_offset = m_config.spdif_in_pio_program_offset,
                .data_sm = m_config.spdif_in_sm,
                .raw_sm = m_config.spdif_in_raw_sm,
                .dma_ctrl_ch = m_dma_ctrl_ch,
                .dma_ctrl_block = m_dma_control_blocks
            };

            uint32_t maxtrycount = 256;
            if (m_signal_inverted)
            {
                pio->instr_mem[m_config.spdif_in_pio_program_offset + spdif_in_offset_sync_instr_point] = pio_encode_wait_pin(0, 0);
                detected = sync::detect_first_preamble<(uint8_t)~pattern_bits>(workdata, maxtrycount);
            }
            else
            {
                pio->instr_mem[m_config.spdif_in_pio_program_offset + spdif_in_offset_sync_instr_point] = pio_encode_wait_pin(1, 0);
                detected = sync::detect_first_preamble<pattern_bits>(workdata, maxtrycount);
            }
        }

        SPDIF_IN_LOG("sync result=%s, negative=%s\n", detected ? "true" : "false", m_signal_inverted ? "true" : "false");

        pio_sm_set_enabled(pio, m_config.spdif_in_raw_sm, false);

        m_stream_buffer_read_addr = m_config.buffer_begin;

        change_job_method(detected ? &spdif_in::job_scan_block_first
                                   : &spdif_in::job_init, true);
    }

    void spdif_in::job_scan_block_first()
    {
        if(m_job_work.first_call)
        {
            SPDIF_IN_LOG("scan first preamble in a block\n");
        }

        if(get_available_samples_internal() < 32)
        {
            m_job_work.set_pending_delay_us(100);
            return;
        }

        auto spdif_dma = dma_channel_hw_addr(m_dma_ch);
        const uint8_t preamble_b = spdif::preamble_b ^ (m_signal_inverted ? spdif::preamble_mask : 0);

        while(m_stream_buffer_read_addr != (uint32_t*)spdif_dma->write_addr)
        {
            if (check_sample_preamble(preamble_b))
            {
                change_job_method(&spdif_in::job_parse_control_block, true);
                return;
            }
        }

        if(++m_job_work.failure_count >= 4)
        {
            m_job_work.failure_count = 0;
            change_job_method(&spdif_in::job_init, true);
        }
        else
        {
            set_pending_job();
        }
    }
    
    void spdif_in::job_parse_control_block()
    {
        if(m_job_work.first_call)
        {
            SPDIF_IN_LOG("parse control bits of a block\n");
        }

        if(get_available_samples_internal() < spdif::block_samples)
        {
            m_job_work.set_pending_delay_us(100);
            return;
        }

        auto spdif_dma = dma_channel_hw_addr(m_dma_ch);

        const uint8_t preamble_b = spdif::preamble_b ^ (m_signal_inverted ? spdif::preamble_mask : 0);
        const uint8_t preamble_m = spdif::preamble_m ^ (m_signal_inverted ? spdif::preamble_mask : 0);
        const uint8_t preamble_w = spdif::preamble_w ^ (m_signal_inverted ? spdif::preamble_mask : 0);

        auto read_control_flags = [&]()
        {
            uint32_t value = *m_stream_buffer.end();
            
            if (!check_sample_preamble(preamble_w))
                return false;

            m_status_bits[0] = (value >> spdif::control_shift_lsb) & 1;

            for (size_t i = 1; i < spdif::block_frames; ++i)
            {
                if (!check_sample_preamble(preamble_m))
                    return false;
                if (!check_sample_preamble(preamble_w))
                    return false;

                m_status_bits[i] = (value >> spdif::control_shift_lsb) & 1;
            }

            if (!check_sample_preamble(preamble_b))
                return false;

            return true;
        };

        if(read_control_flags())
        {
            m_job_work.failure_count = 0;
            m_preamble_phase_w = false;
            m_signal_active = true;
            update_convert_context();

            SPDIF_IN_LOG("activated\n");
        }
        else
        {
            m_signal_active = false;

            if(++m_job_work.failure_count >= 4)
            {
                auto pio = get_pio(m_config.spdif_in_pio);
                pio_sm_set_enabled(pio, m_config.spdif_in_sm, false);
                abort_dma_chainning_channles(m_dma_ch, m_dma_ctrl_ch);
                SPDIF_IN_LOG("a block is invalid. stop transfarring.\n");

                m_job_work.failure_count = 0;
                m_stream_buffer_read_addr = (uint32_t*)spdif_dma->write_addr;
                change_job_method(&spdif_in::job_init, true); 
            }
            else
            {
                change_job_method(&spdif_in::job_scan_block_first, true); 
            }
        }
    }

    void spdif_in::raise_error()
    {
        SPDIF_IN_LOG("sample error deteted\n");

        m_signal_active = false;
        if(m_job_work.method == &spdif_in::job_parse_control_block)
            set_pending_job();
    }

    void spdif_in::change_job_method(void(spdif_in::*method)(), bool pending)
    {
        m_job_work.first_call = true;
        m_job_work.method = method;
        if(pending)
            set_pending_job();
    }

    void spdif_in::set_pending_job()
    {
        m_job_work.set_pending();
    }

    void spdif_in::set_pending_job(uint32_t delay_us)
    {
        m_job_work.set_pending_delay_us(delay_us);
    }

    void spdif_in::start()
    {
        if(m_running) return;

        SPDIF_IN_LOG("start control\n");

        m_job_work.method = &spdif_in::job_init;

        m_job_work.this_ptr = this;
        m_job_work.activate();
        m_job_work.set_pending();

        m_running = true;
    }

    void spdif_in::stop()
    {
        if(!m_running) return;
        
        SPDIF_IN_LOG("stop control\n");

        m_job_work.deactivate();
        m_job_work.wait_done();
        
        m_running = false;
        m_signal_active = false;

        std::fill(m_config.buffer_begin, m_config.buffer_end, 0);
    }

    void spdif_in::set_job_affinity_mask(uint8_t affinity)
    {
        m_job_work.set_affinity_mask(affinity);
    }
    
    void spdif_in::on_dma_isr()
    {
        if (dma_irqn_get_channel_status(m_config.dma_irq_n, m_raw_dma_ch))
        {
            if(m_job_work.method == &spdif_in::job_detect_finish_dma_raw_data)
                set_pending_job();
            dma_irqn_acknowledge_channel(m_config.dma_irq_n, m_raw_dma_ch);
        }
    }
    
    bool spdif_in::check_sample_preamble(uint8_t preamble)
    {
        auto spdif_dma = dma_channel_hw_addr(m_dma_ch);
        m_stream_buffer_read_addr = m_stream_buffer.advance((uint32_t*)spdif_dma->write_addr, m_stream_buffer_read_addr, 1);
        auto val = *m_stream_buffer_read_addr;
        return (((val >> spdif::preamble_shift_lsb) & spdif::preamble_mask) == preamble);
    }
    
    void spdif_in::update_convert_context()
    {
        bool need_update = 
            m_converter.get_config().src_freq != get_sampling_frequency(false)
            || m_converter.get_config().src_bits != get_resolution_bits()
            || m_converter.get_config().dst_freq != m_output_frequency
            || m_converter.get_config().dst_bits != m_output_resolution_bits;

        if(need_update)
        {
            processing::converter::config cfg = {
                .src_bits = get_resolution_bits(),
                .src_stride = 4,
                .src_freq = get_sampling_frequency(false),
                .dst_bits = m_output_resolution_bits,
                .dst_stride = bits_to_bytes(m_output_resolution_bits),
                .dst_freq = m_output_frequency,
                .channels = device_input_channels,
                .use_interp = true
            };
            m_converter.setup(cfg);

            SPDIF_IN_LOG("update conv\n");
        }
    }

    namespace sync
    {
        work_data   g_work_data;

        void reset_pio_sm()
        {
            g_work_data.pio->ctrl = (g_work_data.pio->ctrl & ~(1 << g_work_data.data_sm)) | (1 << (PIO_CTRL_SM_RESTART_LSB + g_work_data.data_sm)) | (1 << (PIO_CTRL_CLKDIV_RESTART_LSB + g_work_data.data_sm));
            pio_sm_clear_fifos(g_work_data.pio, g_work_data.data_sm);
            spdif_in_program_preexec(g_work_data.pio, g_work_data.data_sm, g_work_data.pio_program_offset);
        }

        void reset_raw_pio_sm()
        {
            g_work_data.pio->ctrl = (g_work_data.pio->ctrl & ~(1 << g_work_data.raw_sm)) | (1 << (PIO_CTRL_SM_RESTART_LSB + g_work_data.raw_sm)) | (1 << (PIO_CTRL_CLKDIV_RESTART_LSB + g_work_data.raw_sm));
            pio_sm_clear_fifos(g_work_data.pio, g_work_data.raw_sm);
        }

        void start_dma_transfar()
        {
            dma_channel_transfer_from_buffer_now(g_work_data.dma_ctrl_ch, g_work_data.dma_ctrl_block, 1);
        }

        
        template<uint8_t Patttern>  bool __no_inline_not_in_flash_func(detect_first_preamble)(const work_data& workdata, uint32_t maxtrycount) 
        {
            int loops = maxtrycount;

            {
                g_work_data = workdata;

                bus_ctrl_hw->priority |= BUSCTRL_BUS_PRIORITY_PROC1_BITS;

#define s_(L) #L
#define s(L) s_(L)
#define wait_rx_fifo_block(L)                           \
    ".Lloop%=" L ":\n"                                  \
    "   ldr r0, [%[pio], %[pio_fstat_ofs]]\n"           \
    "   tst r0, %[raw_rx_empty_bit]\n"                  \
    "   bne .Lloop%=" L "\n"
#define signal_pass_block(n)                            \
    wait_rx_fifo_block(s(__LINE__))                     \
    "   ldr r0, [%[pio], %[pio_raw_rxf_ofs]]\n"         \
    "   cmp r0, #((%c[patternbits]>>" s(n) ")&1)\n"     \
    "   bne .Lretry%=\n"
#define call_c_function(f)                              \
    "   push { r1, r2, r3 }\n"                          \
    "   bl %c[" s(f) "]\n"                              \
    "   pop { r1, r2, r3 }\n"

                __asm volatile(
                    ".Lstart%=:\n" 
                        call_c_function(fn_reset_data_sm) 
                    ".Lretry%=:\n" 
                        call_c_function(fn_reset_raw_sm) 
                    ".Ldetection%=:\n"
                    "   sub %[loops], #1\n"
                    "   beq .Lexit%=\n"
                    "   str %[enable_raw_sm], [%[pio], %[pio_ctrl_ofs]]\n" 
                        signal_pass_block(0)
                        signal_pass_block(1)
                        signal_pass_block(2)
                        signal_pass_block(3) 
                    "   str %[enable_data_sm], [%[pio], %[pio_ctrl_ofs]]\n"
                    "   ldr r0, [%[pio], %[pio_fstat_ofs]]\n"
                    "   tst r0, %[raw_rx_empty_bit]\n"
                    "   beq .Lstart%=\n"
                    "   movs %[loops], #0xff\n" 
                        call_c_function(fn_start_dma) 
                    ".Lexit%=:"
                    : [loops] "+r"(loops)
                    : [pio] "r"(workdata.pio),
                      [enable_raw_sm] "l"(workdata.pio->ctrl | (1 << workdata.raw_sm)),
                      [enable_data_sm] "l"((workdata.pio->ctrl & ~(1 << workdata.raw_sm)) | (1 << workdata.data_sm)),
                      [raw_rx_empty_bit] "l"(1 << (PIO_FSTAT_RXEMPTY_LSB + workdata.raw_sm)),
                      [pio_raw_rxf_ofs] "l"(PIO_RXF0_OFFSET + workdata.raw_sm * 4),
                      [pio_ctrl_ofs] "i"(0),
                      [pio_fstat_ofs] "i"(4),
                      [patternbits] "i"(Patttern),
                      [fn_reset_data_sm] "i"(reset_pio_sm),
                      [fn_reset_raw_sm] "i"(reset_raw_pio_sm),
                      [fn_start_dma] "i"(start_dma_transfar)
                    : "r0", "cc");

#undef s
#undef _s
#undef wait_rx_fifo_block
#undef signal_pass_block

                bus_ctrl_hw->priority &= ~BUSCTRL_BUS_PRIORITY_PROC1_BITS;
            }

            return loops > 0;
        }
    }

}