#pragma once

#include <bitset>
#include "device_config.h"

namespace spdif
{

    constexpr uint32_t control_frequency_22k05 = 0b0010;
    constexpr uint32_t control_frequency_24k = 0b0110;
    constexpr uint32_t control_frequency_32k = 0b1100;
    constexpr uint32_t control_frequency_44k1 = 0b0000;
    constexpr uint32_t control_frequency_48k = 0b0100;
    constexpr uint32_t control_frequency_88k2 = 0b0001;
    constexpr uint32_t control_frequency_96k = 0b0101;
    constexpr uint32_t control_frequency_176k4 = 0b0011;
    constexpr uint32_t control_frequency_192k = 0b0111;
    constexpr uint32_t preamble_b = 0b1101;
    constexpr uint32_t preamble_m = 0b0111;
    constexpr uint32_t preamble_w = 0b1011;
    constexpr uint32_t preamble_shift_lsb = 0;
    constexpr uint32_t preamble_mask = 0b1111;
    constexpr uint32_t preambles[] = { preamble_b, preamble_w, preamble_m };
    constexpr uint32_t data_shift_lsb = 4;
    constexpr uint32_t validity_shift_lsb = 28;
    constexpr uint32_t userdata_shift_lsb = 29;
    constexpr uint32_t control_shift_lsb = 30;
    constexpr uint32_t parity_shift_lsb = 31;
    constexpr size_t  block_frames = 192;
    constexpr size_t  block_samples = block_frames*device_output_channels;
    constexpr size_t  frame_bytes = (32/8)*device_output_channels;
    constexpr size_t  block_bytes = frame_bytes*block_frames;

    using status_bits = std::bitset<block_frames>;

    enum resolution : uint8_t
    {
        bit20 = 20, bit24 = 24
    };

    void set_status_frequency(status_bits& status, uint32_t freq);
    void set_status_resolution(status_bits& status, resolution resolution);
    uint32_t get_status_frequency(const status_bits& status);
    resolution get_status_resolution(const status_bits& status);
}