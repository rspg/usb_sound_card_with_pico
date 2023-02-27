#pragma once

#include <bitset>
#include "spdifdefs.h"
#include "device_config.h"
#include "converter.h"
#include "job_queue.h"
#include "circular_buffer.h"

namespace streaming
{
    class spdif_in
    {
    public:
        struct init_config
        {
            uint32_t* buffer_begin;
            uint32_t* buffer_end;
            uint32_t* temp_begin;
            uint32_t* temp_end;
            uint8_t dma_irq_n;
            uint8_t spdif_in_pio_program_offset;
            uint8_t spdif_in_pio;
            uint8_t spdif_in_sm;
            uint8_t spdif_in_raw_sm;
            uint8_t spdif_in_data_pin;
            uint8_t spdif_in_raw_pio_program_offset;
        };

        void init(const init_config &);
        void start();
        void stop();
        void set_job_affinity_mask(uint8_t affinity);
        void set_output_format(uint32_t freq, uint8_t bits);
        uint32_t get_sampling_frequency(bool indicated_by_status);
        uint8_t get_resolution_bits();
        size_t get_available_samples() const;
        size_t fetch_stream_data(uint8_t *buffer_begin, uint8_t *buffer_end);
        bool is_signal_active() const { return m_signal_active; }
        bool is_enough_available_samples(size_t fetch_require_samples) const;
        bool is_running() const { return m_running; }

        void on_dma_isr();

        static constexpr size_t get_buffer_size(uint16_t duration_ms) { return spdif::block_samples*((duration_ms + 1)/2); }
        template<uint16_t DurationMS> using buffer = std::array<uint32_t, get_buffer_size(DurationMS)>;
    private:
        struct job_signal_work : public job_queue::work
        {
            uint8_t  interval_min = 0;
            uint8_t  interval_max = 0;
            size_t   interval_sample_num = 0;
            uint32_t interval_min_samples[8];
            uint32_t interval_max_samples[8];
            uint32_t  failure_count = 0;
            spdif_in* this_ptr = nullptr;
            void (spdif_in::*method)() = nullptr;
            bool first_call = false;

            virtual void operator()() override
            {
                if(this_ptr && method) (this_ptr->*method)();
                first_call = false;
            }
        };

        init_config m_config;
        int m_raw_dma_ch = 0;
        int m_dma_ch = 0;
        int m_dma_ctrl_ch = 0;
        spdif::status_bits m_status_bits = {0};
        bool m_signal_active = false;
        bool m_signal_inverted = false;
        data_structure::circular_buffer<data_structure::container_range<uint32_t>> m_stream_buffer;
        uint32_t *m_stream_buffer_read_addr = nullptr;
        uint32_t m_sampling_frequency = 48000;
        uint32_t m_output_frequency = 48000;
        uint8_t m_output_resolution_bits = 16;
        bool  m_preamble_phase_w = false;
        alignas(16) uint32_t *m_dma_control_blocks[2] = {};
        job_signal_work m_job_work;
        processing::converter m_converter;
        uint16_t m_fetch_temp_top = 0;
        uint16_t m_fetch_temp_tail = 0;
        bool m_running = false;

        void set_sampling_frequency(uint32_t freq);
        void raise_error();
        void change_job_method(void(spdif_in::*method)(), bool pending); 
        void set_pending_job();
        void set_pending_job(uint32_t delay_us);

        void job_init();
        void job_detect_kick_dma_raw_data();
        void job_detect_finish_dma_raw_data();
        void job_detect_signal_clock_interval();
        void job_sync_spdif_frame();
        void job_scan_block_first();
        void job_parse_control_block();
        
        bool check_sample_preamble(uint8_t preamble);
        size_t get_available_samples_internal() const;
        void update_convert_context();
    };

}

#include "streaming_spdif_in_impl.h"
