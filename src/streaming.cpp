
#include <array>
#include <memory>
#include <algorithm>
#include <hardware/timer.h>
#include "tusb.h"
#include "device_config.h"

#if USB_IF_AUDIO_ENABLE

#include "debug.h"
#include "profiler.h"
#include "profile_measurement_list.h"
#include "support.h"
#include "circular_buffer.h"
#include "converter.h"
#include "mixer.h"
#include "streaming.h"
#include "streaming_internal.h"
#include "streaming_adc_in.h"
#include "streaming_spdif_in.h"
#include "streaming_spdif_out.h"
#include "streaming_dac_out.h"

#define STREAM_LOG(...) TU_LOG1("[STREAM] " __VA_ARGS__)

namespace streaming
{
    using namespace data_structure;

    enum PIO0_SM_TYPE
    {
        PIO0_SM_SPDIF_OUT,
        PIO0_SM_DAC_OUT,
        PIO0_SM_ADC_CLK,
    };
    enum PIO1_SM_TYPE
    {
        PIO1_SM_ADC_IN,
        PIO1_SM_SPDIF_IN,
        PIO1_SM_SPDIF_IN_RAW,
    };

    struct job_mix_out_info : public job_queue::work_fn
    {
        uint8_t sample_bytes;
        size_t buffer_size;
    };
    struct job_mix_out_io_info : public job_queue::work_fn
    {
        uint8_t *data_begin;
        uint8_t *data_end;
        uint32_t require_samples;
        uint32_t result_size;
    };
    struct job_mix_in_info : public job_queue::work_fn
    {
        uint64_t timeout;
        uint32_t require_samples;
        uint32_t buffer_size;
    };

    template <typename T>
    PIO get_sm_pio(T);
    template <>
    inline PIO get_sm_pio(PIO0_SM_TYPE) { return pio0; }
    template <>
    inline PIO get_sm_pio(PIO1_SM_TYPE) { return pio1; }
    template <typename T>
    uint8_t get_sm_pio_index(T);
    template <>
    inline uint8_t get_sm_pio_index(PIO0_SM_TYPE) { return 0; }
    template <>
    inline uint8_t get_sm_pio_index(PIO1_SM_TYPE) { return 1; }

    static constexpr uint32_t task_priority_default = 16;

    static constexpr uint16_t device_buffer_duration = 8;
    static constexpr uint16_t input_mixing_buffer_duration = 16;
    static constexpr uint16_t input_mixing_processing_buffer_duration_per_cycle = input_mixing_buffer_duration / 4;
    static constexpr uint16_t output_mixing_processing_buffer_duration_per_cycle = device_buffer_duration / 4;

    static circular_buffer<container_array<uint8_t, max_output_samples_1ms * device_buffer_duration * sizeof(uint32_t)>> g_rx_stream_buffer;
    static uint8_t *g_rx_stream_buffer_write_addr;
    static const uint8_t *g_rx_stream_buffer_read_addr;

    static uint32_t g_output_sampling_frequency = 0;
    static uint8_t g_output_resolution_bits = 0;
    static uint32_t g_input_sampling_frequency = 0;
    static uint8_t g_input_resolution_bits = 0;
    static processing::mixer g_input_mixer;
    static uint8_t g_input_mixer_adc_volume = 0xff;
    static uint8_t g_input_mixer_spdif_volume = 0xff;
    static circular_buffer<container_array<uint8_t, max_input_samples_1ms * input_mixing_buffer_duration * sizeof(uint32_t)>> g_input_mixing_buffer;
    static uint8_t *g_input_mixing_buffer_write_addr;
    static const uint8_t *g_input_mixing_buffer_pop_tx_read_addr;
    static const uint8_t *g_input_mixing_buffer_output_read_addr;
    static bool g_input_mixing_task_active;
    static processing::mixer g_output_mixer;
    static processing::converter g_output_input_converter;
    static uint8_t g_output_mixer_rx_volume = 0xff;
    static uint8_t g_output_mixer_mixed_input_volume = 0xff;
    static bool g_output_process_task_active;
    static uint8_t g_output_device_charge_count = 0;

