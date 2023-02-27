#pragma once

#include <utility>
#include "spdifdefs.h"
#include "device_config.h"
#include "circular_buffer.h"

namespace streaming
{
    
    class dac_out
    {
    public:
        struct init_config
        {
            uint32_t* buffer_begin;
            uint32_t* buffer_end;
            uint8_t i2s_out_pio_program_offset;
            uint8_t i2s_out_pio;
            uint8_t i2s_out_sm;
            uint8_t i2s_out_data_pin;
            uint8_t i2s_out_bck_lrck_pin;
            uint8_t dac_mute_pin;
            uint8_t dma_irq_n;
        };

        void init(const init_config &);
        void start();
        void stop();
        void set_format(uint32_t sampling_frequency, uint32_t bits);
        size_t write(const uint8_t* begin, const uint8_t* end);
        void on_dma_isr();

        bool is_running() const { return m_running; }
        uint32_t get_consumed_samples() const;
        uint32_t get_buffer_available_samples() const;
        uint32_t get_buffer_left_count() const;

#if PRINT_STATS
        void print_stats();
#endif

        static constexpr size_t get_buffer_size(uint16_t duration_ms) { return max_output_samples_1ms*duration_ms; }
        template<uint16_t DurationMS> using buffer = std::array<uint32_t, get_buffer_size(DurationMS)>;
    private:
        init_config m_config;
        data_structure::circular_buffer<data_structure::container_range<uint32_t>> m_stream_buffer;
        uint32_t* m_stream_buffer_write_addr = nullptr;
        uint64_t  m_stream_buffer_dma_read_samples = 0;
        int m_dma_ch = 0;
        int m_dma_ctrl_ch = 0;
        alignas(16) uint32_t *m_dma_control_blocks[2] = {};
        uint8_t m_resolution_bits = 16;
        bool m_running = false;

#if PRINT_STATS
        uint32_t m_debug_available_samples = 0;
#endif

        void abort_dma_transfar();
        void process_data_samples();
    };

}

#include "streaming_dac_out_impl.h"