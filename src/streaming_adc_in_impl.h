#include <hardware/dma.h>
#include <hardware/clocks.h>
#include "support.h"
#include "audio_i2s_32_in.pio.h"
#include "pulse_out.pio.h"


#define ADC_IN_LOG(...)   TU_LOG1("[ADC_IN] " __VA_ARGS__);

namespace streaming
{

using namespace support;

constexpr uint32_t i2s_input_cycles_per_bit = 2;
constexpr uint8_t  adc_in_resolution_bits = 32;

void adc_in::init(const init_config& config)
{
    m_config = config;

    m_stream_buffer = { m_config.buffer_begin, m_config.buffer_end };

    audio_i2s_32_in_program_init(get_pio(m_config.i2s_in_pio), m_config.i2s_in_sm, m_config.i2s_in_pio_program_offset, m_config.i2s_in_data_pin, m_config.i2s_in_bck_lrck_pin);
    pulse_out_program_init(get_pio(m_config.clk_pio), m_config.clk_sm, m_config.clk_pio_program_offset, m_config.i2s_in_sck_pin);

    m_dma_ctrl_ch = dma_claim_unused_channel(true);
    m_dma_ch = dma_claim_unused_channel(true); 
    setup_dma_circular_write_config(
        m_dma_ch, m_dma_ctrl_ch, 
        pio_get_dreq(get_pio(m_config.i2s_in_pio), m_config.i2s_in_sm, false), 
        &get_pio(m_config.i2s_in_pio)->rxf[m_config.i2s_in_sm], 
        0, 
        true);

    m_dma_control_blocks[0] = config.buffer_begin;

    ADC_IN_LOG("initialized buffer size=%u\n", config.buffer_end - config.buffer_begin);
}


void adc_in::start()
{
    if(m_running) return;

    ADC_IN_LOG("start transfarring\n");

    m_stream_buffer_read_addr = m_config.buffer_begin;

    pio_sm_set_enabled(get_pio(m_config.clk_pio), m_config.clk_sm, true);
    pio_sm_set_enabled(get_pio(m_config.i2s_in_pio), m_config.i2s_in_sm, true);

    dma_channel_set_trans_count(m_dma_ch, m_stream_buffer.size(), false);
    dma_channel_transfer_from_buffer_now(m_dma_ctrl_ch, m_dma_control_blocks, 1);

    m_running = true;
}

void adc_in::stop()
{
    if(!m_running) return;

    ADC_IN_LOG("stop transfarring\n");

    m_running = false;

    pio_sm_set_enabled(get_pio(m_config.i2s_in_pio), m_config.i2s_in_sm, false);
    pio_sm_set_enabled(get_pio(m_config.clk_pio), m_config.clk_sm, false);
    abort_dma_chainning_channles(m_dma_ch, m_dma_ctrl_ch);

    std::fill(m_config.buffer_begin, m_config.buffer_end, 0);
}


void adc_in::set_format(uint32_t sampling_frequency, uint32_t bits)
{
    ADC_IN_LOG("set format freq=%d, bits=%d\n", sampling_frequency, bits);

    m_sampling_frequency = sampling_frequency;
    m_resolution_bits = bits;

    const size_t duration = (m_config.buffer_end - m_config.buffer_begin)/max_input_samples_1ms;
    m_stream_buffer.resize(get_samples_duration_ms(duration, sampling_frequency, device_input_channels));
    m_stream_buffer_read_addr = m_config.buffer_begin;

    const uint32_t adc_output_frequency = sampling_frequency*device_input_channels*32*i2s_input_cycles_per_bit;
    const auto adc_output_frequency_div = (float)(clock_get_hz(clk_sys)/(double)adc_output_frequency);
    pio_sm_set_clkdiv(get_pio(m_config.i2s_in_pio), m_config.i2s_in_sm, adc_output_frequency_div);
    
    const uint32_t adc_scki_frequency = sampling_frequency*256*2;
    const auto adc_scki_frequency_div = (float)(clock_get_hz(clk_sys)/(double)adc_scki_frequency);
    pio_sm_set_clkdiv(get_pio(m_config.clk_pio), m_config.clk_sm, adc_scki_frequency_div);

    processing::converter::config convert_config = {
        .src_bits = adc_in_resolution_bits,
        .src_stride = bits_to_bytes(adc_in_resolution_bits),
        .src_freq = m_sampling_frequency,
        .dst_bits = m_resolution_bits,
        .dst_stride = bits_to_bytes(m_resolution_bits),
        .dst_freq = m_sampling_frequency,
        .channels = device_input_channels,
        .use_interp = true
    };
    m_converter.setup(convert_config);
}


uint32_t adc_in::get_sampling_frequency()
{
    return m_sampling_frequency;
}


uint8_t adc_in::get_resolution_bits()
{
    return m_resolution_bits;
}

size_t adc_in::get_available_samples() const
{
    auto adc_dma = dma_channel_hw_addr(m_dma_ch);
    return m_stream_buffer.distance((uint32_t*)adc_dma->write_addr, m_stream_buffer_read_addr);
}

bool adc_in::is_enough_available_samples(size_t fetch_require_samples) const
{
    return get_available_samples() > m_converter.get_requirement_src_samples(fetch_require_samples);
}

size_t adc_in::fetch_stream_data(uint8_t* buffer_begin, uint8_t* buffer_end)
{
    if(!m_running) return 0;

    auto adc_dma = dma_channel_hw_addr(m_dma_ch);
    auto dst = buffer_begin;

    constexpr uint32_t frame_bytes = bits_to_bytes(adc_in_resolution_bits) * device_input_channels;
    auto write_addr = (uint32_t*)((adc_dma->write_addr/frame_bytes)*frame_bytes);

    if(m_stream_buffer.distance((uint32_t*)write_addr, m_stream_buffer_read_addr) < device_input_channels)
        return 0;

    m_stream_buffer_read_addr = m_stream_buffer.apply_linear(write_addr, m_stream_buffer_read_addr, 
        [&](auto begin_addr, auto end_addr){
            auto result = m_converter.apply((uint8_t*)begin_addr, (uint8_t*)end_addr, dst, buffer_end);
            dst += result.dst_advanced_bytes;
            return result.src_advanced_bytes/4;
        });

    dbg_assert((dst - buffer_begin)%(m_converter.get_config().dst_stride*device_input_channels) == 0);

    return dst - buffer_begin;
}

bool adc_in::is_active() const
{
    return m_stream_buffer_read_addr != nullptr;
}

}