    static job_mix_out_info g_job_mix_out = {};
#if DAC_OUTPUT_ENABLE
    static job_mix_out_io_info g_job_mix_out_dac = {};
#endif
#if SPDIF_OUTPUT_ENABLE
    static job_mix_out_io_info g_job_mix_out_spdif = {};
#endif
    static job_mix_in_info g_job_mix_in = {};
#if ADC_INPUT_ENABLE
    static job_mix_out_io_info g_job_mix_in_adc = {};
#endif
#if SPDIF_INPUT_ENABLE
    static job_mix_out_io_info g_job_mix_in_spdif = {};
#endif

#if ADC_INPUT_ENABLE
    static adc_in g_adc_in;
    static adc_in::buffer<device_buffer_duration> g_adc_in_buffer;
    static std::array<uint8_t, max_input_samples_1ms * input_mixing_processing_buffer_duration_per_cycle * sizeof(uint32_t)> g_adc_in_fetch_buffer;
#endif
#if SPDIF_INPUT_ENABLE
    static spdif_in g_spdif_in;
    static spdif_in::buffer<device_buffer_duration> g_spdif_in_buffer;
    static spdif_in::buffer<1> g_spdif_in_temp_buffer;
    static std::array<uint8_t, max_input_samples_1ms * input_mixing_processing_buffer_duration_per_cycle * sizeof(uint32_t)> g_spdif_in_fetch_buffer;
#endif
#if DAC_OUTPUT_ENABLE
    static dac_out g_dac_out;
    static dac_out::buffer<device_buffer_duration> g_dac_out_buffer;
#endif
#if SPDIF_OUTPUT_ENABLE
    static spdif_out g_spdif_out;
    static spdif_out::buffer<device_buffer_duration> g_spdif_out_buffer;
#endif

#if USB_IF_CONTROL_ENABLE
    struct debug_stats
    {
        uint32_t received_bytes;
        uint32_t transfar_bytes;
        struct
        {
            uint32_t src_left;
            uint32_t input_left;
            uint32_t processed_bytes;
        } outmix;
        struct
        {
            uint32_t processed_bytes;
            uint32_t adc_in_samples;
            uint32_t spdif_in_samples;
        } inmix;
    };
    debug_stats g_debug_stats;
#endif

    static void update_input_mixer()
    {
        if (g_input_resolution_bits == 0)
        {
            return;
        }

        processing::mixer::config mixer_config = {
            .bits = g_input_resolution_bits,
            .stride = bits_to_bytes(g_input_resolution_bits),
            .channels = device_input_channels,
            .use_interp = true};
        g_input_mixer.setup(mixer_config);
    }

    void update_output_mixer()
    {
        if (g_output_resolution_bits == 0 || g_input_resolution_bits == 0)
        {
            return;
        }

        processing::converter::config conversion_config = {
            .src_bits = g_input_resolution_bits,
            .src_stride = bits_to_bytes(g_input_resolution_bits),
            .src_freq = g_input_sampling_frequency,
            .dst_bits = g_output_resolution_bits,
            .dst_stride = bits_to_bytes(g_output_resolution_bits),
            .dst_freq = g_output_sampling_frequency,
            .channels = device_output_channels,
            .use_interp = true};
        g_output_input_converter.setup(conversion_config);

        processing::mixer::config mixer_config = {
            .bits = g_output_resolution_bits,
            .stride = bits_to_bytes(g_output_resolution_bits),
            .channels = device_output_channels,
            .use_interp = true};
        g_output_mixer.setup(mixer_config);
    }



    static void start_output_process_job();
    static void stop_output_process_job();

    void set_rx_format(uint32_t sampling_frequency, uint32_t bits)
    {
        g_output_device_charge_count = (device_buffer_duration / output_mixing_processing_buffer_duration_per_cycle) >> 1;

        if (g_output_sampling_frequency == sampling_frequency && g_output_resolution_bits == bits)
        {
            return;
        }
        STREAM_LOG("set rx format %u %u\n", sampling_frequency, bits);

        stop_output_process_job();

#if SPDIF_OUTPUT_ENABLE
        g_spdif_out.stop();
#endif
#if DAC_OUTPUT_ENABLE
        g_dac_out.stop();
#endif

        g_output_sampling_frequency = sampling_frequency;
        g_output_resolution_bits = bits;

        g_rx_stream_buffer.resize(get_samples_duration_ms(device_buffer_duration, g_output_sampling_frequency, device_output_channels) * bits_to_bytes(g_output_resolution_bits));
        g_rx_stream_buffer_write_addr = g_rx_stream_buffer.begin();
        g_rx_stream_buffer_read_addr = g_rx_stream_buffer.begin();

#if SPDIF_OUTPUT_ENABLE
        g_spdif_out.set_format(sampling_frequency, bits);
#endif
#if DAC_OUTPUT_ENABLE
        g_dac_out.set_format(sampling_frequency, bits);
#endif
        update_output_mixer();

        start_output_process_job();
    }

    void close_rx()
    {
#if SPDIF_OUTPUT_ENABLE
        g_spdif_out.stop();
#endif
#if DAC_OUTPUT_ENABLE
        g_dac_out.stop();
#endif
    }

