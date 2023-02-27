#pragma once

#include <stdint.h>
#include "usb_config.h"

#define SPDIF_OUTPUT_ENABLE     1
#define DAC_OUTPUT_ENABLE       1
#define SPDIF_INPUT_ENABLE      1
#define ADC_INPUT_ENABLE        1
#define MIXING_INPUT_TO_OUTPUT_ENABLE  1

constexpr uint8_t  device_output_channels = 2;
constexpr uint8_t  device_input_channels = 2;
constexpr uint8_t  max_resolution_bits = 24;
constexpr uint32_t max_sampling_frequency = 96000;
constexpr uint32_t max_byte_per_sample = max_resolution_bits*device_output_channels/8; // 24bit 2ch
constexpr size_t   max_output_samples_1ms = max_sampling_frequency*device_output_channels/1000;
constexpr size_t   max_input_samples_1ms = max_sampling_frequency*device_input_channels/1000;


namespace gpio_assign
{
    constexpr uint spdif_tx = 14;
    constexpr uint spdif_rx = 15;

    constexpr uint dac_data = 6;
    constexpr uint dac_bck_lrck = 7;
    constexpr uint dac_sck = 9;
    constexpr uint dac_mute = 10;

    constexpr uint adc_data = 2;
    constexpr uint adc_bck_lrck = 3;
    constexpr uint adc_sck = 5;
}

