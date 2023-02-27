#pragma once

#include <utility>
#include <array>
#include <bitset>
#include "spdifdefs.h"
#include "device_config.h"
#include "circular_buffer.h"

namespace streaming
{
    class spdif_out
    {
    public:
        struct init_config
        {
            uint32_t*   buffer_begin;
            uint32_t*   buffer_end;
            uint8_t     spdif_out_pio_program_offset;
            uint8_t     spdif_out_pio;
            uint8_t     spdif_out_sm;
            uint8_t     spdif_out_data_pin;
            uint8_t     dma_irq_n;
        };

        void init(const init_config &);
        void start();
        void stop();
        void set_format(uint32_t sampling_frequency, uint32_t bits);
        size_t write(const uint8_t *begin, const uint8_t * end);
        void on_dma_isr();
        
        bool is_running() const { return m_running; }
        uint32_t get_consumed_samples() const;
        uint32_t get_buffer_available_samples() const;
        uint32_t get_buffer_left_count() const;

#if PRINT_STATS
        void print_stats();
#endif
        
        static constexpr size_t get_buffer_size(uint16_t duration_ms) { return spdif::block_samples*((duration_ms + 1)/2); }
        template<uint16_t DurationMS> using buffer = std::array<uint32_t, get_buffer_size(DurationMS)>;
    private:

        init_config m_config;
        int m_dma_ch = 0;
        int m_dma_ctrl_ch = 0;
        alignas(16) uint32_t *m_dma_control_blocks[2] = {};
        data_structure::circular_buffer<data_structure::container_range<uint32_t>> m_stream_buffer;
        uint32_t* m_stream_buffer_write_addr = nullptr;
        uint64_t  m_stream_buffer_dma_read_samples = 0;
        spdif::status_bits m_status_bits = {0};
        uint8_t m_resolutin_bits = 16;
        uint16_t m_processed_samples = 0;
        bool m_running = false;

#if PRINT_STATS
        uint32_t m_debug_available_samples = 0;
#endif

        void abort_dma_transfar();
        void process_data_samples();
    };

}

#include "streaming_spdif_out_impl.h"