    void get_rx_buffer_size(uint32_t &left, uint32_t &max_size)
    {
        left = g_rx_stream_buffer.distance(g_rx_stream_buffer_write_addr, g_rx_stream_buffer_read_addr);
        max_size = g_rx_stream_buffer.size();
    }

    void push_rx_data(size_t (*fn)(uint8_t *, size_t), size_t data_size)
    {
        auto write_addr = g_rx_stream_buffer_write_addr;
        auto data_size_saved = data_size;

        while (data_size)
        {
            size_t sz = std::min(data_size, (size_t)(g_rx_stream_buffer.end() - g_rx_stream_buffer_write_addr));
            fn(g_rx_stream_buffer_write_addr, sz);

            data_size -= sz;
            g_rx_stream_buffer_write_addr += sz;
            if (g_rx_stream_buffer_write_addr >= g_rx_stream_buffer.end())
                g_rx_stream_buffer_write_addr = g_rx_stream_buffer.begin();
        }

#if USB_IF_CONTROL_ENABLE
        g_debug_stats.received_bytes += g_rx_stream_buffer.distance(g_rx_stream_buffer_write_addr, write_addr);
#endif

        g_job_mix_out.set_pending();
    }



    static void start_mix_input_job();
    static void stop_mix_input_job();

    void set_tx_format(uint32_t sampling_frequency, uint32_t bits)
    {
        if (g_input_sampling_frequency == sampling_frequency && g_input_resolution_bits == bits)
        {
            return;
        }
        STREAM_LOG("set tx format %u %u\n", sampling_frequency, bits);

        stop_mix_input_job();

#if ADC_INPUT_ENABLE
        g_adc_in.stop();
#endif
#if SPDIF_INPUT_ENABLE
        g_spdif_in.stop();
#endif

        g_input_sampling_frequency = sampling_frequency;
        g_input_resolution_bits = bits;

        g_input_mixing_buffer.resize(get_samples_duration_ms(input_mixing_buffer_duration, g_input_sampling_frequency, device_input_channels) * bits_to_bytes(g_input_resolution_bits));
        g_input_mixing_buffer_pop_tx_read_addr = g_input_mixing_buffer.begin();
        g_input_mixing_buffer_output_read_addr = g_input_mixing_buffer.begin();

        update_input_mixer();
        update_output_mixer();

#if ADC_INPUT_ENABLE
        g_adc_in.set_format(sampling_frequency, bits);
#endif
#if SPDIF_INPUT_ENABLE
        g_spdif_in.set_output_format(sampling_frequency, bits);
#endif

#if ADC_INPUT_ENABLE
        g_adc_in.start();
#endif
#if SPDIF_INPUT_ENABLE
        g_spdif_in.start();
#endif
        start_mix_input_job();
    }

    size_t pop_tx_data(size_t (*fn)(const uint8_t *, const uint8_t *))
    {
        static std::array<uint8_t, CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ> tmp_buf;

        const size_t epinPacketBytes = support::get_epin_packet_bytes(g_input_sampling_frequency, g_input_resolution_bits);

        auto buffer_write_addr = g_input_mixing_buffer_write_addr;
        const size_t available_size = g_input_mixing_buffer.distance(buffer_write_addr, g_input_mixing_buffer_pop_tx_read_addr);
        
        if (available_size > epinPacketBytes)
        {
            size_t copy_size = epinPacketBytes;
            g_input_mixing_buffer_pop_tx_read_addr = g_input_mixing_buffer.copy_to(buffer_write_addr, g_input_mixing_buffer_pop_tx_read_addr, tmp_buf.begin(), copy_size);

            dbg_assert(copy_size == epinPacketBytes);
        }
        else
        {
            STREAM_LOG("[pop_tx] stream exausted\n");
        }

#if USB_IF_CONTROL_ENABLE
        g_debug_stats.transfar_bytes += epinPacketBytes;
#endif

        return fn(tmp_buf.begin(), tmp_buf.begin() + epinPacketBytes);
    }

    void close_tx()
    {
    }

    void set_spdif_in_volume(uint8_t value)
    {
        g_input_mixer_spdif_volume = value;
    }

    void set_line_in_volume(uint8_t value)
    {
        g_input_mixer_adc_volume = value;
    }

    uint8_t get_spdif_in_volume()
    {
        return g_input_mixer_spdif_volume;
    }

    uint8_t get_line_in_volume()
    {
        return g_input_mixer_adc_volume;
    }


    static void job_mix_output_init(job_queue::work *);
    static void job_mix_output_process(job_queue::work *);
    static void job_mix_output_dac_write(job_queue::work *);
    static void job_mix_output_spdif_write(job_queue::work *);

