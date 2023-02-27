#include <pico/types.h>
#include "spdifdefs.h"

namespace spdif
{
    void set_status_frequency(status_bits& status, uint32_t freq)
    {
        uint32_t spdif_freq = 0;
        switch(freq)
        {
            case 22050:
                spdif_freq = control_frequency_22k05;
                break;
            case 24000:
                spdif_freq = control_frequency_24k;
                break;
            case 32000:
                spdif_freq = control_frequency_32k;
                break;
            case 44100:
                spdif_freq = control_frequency_44k1;
                break;
            case 48000:
                spdif_freq = control_frequency_48k;
                break;
            case 88200:
                spdif_freq = control_frequency_88k2;
                break;
            case 96000:
                spdif_freq = control_frequency_96k;
                break;
            case 176400:
                spdif_freq = control_frequency_176k4;
                break;
            case 192000:
                spdif_freq = control_frequency_192k;
                break;
        }

        status[24] = (spdif_freq&0b1000) ? 1 : 0;
        status[25] = (spdif_freq&0b0100) ? 1 : 0;
        status[26] = (spdif_freq&0b0010) ? 1 : 0;
        status[27] = (spdif_freq&0b0001) ? 1 : 0;
    }

    void set_status_resolution(status_bits& status, resolution resolution)
    {
        status[32] = resolution==bit20 ? 0 : 1;
    }

    uint32_t get_status_frequency(const status_bits &status)
    {
        const uint32_t value = bool_to_bit(status[24]) << 3 | bool_to_bit(status[25]) << 2 | bool_to_bit(status[26]) << 1 | bool_to_bit(status[27]);
        switch (value)
        {
        case spdif::control_frequency_22k05:
            return 22050;
        case spdif::control_frequency_24k:
            return 24000;
        case spdif::control_frequency_32k:
            return 32000;
        case spdif::control_frequency_44k1:
            return 44100;
        case spdif::control_frequency_48k:
            return 48000;
        case spdif::control_frequency_88k2:
            return 88200;
        case spdif::control_frequency_96k:
            return 96000;
        case spdif::control_frequency_176k4:
            return 176400;
        case spdif::control_frequency_192k:
            return 192000;
        }
        return 0;
    }

    resolution get_status_resolution(const status_bits& status)
    {
        return status[32] ? bit24 : bit20;
    }

}