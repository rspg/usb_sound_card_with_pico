#include <hardware/dma.h>
#include <hardware/clocks.h>
#include "spdif_out.pio.h"
#include "support.h"
#include "streaming_internal.h"


#define SPDIF_OUT_LOG(...)   TU_LOG1("[SPDIF_OUT] " __VA_ARGS__);

namespace streaming
{

using namespace support;

constexpr uint32_t spdif_output_cycles_per_bit = 8;;


template <uint8_t Bits, bool Signed>
uint32_t to_dword24(const uint8_t *p)
{
    if constexpr (Bits > 24)
        return (bytes_to_dword<Bits, Signed>(p) >> (Bits - 24)) & 0x00ffffff;
    else
        return (bytes_to_dword<Bits, Signed>(p) << (24 - Bits)) & 0x00ffffff;
}

template<size_t Bits> OutputWriteResult write_spdif_data_impl(uint32_t start_sample_idx, uint32_t* dst_begin, uint32_t* dst_end, const uint8_t* data_begin, const uint8_t* data_end, const spdif::status_bits& status_bits)
{
    uint32_t preamble_idx = (start_sample_idx==0) ? 0 : (((start_sample_idx + 1)&1) + 1);
    uint32_t sample_idx = start_sample_idx;
    constexpr auto bytes = bits_to_bytes(Bits);

    auto p = data_begin;
    for(; p < data_end && sample_idx < spdif::block_samples && dst_begin < dst_end; p += bytes)
    {
        uint32_t flags = (0 << spdif::validity_shift_lsb) | (0 << spdif::userdata_shift_lsb)  | (status_bits[sample_idx>>1] << spdif::control_shift_lsb);
        uint32_t data = (to_dword24<Bits, true>(p) << spdif::data_shift_lsb) | flags;
        *dst_begin++ = (spdif::preambles[preamble_idx] << spdif::preamble_shift_lsb) | data | ((__builtin_popcount(data)&1) << spdif::parity_shift_lsb);
        
        preamble_idx = (preamble_idx&1) + 1;
        sample_idx++;
    }

    return { sample_idx - start_sample_idx, (size_t)(p - data_begin) };
}

static OutputWriteResult write_spdif_data(uint32_t start_sample_idx, uint32_t* dst_begin, uint32_t* dst_end, const uint8_t* data_begin, const uint8_t* data_end, uint8_t data_resolution_bits, const spdif::status_bits& status_bits)
{
    if(data_resolution_bits == 16)
    {
        return write_spdif_data_impl<16>(start_sample_idx, dst_begin, dst_end, data_begin, data_end, status_bits);
    }
    else if(data_resolution_bits == 24)
    {
        return write_spdif_data_impl<24>(start_sample_idx, dst_begin, dst_end, data_begin, data_end, status_bits);
    }

    return {0, 0};
}

static uint32_t clear_spdif_buffer(uint32_t index, uint32_t* begin, uint32_t* end, const spdif::status_bits& status_bits)
{
    uint8_t clear[8] = {0};
    uint32_t sample_idx = index;

    while(begin < end)
    {
        auto result = write_spdif_data(sample_idx, begin, end, std::begin(clear), std::end(clear), 16, status_bits);
        begin += result.wrote_samples;
        sample_idx += result.wrote_samples;
        if(sample_idx >= spdif::block_samples)
            sample_idx = 0;
    }

    return sample_idx;
}


void spdif_out::init(const init_config& config)
{
    m_config = config;

    spdif_out_program_init(get_pio(m_config.spdif_out_pio), m_config.spdif_out_sm, m_config.spdif_out_pio_program_offset, m_config.spdif_out_data_pin);

    m_dma_ch = dma_claim_unused_channel(true);
    m_dma_ctrl_ch = dma_claim_unused_channel(true);
    setup_dma_circular_read_config(
        m_dma_ch, m_dma_ctrl_ch, 
        pio_get_dreq(get_pio(m_config.spdif_out_pio), m_config.spdif_out_sm, true), 
        &get_pio(m_config.spdif_out_pio)->txf[m_config.spdif_out_sm], 
        0,
        false);

    dma_irqn_set_channel_enabled(m_config.dma_irq_n, m_dma_ch, true);

    clear_spdif_buffer(0, config.buffer_begin, config.buffer_end, std::bitset<192>(0));
    m_dma_control_blocks[0] = config.buffer_begin;
    m_stream_buffer = { m_config.buffer_begin, m_config.buffer_end };
    m_stream_buffer_write_addr = m_stream_buffer.begin();

    SPDIF_OUT_LOG("initialized buffer size=%u\n", config.buffer_end - config.buffer_begin);
}


void spdif_out::start()
{
    if(m_running) return;

    SPDIF_OUT_LOG("start transfarring\n");

    pio_sm_set_enabled(get_pio(m_config.spdif_out_pio), m_config.spdif_out_sm, true);
    dma_channel_set_trans_count(m_dma_ch, m_stream_buffer.size(), false);
    dma_channel_transfer_from_buffer_now(m_dma_ctrl_ch, m_dma_control_blocks, 1);

    m_stream_buffer_dma_read_samples = 0;
    m_processed_samples = 0;
    m_running = true;
}

void spdif_out::stop()
{
    if(!m_running) return;

    SPDIF_OUT_LOG("stop transfarring\n");

    m_running = false;

    pio_sm_set_enabled(get_pio(m_config.spdif_out_pio), m_config.spdif_out_sm, false);
    abort_dma_transfar();

    clear_spdif_buffer(0, m_config.buffer_begin, m_stream_buffer.end(), m_status_bits);
}

void spdif_out::abort_dma_transfar()
{
    SPDIF_OUT_LOG("stop dma\n");

    abort_dma_chainning_channles(m_dma_ch, m_dma_ctrl_ch);
}

void spdif_out::set_format(uint32_t sampling_frequency, uint32_t bits)
{
    SPDIF_OUT_LOG("set format freq=%d, bits=%d\n", sampling_frequency, bits);

    m_resolutin_bits = bits;

    m_status_bits.reset();
    spdif::set_status_frequency(m_status_bits, sampling_frequency);
    spdif::set_status_resolution(m_status_bits, spdif::bit24);

    const uint16_t blocks = (m_config.buffer_end - m_config.buffer_begin)*sampling_frequency/(spdif::block_samples*max_sampling_frequency);
    m_stream_buffer.resize(blocks*spdif::block_samples);
    m_stream_buffer_write_addr = m_config.buffer_begin;

    clear_spdif_buffer(0, m_config.buffer_begin, m_stream_buffer.end(), m_status_bits);

    const uint32_t spdif_output_frequency = sampling_frequency*device_output_channels*32*spdif_output_cycles_per_bit;
    pio_sm_set_clkdiv(get_pio(m_config.spdif_out_pio), m_config.spdif_out_sm, (float)(clock_get_hz(clk_sys)/(double)spdif_output_frequency));
}

size_t spdif_out::write(const uint8_t *begin, const uint8_t * end)
{
    auto p = begin;
    while(p < end)
    {
        auto result = write_spdif_data(
            m_processed_samples, m_stream_buffer_write_addr, m_stream_buffer.end(), p, end, m_resolutin_bits, m_status_bits);
        m_processed_samples += result.wrote_samples;
        if(m_processed_samples >= spdif::block_samples)
            m_processed_samples = 0;
        m_stream_buffer_write_addr = m_stream_buffer.advance(m_stream_buffer_write_addr, result.wrote_samples);
        p += result.consumed_data_bytes;
    }

#if PRINT_STATS
        m_debug_available_samples = get_buffer_available_samples();
#endif

    return p - begin;
}

void spdif_out::on_dma_isr()
{
    if(dma_irqn_get_channel_status(m_config.dma_irq_n, m_dma_ch))
    {
        m_stream_buffer_dma_read_samples += m_stream_buffer.size();
        dma_irqn_acknowledge_channel(m_config.dma_irq_n, m_dma_ch);
    }
}

uint32_t spdif_out::get_consumed_samples() const
{
    auto read_addr = m_running ? (uint32_t*)dma_channel_hw_addr(m_dma_ch)->al1_read_addr : m_config.buffer_begin;
    return m_stream_buffer_dma_read_samples + (read_addr - m_config.buffer_begin);
}

uint32_t spdif_out::get_buffer_available_samples() const
{
    auto dma = dma_channel_hw_addr(m_dma_ch);
    auto read_addr = m_running ? (uint32_t*)dma->al1_read_addr : m_config.buffer_begin;
    return m_stream_buffer.distance(m_stream_buffer_write_addr, read_addr);
}

uint32_t spdif_out::get_buffer_left_count() const
{
    return (m_stream_buffer.size()) - get_buffer_available_samples();
}

#if PRINT_STATS
void spdif_out::print_stats()
{
    dbg_printf(
        "  spdif out:\n"
        "    left: %u/%u\n"
        "    consumed: %u\n",
        m_debug_available_samples, m_stream_buffer.size(),
        get_consumed_samples());
}
#endif

}