    static void start_output_process_job()
    {
        STREAM_LOG("start output process job\n");

#if DAC_OUTPUT_ENABLE
        g_job_mix_out_dac.set_callback(job_mix_output_dac_write);
        g_job_mix_out_dac.activate();
#endif
#if SPDIF_OUTPUT_ENABLE
        g_job_mix_out_spdif.set_callback(job_mix_output_spdif_write);
        g_job_mix_out_spdif.activate();
#endif
        g_job_mix_out.set_callback(job_mix_output_init);
        g_job_mix_out.activate();
        g_job_mix_out.set_pending();
    }
    static void stop_output_process_job()
    {
        STREAM_LOG("stop output process job\n");

        g_job_mix_out.deactivate();
        g_job_mix_out.wait_done();
#if DAC_OUTPUT_ENABLE
        g_job_mix_out_dac.deactivate();
        g_job_mix_out_dac.wait_done();
#endif
#if SPDIF_OUTPUT_ENABLE
        g_job_mix_out_spdif.deactivate();
        g_job_mix_out_spdif.wait_done();
#endif
    }

    static void job_mix_output_init(job_queue::work *)
    {
        JOB_TRACE_LOG("job_mix_output_init\n");

        g_job_mix_out.sample_bytes = bits_to_bytes(g_output_resolution_bits);
        g_job_mix_out.buffer_size = get_samples_duration_ms(output_mixing_processing_buffer_duration_per_cycle, g_output_sampling_frequency, device_output_channels) * g_job_mix_out.sample_bytes;
        g_job_mix_out.set_callback(job_mix_output_process);
        g_job_mix_out.set_pending();
    }

