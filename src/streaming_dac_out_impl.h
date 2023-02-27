#include <array>
#include <hardware/dma.h>
#include <hardware/clocks.h>
#include "support.h"
#include "audio_i2s_32_out.pio.h"
#include "streaming_internal.h"


#define DAC_OUT_LOG(...)   TU_LOG1("[DAC_OUT] " __VA_ARGS__);

namespace streaming
{
    using namespace support;

    constexpr uint32_t i2s_output_cycles_per_bit = 2;

    
    void dac_out::init(const init_config& config)
    {
        m_config = config;
        
        init_out_pin(m_config.dac_mute_pin, true);
        
        audio_i2s_32_out_program_init(get_pio(m_config.i2s_out_pio), m_config.i2s_out_sm, m_config.i2s_out_pio_program_offset, m_config.i2s_out_data_pin, m_config.i2s_out_bck_lrck_pin);

        m_dma_ch = dma_claim_unused_channel(true);
        m_dma_ctrl_ch = dma_claim_unused_channel(true);
        setup_dma_circular_read_config(
            m_dma_ch, m_dma_ctrl_ch, 
            pio_get_dreq(get_pio(m_config.i2s_out_pio), m_config.i2s_out_sm, true), 
            &get_pio(m_config.i2s_out_pio)->txf[m_config.i2s_out_sm], 
            0,
            false);
        dma_irqn_set_channel_enabled(m_config.dma_irq_n, m_dma_ch, true);

        m_dma_control_blocks[0] = config.buffer_begin;
        m_stream_buffer = { m_config.buffer_begin, m_config.buffer_end };
        m_stream_buffer_write_addr = m_stream_buffer.begin();

        DAC_OUT_LOG("initialized buffer size=%u\n", config.buffer_end - config.buffer_begin);
    }

    
    void dac_out::start()
    {
        if(m_running) return;

        DAC_OUT_LOG("start transfarring\n");

        pio_sm_set_enabled(get_pio(m_config.i2s_out_pio), m_config.i2s_out_sm, true);
        dma_channel_set_trans_count(m_dma_ch, m_stream_buffer.size(), false);
        dma_channel_transfer_from_buffer_now(m_dma_ctrl_ch, m_dma_control_blocks, 1);

        m_stream_buffer_dma_read_samples = 0;
        m_running = true;
    }

    void dac_out::stop()
    {
        if(!m_running) return;

        DAC_OUT_LOG("stop transfarring\n");

        m_running = false;

        pio_sm_set_enabled(get_pio(m_config.i2s_out_pio), m_config.i2s_out_sm, false);
        abort_dma_transfar();

        std::fill(m_config.buffer_begin, m_config.buffer_end, 0);
    }
    
    void dac_out::abort_dma_transfar()
    {
        DAC_OUT_LOG("stop dma\n");
        abort_dma_chainning_channles(m_dma_ch, m_dma_ctrl_ch);
    }

    
    void dac_out::set_format(uint32_t sampling_frequency, uint32_t bits)
    {
        DAC_OUT_LOG("set format freq=%d, bits=%d\n", sampling_frequency, bits);

        m_resolution_bits = bits;

        const uint32_t dac_output_frequency = sampling_frequency * device_output_channels * 32 * i2s_output_cycles_per_bit;
        pio_sm_set_clkdiv(get_pio(m_config.i2s_out_pio), m_config.i2s_out_sm, (float)(clock_get_hz(clk_sys) / (double)dac_output_frequency));

        const uint16_t duration = (m_config.buffer_end - m_config.buffer_begin)/max_output_samples_1ms;
        m_stream_buffer.resize(get_samples_duration_ms(duration, sampling_frequency, device_output_channels));
        m_stream_buffer_write_addr = m_config.buffer_begin;
    }

    static OutputWriteResult write_dac_data(uint32_t *dst_begin, uint32_t *dst_end, const uint8_t *data_begin, const uint8_t *data_end, uint8_t data_resolution_bits)
    {
        auto dst32_begin = dst_begin;
        auto dst32 = dst32_begin;

        auto p = data_begin;
        if (data_resolution_bits == 16)
        {
            for (; p < data_end && dst32 < dst_end; p += 2)
            {
                *dst32++ = bytes_to_dword<16, false>(p) << 16;
            }
        }
        else if (data_resolution_bits == 24)
        {
            for (; p < data_end && dst32 < dst_end; p += 3)
            {
                *dst32++ = bytes_to_dword<24, false>(p) << 8;
            }
        }

        return {(size_t)(dst32 - dst32_begin), (size_t)(p - data_begin)};
    }

    size_t dac_out::write(const uint8_t* begin, const uint8_t* end)
    {
        auto p = begin;
        while (p < end)
        {
            auto result = write_dac_data(m_stream_buffer_write_addr, m_stream_buffer.end(), p, end, m_resolution_bits);
            m_stream_buffer_write_addr = 
                m_stream_buffer.advance(m_stream_buffer_write_addr, result.wrote_samples);
            p += result.consumed_data_bytes;
        }

#if PRINT_STATS
        m_debug_available_samples = get_buffer_available_samples();
#endif

        return p - begin;
    }

    void dac_out::on_dma_isr()
    {
        if(dma_irqn_get_channel_status(m_config.dma_irq_n, m_dma_ch))
        {
            m_stream_buffer_dma_read_samples += m_stream_buffer.size();
            dma_irqn_acknowledge_channel(m_config.dma_irq_n, m_dma_ch);
        }
    }

    uint32_t dac_out::get_consumed_samples() const
    {
        auto read_addr = m_running ? (uint32_t*)dma_channel_hw_addr(m_dma_ch)->al1_read_addr : m_config.buffer_begin;
        return m_stream_buffer_dma_read_samples + (read_addr - m_config.buffer_begin);
    }

    uint32_t dac_out::get_buffer_available_samples() const
    {
        auto dma = dma_channel_hw_addr(m_dma_ch);
        auto read_addr = m_running ? (uint32_t*)dma->al1_read_addr : m_config.buffer_begin;
        return  m_stream_buffer.distance(m_stream_buffer_write_addr, read_addr);
    }

    uint32_t dac_out::get_buffer_left_count() const
    {
        return m_stream_buffer.size() - get_buffer_available_samples();
    }

#if PRINT_STATS
    void dac_out::print_stats()
    {
        dbg_printf(
            "  dac out:\n"
            "    left: %u/%u\n"
            "    consumed: %u\n",
            m_debug_available_samples, m_stream_buffer.size(),
            get_consumed_samples());
    }
#endif

}
