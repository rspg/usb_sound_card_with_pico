#pragma once
#include <hardware/pio.h>
#include "device_config.h"

namespace support
{

    void init_out_pin(uint pin, bool value);
    void init_in_pin(uint pin);
    void setup_dma_circular_read_config(int ch, int ctrl_ch, uint32_t dreq, io_wo_32 *write_adr, uint32_t count, bool irq_quiet);
    void setup_dma_circular_write_config(int ch, int ctrl_ch, uint32_t dreq, io_ro_32 *read_adr, uint32_t count, bool irq_quiet);
    void abort_dma_chainning_channles(uint32_t ch1, uint32_t ch2);

    struct OutputWriteResult
    {
        size_t wrote_samples;
        size_t consumed_data_bytes;
    };

    template<typename T>
    constexpr bool is_power2(T val)
    {
        while((val&1) == 0) val >>= 1;
        return val == 1;
    }

    constexpr uint8_t bits_to_bytes(uint8_t bits)
    {
        return (bits+7)/8;
    }

    constexpr size_t get_epin_packet_bytes(uint32_t freq, uint8_t resolution_bits)
    {
        return freq / 1000 * bits_to_bytes(resolution_bits) * device_input_channels;
    }

    constexpr size_t get_samples_duration_ms(uint16_t ms, uint32_t freq, uint8_t channels)
    {
        return ms*freq*channels/1000;
    }

    template <uint8_t Bits, bool Signed>
    uint32_t bytes_to_dword(const uint8_t *p)
    {
        static_assert(Bits > 0 && Bits <= 32);

        constexpr uint8_t bytes = bits_to_bytes(Bits);
        
        uint32_t value = 0;
        if constexpr (bytes >= 4)
            value |= p[3] << 24;
        if constexpr (bytes >= 3)
            value |= p[2] << 16;
        if constexpr (bytes >= 2)
            value |= p[1] << 8;
        if constexpr (bytes >= 1)
            value |= p[0];

        constexpr uint32_t mask = Bits<32 ? (((uint32_t)1<<Bits) - 1) : (uint32_t)-1;

        value &= mask;
        if constexpr (Signed && Bits < 32){
            constexpr uint32_t sign_bit = 1<<(Bits - 1);
            value |= (((value&sign_bit)^sign_bit) - 1)&(~mask);
        }
        
        return value;
    }

    template<uint8_t Bits> void copy_dword(uint8_t* dst, uint32_t value)
    {
        static_assert(Bits > 0 && Bits <= 32);
        value &= (Bits<32 ? ((uint32_t)1<<Bits) - 1 : (uint32_t)-1);
        if constexpr (Bits > 0) dst[0] = ((const uint8_t*)&value)[0];
        if constexpr (Bits > 8) dst[1] = ((const uint8_t*)&value)[1];
        if constexpr (Bits > 16) dst[2] = ((const uint8_t*)&value)[2];
        if constexpr (Bits > 24) dst[3] = ((const uint8_t*)&value)[3];
    }

    template<uint8_t Bits, bool Signed> uint32_t blend_value(uint32_t v0, uint32_t v1, uint8_t alpha)
    {
        if constexpr (Bits >= 32)
        {
            uint64_t v0_64 = (uint64_t)(int32_t)v0;
            uint64_t v1_64 = (uint64_t)(int32_t)v1;
            if constexpr(Signed)
                return (uint32_t)(v0_64 + ((int64_t)((v1_64 - v0_64)*alpha) >> 8));
            else
                return (uint32_t)(v0_64 + (((v1_64 - v0_64)*alpha) >> 8));
        }
        else
        {
            if constexpr(Signed)
                return v0 + ((int32_t)((v1 - v0)*alpha) >> 8);
            else
                return v0 + (((v1 - v0)*alpha) >> 8);
        }
    }

    inline uint32_t is_bits_odd(uint32_t value)
    {
        value ^= value >> 16;
        value ^= value >> 8;
        value ^= value >> 4;
        value &= 0xf;
        return (0x6996 >> value) & 1;
    }

    inline size_t round_frame_bytes(size_t bytes, uint8_t bits, uint8_t channels)
    {
        const size_t frameBytes = bits_to_bytes(bits)*channels;
        return bytes/frameBytes*frameBytes;
    }

    inline PIO get_pio(int index) { return (index==0) ? pio0 : pio1; }

}