    static void job_mix_output_process(job_queue::work *)
    {
        JOB_TRACE_LOG("job_mix_output_process\n");

        static std::array<uint8_t, max_output_samples_1ms * output_mixing_processing_buffer_duration_per_cycle * sizeof(uint32_t)> data_tmp_buf;
        static std::array<uint8_t, max_output_samples_1ms * output_mixing_processing_buffer_duration_per_cycle * sizeof(uint32_t)> mix_tmp_buf;

        const uint8_t output_sample_bytes = g_job_mix_out.sample_bytes;
        const size_t buffer_size = g_job_mix_out.buffer_size;

        bool is_idle_write_job = true;
#if DAC_OUTPUT_ENABLE
        is_idle_write_job &= g_job_mix_out_dac.is_idle();
#endif
#if SPDIF_OUTPUT_ENABLE
        is_idle_write_job &= g_job_mix_out_spdif.is_idle();
#endif

        if (!is_idle_write_job)
        {
            g_job_mix_out.set_pending_delay_us(100);
            return;
        }

        if (g_rx_stream_buffer.distance(g_rx_stream_buffer_write_addr, g_rx_stream_buffer_read_addr) < buffer_size)
        {
            g_job_mix_out.set_pending_delay_us(200);
            return;
        }

#if USB_IF_CONTROL_ENABLE
        g_debug_stats.outmix.src_left = g_rx_stream_buffer.distance(g_rx_stream_buffer_write_addr, g_rx_stream_buffer_read_addr);
#endif

        PROFILE_MEASURE_BEGIN(PROF_MIXOUT_USBDATA);
        
        const auto rx_stream_buffer_write_addr = g_rx_stream_buffer_write_addr;
        auto read_addr = g_rx_stream_buffer_read_addr;

        size_t fetch_bytes = buffer_size;
        g_rx_stream_buffer_read_addr =
            g_rx_stream_buffer.copy_to(rx_stream_buffer_write_addr, g_rx_stream_buffer_read_addr, data_tmp_buf.begin(), fetch_bytes);

        g_output_mixer.apply(
            g_output_mixer_rx_volume,
            data_tmp_buf.begin(), data_tmp_buf.begin() + fetch_bytes,
            mix_tmp_buf.begin(), mix_tmp_buf.begin() + fetch_bytes, true);

        PROFILE_MEASURE_END();

#if MIXING_INPUT_TO_OUTPUT_ENABLE
        auto input_mixing_buffer_write_addr = g_input_mixing_buffer_write_addr;
        const auto input_available_bytes =
            g_input_mixing_buffer.distance(input_mixing_buffer_write_addr, g_input_mixing_buffer_output_read_addr);
        if (g_output_input_converter.get_requirement_src_bytes(fetch_bytes) <= input_available_bytes)
        {
            PROFILE_MEASURE_BEGIN(PROF_MIXOUT_LINEIN_FETCH);
            auto dst = data_tmp_buf.begin();
            g_input_mixing_buffer_output_read_addr = 
                g_input_mixing_buffer.apply_linear(input_mixing_buffer_write_addr, g_input_mixing_buffer_output_read_addr, 
                [&](const uint8_t *begin, const uint8_t *end)
                {
                    auto result = g_output_input_converter.apply(begin, end, dst, data_tmp_buf.begin() + fetch_bytes);
                    dst += result.dst_advanced_bytes;
                    return result.src_advanced_bytes;
                });
            PROFILE_MEASURE_END();

            PROFILE_MEASURE_BEGIN(PROF_MIXOUT_LINEIN_MIX);
            g_output_mixer.apply(
                g_output_mixer_mixed_input_volume,
                data_tmp_buf.begin(), data_tmp_buf.begin() + fetch_bytes,
                mix_tmp_buf.begin(), mix_tmp_buf.begin() + fetch_bytes, false);
            PROFILE_MEASURE_END();
        }
        else
        {
            STREAM_LOG("input stream exhausted.\n");
        }

#if USB_IF_CONTROL_ENABLE
        g_debug_stats.outmix.input_left =
            g_input_mixing_buffer.distance(input_mixing_buffer_write_addr, g_input_mixing_buffer_output_read_addr); 
        g_debug_stats.outmix.processed_bytes += fetch_bytes;
#endif
#endif

        const auto fetch_samples = fetch_bytes / output_sample_bytes;
#if DAC_OUTPUT_ENABLE
        g_job_mix_out_dac.require_samples = fetch_samples;
        g_job_mix_out_dac.data_begin = mix_tmp_buf.begin();
        g_job_mix_out_dac.data_end = mix_tmp_buf.begin() + fetch_bytes;
        g_job_mix_out_dac.set_pending();
#endif
#if SPDIF_OUTPUT_ENABLE
        g_job_mix_out_spdif.require_samples = fetch_samples;
        g_job_mix_out_spdif.data_begin = mix_tmp_buf.begin();
        g_job_mix_out_spdif.data_end = mix_tmp_buf.begin() + fetch_bytes;
        g_job_mix_out_spdif.set_pending();
#endif
        if (g_output_device_charge_count)
            --g_output_device_charge_count;

        g_job_mix_out.set_pending_delay_us(100);
    }

#if DAC_OUTPUT_ENABLE
    static void job_mix_output_dac_write(job_queue::work *)
    {
        JOB_TRACE_LOG("job_mix_output_dac_write\n");

        auto &job = g_job_mix_out_dac;
        if (g_dac_out.is_running())
        {
            if (job.require_samples > g_dac_out.get_buffer_left_count())
            {
                job.set_pending_delay_us(200);
                return;
            }
        }

        PROFILE_MEASURE_BEGIN(PROF_MIXOUT_DAC_WRITE);
        job.result_size = g_dac_out.write(job.data_begin, job.data_end);
        PROFILE_MEASURE_END();

        if (g_output_device_charge_count == 0 && !g_dac_out.is_running())
            g_dac_out.start();
    }
#endif

#if SPDIF_OUTPUT_ENABLE
    static void job_mix_output_spdif_write(job_queue::work *)
    {
        JOB_TRACE_LOG("job_mix_output_spdif_write\n");

        auto &job = g_job_mix_out_spdif;
        if (g_spdif_out.is_running())
        {
            if (job.require_samples > g_spdif_out.get_buffer_left_count())
            {
                job.set_pending_delay_us(200);
                return;
            }
        }

        PROFILE_MEASURE_BEGIN(PROF_MIXOUT_SPDIF_WRITE);
        job.result_size = g_spdif_out.write(job.data_begin, job.data_end);
        PROFILE_MEASURE_END();

        if (g_output_device_charge_count == 0 && !g_spdif_out.is_running())
            g_spdif_out.start();
    }
#endif

    static void job_mix_input_init(job_queue::work*);
    static void job_mix_input_fetch(job_queue::work*);
    static void job_mix_input_read_adc(job_queue::work*);
    static void job_mix_input_read_spdif(job_queue::work*);

    static void start_mix_input_job()
    {
        STREAM_LOG("start mixing task\n");

#if ADC_INPUT_ENABLE
        g_job_mix_in_adc.set_callback(job_mix_input_read_adc);
        g_job_mix_in_adc.activate();
#endif
#if SPDIF_INPUT_ENABLE  
        g_job_mix_in_spdif.set_callback(job_mix_input_read_spdif);
        g_job_mix_in_spdif.activate();
#endif
        g_job_mix_in.set_callback(job_mix_input_init);
        g_job_mix_in.activate();
        g_job_mix_in.set_pending();
    }

    static void stop_mix_input_job()
    {
        STREAM_LOG("stop mixing task\n");

        g_job_mix_in.deactivate();
        g_job_mix_in.wait_done();

#if ADC_INPUT_ENABLE
        g_job_mix_in_adc.deactivate();
        g_job_mix_in_adc.wait_done();
#endif
#if SPDIF_INPUT_ENABLE  
        g_job_mix_in_spdif.deactivate();
        g_job_mix_in_spdif.wait_done();
#endif
    }

