#pragma once

#include <array>
#include "device_config.h"
#include "converter.h"
#include "circular_buffer.h"

namespace streaming
{
     class adc_in
    {
    public:
        struct init_config
        {
            uint32_t*  buffer_begin; 
            uint32_t*  buffer_end;

            uint8_t    i2s_in_pio_program_offset;
            uint8_t    i2s_in_pio;
            uint8_t    i2s_in_sm;
            uint8_t    i2s_in_data_pin;
            uint8_t    i2s_in_bck_lrck_pin;
            uint8_t    i2s_in_sck_pin;

            uint8_t    clk_pio_program_offset;
            uint8_t    clk_pio;
            uint8_t    clk_sm;            
        };

        void init(const init_config&);
        void start();
        void stop();
        void set_format(uint32_t sampling_frequency, uint32_t bits);
        uint32_t get_sampling_frequency();
        uint8_t get_resolution_bits();
        size_t get_available_samples() const;
        size_t fetch_stream_data(uint8_t* buffer_begin, uint8_t* buffer_end);
        bool is_enough_available_samples(size_t fetch_require_samples) const;
        bool is_active() const;

        static constexpr size_t get_buffer_size(uint16_t duration_ms) { return max_input_samples_1ms*duration_ms; }
        template<uint16_t DurationMS> using buffer = std::array<uint32_t, get_buffer_size(DurationMS)>;
    private:

        init_config                     m_config;
        data_structure::circular_buffer<data_structure::container_range<uint32_t>> m_stream_buffer;
        uint32_t*                       m_stream_buffer_read_addr = nullptr;
        int                             m_dma_ch = 0;
        int                             m_dma_ctrl_ch = 0;
        uint32_t                        m_sampling_frequency = 48000;
        uint8_t                         m_resolution_bits = 16;
        alignas(16) uint32_t*           m_dma_control_blocks[2] = {};
        processing::converter           m_converter;
        bool                            m_running = false;
    };
}

#include "streaming_adc_in_impl.h"