    static size_t mix_input_mixing_out(uint8_t volume, const uint8_t *src_begin, const uint8_t *src_end, bool overwrite, const char *label)
    {
        auto dst = g_input_mixing_buffer_write_addr;

        size_t src_bytes = 0;
        size_t dst_bytes = 0;
        while (src_bytes < (src_end - src_begin))
        {
            auto result = g_input_mixer.apply(volume, src_begin + src_bytes, src_end, dst, g_input_mixing_buffer.end(), overwrite);
            dst = g_input_mixing_buffer.advance(dst, result.dst_advanced_bytes);
            dst_bytes += result.dst_advanced_bytes;
            src_bytes += result.src_advanced_bytes;
        }

        return dst_bytes;
    };

    static void job_mix_input_init(job_queue::work*)
    {
        JOB_TRACE_LOG("job_mix_input_init\n");

        g_input_mixing_buffer_write_addr = g_input_mixing_buffer.begin();

        g_job_mix_in.require_samples = get_samples_duration_ms(input_mixing_processing_buffer_duration_per_cycle, g_input_sampling_frequency, device_input_channels);
        g_job_mix_in.buffer_size = g_job_mix_in.require_samples * bits_to_bytes(g_input_resolution_bits);
        g_job_mix_in.timeout = time_us_64();
        g_job_mix_in.set_callback(job_mix_input_fetch);

#if ADC_INPUT_ENABLE
        g_job_mix_in_adc.data_begin = g_adc_in_fetch_buffer.begin();
        g_job_mix_in_adc.data_end = g_adc_in_fetch_buffer.begin() + g_job_mix_in.buffer_size;
        g_job_mix_in_adc.require_samples = g_job_mix_in.require_samples;
        g_job_mix_in_adc.set_callback(job_mix_input_read_adc);
#endif
#if SPDIF_INPUT_ENABLE
        g_job_mix_in_spdif.data_begin = g_spdif_in_fetch_buffer.begin();
        g_job_mix_in_spdif.data_end = g_spdif_in_fetch_buffer.begin() + g_job_mix_in.buffer_size;
        g_job_mix_in_spdif.require_samples = g_job_mix_in.require_samples;
        g_job_mix_in_spdif.set_callback(job_mix_input_read_spdif);
#endif

        g_job_mix_in.set_pending();
    }

    static void job_mix_input_fetch(job_queue::work*)
    {
        JOB_TRACE_LOG("job_mix_input_fetch\n");

        if(time_us_64() < g_job_mix_in.timeout)
        {
            bool all_done = true;
#if ADC_INPUT_ENABLE
            if(g_job_mix_in_adc.result_size == 0 && g_job_mix_in_adc.is_idle())
                g_job_mix_in_adc.set_pending();
            all_done &= g_job_mix_in_adc.result_size > 0 && g_job_mix_in_adc.is_idle();
#endif
#if SPDIF_INPUT_ENABLE
            if(g_job_mix_in_spdif.result_size == 0 && g_job_mix_in_spdif.is_idle())
                g_job_mix_in_spdif.set_pending();
            all_done &= g_job_mix_in_spdif.result_size > 0 && g_job_mix_in_spdif.is_idle();
#endif
            if(!all_done)
            {
                g_job_mix_in.set_pending_delay_us(200);
                return;
            }
        }
        
        g_job_mix_in.timeout = g_job_mix_in.timeout + input_mixing_processing_buffer_duration_per_cycle*1000;

        bool overwrite = true;
#if ADC_INPUT_ENABLE
        PROFILE_MEASURE_BEGIN(PROF_MIXIN_ADC_MIX);
        if(g_job_mix_in_adc.result_size > 0)
        {
            mix_input_mixing_out(g_input_mixer_adc_volume, g_job_mix_in_adc.data_begin, g_job_mix_in_adc.data_end, overwrite, "adc");
            overwrite = false;
        }
        PROFILE_MEASURE_END();
#endif
#if SPDIF_INPUT_ENABLE
        PROFILE_MEASURE_BEGIN(PROF_MIXIN_SPDIF_MIX);
        if(g_job_mix_in_spdif.result_size > 0)
        {
            mix_input_mixing_out(g_input_mixer_spdif_volume, g_job_mix_in_spdif.data_begin, g_job_mix_in_spdif.data_end, overwrite, "sin");
            overwrite = false;
        }
        PROFILE_MEASURE_END();
#endif
        g_input_mixing_buffer_write_addr =
            g_input_mixing_buffer.advance(g_input_mixing_buffer_write_addr, g_job_mix_in.buffer_size);

#if ADC_INPUT_ENABLE
        g_job_mix_in_adc.result_size = 0;
        g_job_mix_in_adc.set_pending();
#endif
#if SPDIF_INPUT_ENABLE
        g_job_mix_in_spdif.result_size = 0;
        g_job_mix_in_spdif.set_pending();
#endif

#if PRINT_STATS
        g_debug_stats.inmix.processed_bytes += g_job_mix_in.buffer_size;
#endif

        g_job_mix_in.set_pending();
    }

#if ADC_INPUT_ENABLE
    static void job_mix_input_read_adc(job_queue::work*)
    {
        JOB_TRACE_LOG("job_mix_input_read_adc\n");

        auto& job = g_job_mix_in_adc;

        if (g_adc_in.is_active() && g_adc_in.is_enough_available_samples(job.require_samples))
        {
            PROFILE_MEASURE_BEGIN(PROF_MIXIN_ADC_FETCH);
            job.result_size = g_adc_in.fetch_stream_data(job.data_begin, job.data_end);
            PROFILE_MEASURE_END();
#if PRINT_STATS
            g_debug_stats.inmix.adc_in_samples = g_adc_in.get_available_samples();
#endif
        }
    }
#endif

#if SPDIF_INPUT_ENABLE
    static void job_mix_input_read_spdif(job_queue::work*)
    {
        JOB_TRACE_LOG("job_mix_input_read_spdif\n");

        auto& job = g_job_mix_in_spdif;

        if (g_spdif_in.is_signal_active() && g_spdif_in.is_enough_available_samples(job.require_samples))
        {
            PROFILE_MEASURE_BEGIN(PROF_MIXIN_SPDIF_FETCH);
            job.result_size = g_spdif_in.fetch_stream_data(job.data_begin, job.data_end);
            PROFILE_MEASURE_END();
#if PRINT_STATS
            g_debug_stats.inmix.spdif_in_samples = g_spdif_in.get_available_samples();
#endif
        }
    }
#endif

    static void dma0_irq_handler()
    {
        dbg_assert(get_core_num() == 0);

#if SPDIF_INPUT_ENABLE
        g_spdif_in.on_dma_isr();
#endif
#if SPDIF_OUTPUT_ENABLE
        g_spdif_out.on_dma_isr();
#endif
#if DAC_OUTPUT_ENABLE
        g_dac_out.on_dma_isr();
#endif
    }

    static void init_system()
    {
        STREAM_LOG("system initialize\n");

#if SPDIF_OUTPUT_ENABLE
        auto spdif_out_program_offset = (uint8_t)pio_add_program(get_sm_pio(PIO0_SM_SPDIF_OUT), &spdif_out_program);
        decltype(g_spdif_out)::init_config spdif_out_config = {
            .buffer_begin = g_spdif_out_buffer.begin(),
            .buffer_end = g_spdif_out_buffer.end(),
            .spdif_out_pio_program_offset = spdif_out_program_offset,
            .spdif_out_pio = get_sm_pio_index(PIO0_SM_SPDIF_OUT),
            .spdif_out_sm = PIO0_SM_SPDIF_OUT,
            .spdif_out_data_pin = gpio_assign::spdif_tx,
            .dma_irq_n = 0};
        g_spdif_out.init(spdif_out_config);
#endif
#if DAC_OUTPUT_ENABLE
        auto i2s_out_program_offset = (uint8_t)pio_add_program(get_sm_pio(PIO0_SM_DAC_OUT), &audio_i2s_32_out_program);
        decltype(g_dac_out)::init_config dac_out_config = {
            .buffer_begin = g_dac_out_buffer.begin(),
            .buffer_end = g_dac_out_buffer.end(),
            .i2s_out_pio_program_offset = i2s_out_program_offset,
            .i2s_out_pio = get_sm_pio_index(PIO0_SM_DAC_OUT),
            .i2s_out_sm = PIO0_SM_DAC_OUT,
            .i2s_out_data_pin = gpio_assign::dac_data,
            .i2s_out_bck_lrck_pin = gpio_assign::dac_bck_lrck,
            .dac_mute_pin = gpio_assign::dac_mute,
            .dma_irq_n = 0};
        g_dac_out.init(dac_out_config);
#endif

        set_rx_format(48000, 16);

#if ADC_INPUT_ENABLE
        auto i2s_in_program_offset = (uint8_t)pio_add_program(get_sm_pio(PIO1_SM_ADC_IN), &audio_i2s_32_in_program);
        auto adc_clk_program_offset = (uint8_t)pio_add_program(get_sm_pio(PIO0_SM_ADC_CLK), &pulse_out_program);
        decltype(g_adc_in)::init_config adc_in_config = {
            .buffer_begin = g_adc_in_buffer.begin(),
            .buffer_end = g_adc_in_buffer.end(),
            .i2s_in_pio_program_offset = i2s_in_program_offset,
            .i2s_in_pio = get_sm_pio_index(PIO1_SM_ADC_IN),
            .i2s_in_sm = PIO1_SM_ADC_IN,
            .i2s_in_data_pin = gpio_assign::adc_data,
            .i2s_in_bck_lrck_pin = gpio_assign::adc_bck_lrck,
            .i2s_in_sck_pin = gpio_assign::adc_sck,
            .clk_pio_program_offset = adc_clk_program_offset,
            .clk_pio = get_sm_pio_index(PIO0_SM_ADC_CLK),
            .clk_sm = PIO0_SM_ADC_CLK};
        g_adc_in.init(adc_in_config);
#endif

#if SPDIF_INPUT_ENABLE
        auto spdif_in_program_offset = (uint8_t)pio_add_program(get_sm_pio(PIO1_SM_SPDIF_IN), &spdif_in_program);
        auto spdif_in_raw_program_offset = (uint8_t)pio_add_program(get_sm_pio(PIO1_SM_SPDIF_IN_RAW), &spdif_in_raw_program);
        decltype(g_spdif_in)::init_config spdif_in_config = {
            .buffer_begin = g_spdif_in_buffer.begin(),
            .buffer_end = g_spdif_in_buffer.end(),
            .temp_begin = g_spdif_in_temp_buffer.begin(),
            .temp_end = g_spdif_in_temp_buffer.end(),
            .dma_irq_n = 0,
            .spdif_in_pio_program_offset = spdif_in_program_offset,
            .spdif_in_pio = get_sm_pio_index(PIO1_SM_SPDIF_IN),
            .spdif_in_sm = PIO1_SM_SPDIF_IN,
            .spdif_in_raw_sm = PIO1_SM_SPDIF_IN_RAW,
            .spdif_in_data_pin = gpio_assign::spdif_rx,
            .spdif_in_raw_pio_program_offset = spdif_in_raw_program_offset};
        g_spdif_in.init(spdif_in_config);
#endif

        set_tx_format(48000, 16);

#if SPDIF_INPUT_ENABLE
        dbg_assert(get_core_num() == 0);
        irq_set_exclusive_handler(DMA_IRQ_0, dma0_irq_handler);
        irq_set_enabled(DMA_IRQ_0, true);
#endif
    }

    void init()
    {
        STREAM_LOG("initialize\n");

        const uint8_t core0mask = (1 << 0);
        const uint8_t core1mask = (1 << 1);
        const uint8_t core_both_mask = core0mask | core1mask;

        g_job_mix_out.set_affinity_mask(core_both_mask);
        g_job_mix_in.set_affinity_mask(core_both_mask);

#if SPDIF_OUTPUT_ENABLE
        g_job_mix_out_spdif.set_affinity_mask(core_both_mask);
#endif
#if DAC_OUTPUT_ENABLE
        g_job_mix_out_dac.set_affinity_mask(core_both_mask);
#endif
#if SPDIF_INPUT_ENABLE
        g_spdif_in.set_job_affinity_mask(core1mask);
        g_job_mix_in_spdif.set_affinity_mask(core_both_mask);
#endif
#if ADC_INPUT_ENABLE
        g_job_mix_in_adc.set_affinity_mask(core_both_mask);
#endif

        init_system();
    }

#if PRINT_STATS
    void print_debug_stats()
    {
        dbg_printf(
            "  rx:\n"
            "    bytes: %u\n",
            g_debug_stats.received_bytes);
        dbg_printf(
            "  tx:\n"
            "    bytes: %u\n",
            g_debug_stats.transfar_bytes);
        dbg_printf(
            "  outmix:\n"
            "    src left: %u/%u\n"
            "    input left: %u/%u\n"
            "    processed bytes: %u\n",
            g_debug_stats.outmix.src_left, g_rx_stream_buffer.size(), g_debug_stats.outmix.input_left, g_input_mixing_buffer.size(), g_debug_stats.outmix.processed_bytes);
#if SPDIF_OUTPUT_ENABLE
        g_spdif_out.print_stats();
#endif
#if DAC_OUTPUT_ENABLE
        g_dac_out.print_stats();
#endif

        dbg_printf(
            "  inmix:\n"
            "    adc in left: %u\n"
            "    spdif in left: %u\n"
            "    processed bytes: %u\n",
            g_debug_stats.inmix.adc_in_samples, g_debug_stats.inmix.spdif_in_samples, g_debug_stats.inmix.processed_bytes);

        g_debug_stats = {};
    }
#endif

}

#endif
