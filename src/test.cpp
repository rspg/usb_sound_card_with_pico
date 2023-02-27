#include <pico/time.h>
#include <array>
#include <memory>
#include <algorithm>
#include <string.h>
#include "debug.h"
#include "support.h"
#include "converter.h"
#include "mixer.h"


using namespace support;


void test_conversion(uint8_t src_bits, uint32_t src_freq, uint8_t dst_bits, uint32_t dst_freq,  bool use_interp)
{
    dbg_printf("src_bits=%d src_freq=%d dst_bits=%d dst_freq=%d interp=%s\n",
        src_bits, src_freq, dst_bits, dst_freq, use_interp ? "on" : "off");

    constexpr uint32_t src_samples_num = 64;

    static uint8_t src_data[4 * src_samples_num];
    static uint8_t dst_data[4 * src_samples_num * 4];
    auto p_src_data = src_data;
    auto p_dst_data = dst_data;

    const int src_byte_size = bits_to_bytes(src_bits);
    const int dst_byte_size = bits_to_bytes(dst_bits);
    for (uint32_t i = 0; i < src_samples_num; ++i)
    {
        auto val = (((int)src_samples_num / 2 - i) * (65535/src_samples_num)) << (src_bits - 16);
        
        auto p = src_data + i * src_byte_size;
        switch(src_bits)
        {
            case 16: copy_dword<16>(p, val); break;
            case 20: copy_dword<20>(p, val); break;
            case 24: copy_dword<24>(p, val); break;
            case 32: copy_dword<32>(p, val); break;
        }
    }

    const auto src_total_size = src_samples_num * src_byte_size;
    const int src_part_sizes[] = { (int)src_total_size, (int)src_total_size, src_byte_size, src_byte_size };
    const int dst_part_sizes[] = { sizeof(dst_data), dst_byte_size, sizeof(dst_data), dst_byte_size };

    for(int i = 0; i < 4; ++i)
    {
        const int src_part_size = src_part_sizes[i];
        const int dst_part_size = dst_part_sizes[i];

        const processing::converter::config config = {
            .src_bits =  src_bits,
            .src_stride = (uint8_t)src_byte_size,
            .src_freq = src_freq,
            .dst_bits = dst_bits,
            .dst_stride = (uint8_t)dst_byte_size,
            .dst_freq = dst_freq,
            .channels = 1,
            .use_interp = use_interp
        };

        processing::converter converter;
        converter.setup(config);

        size_t dst_offset = 0;
        size_t src_offset = 0;
        while(src_offset < src_total_size)
        {
            auto result = converter.apply( 
                src_data + src_offset, src_data + src_offset + src_part_size, 
                dst_data + dst_offset, dst_data + dst_offset + dst_part_size);
            dst_offset += result.dst_advanced_bytes;
            src_offset += result.src_advanced_bytes;
        }

        dbg_printf("%d: ", i);

        auto *p = dst_data;
        while (p < dst_data + dst_offset)
        {
            int value = 0;
            switch(dst_bits)
            {
                case 16: value = bytes_to_dword<16, true>(p); break;
                case 20: value = bytes_to_dword<20, true>(p); break;
                case 24: value = bytes_to_dword<24, true>(p); break;
                case 32: value = bytes_to_dword<32, true>(p); break;
            }
            p += config.dst_stride;
            dbg_printf("%d ", value);
        }
        dbg_printf("\n");
    }

    //dbg_printf("spend %s %u\n", use_interp ? "interp" : "instr", time_us_32() - time);
}

void measure_conversion(bool use_interp)
{
    constexpr uint32_t src_samples_num = 64;

    static uint8_t src_data[4 * src_samples_num];
    static uint8_t dst_data[4 * src_samples_num * 4];
    static uint32_t result[src_samples_num * 4];
    auto p_src_data = src_data;
    auto p_dst_data = dst_data;
    auto p_result = result;

    constexpr int byte_size = 2;
    for (uint32_t i = 0; i < src_samples_num; ++i)
    {
        auto val = ((int)src_samples_num / 2 - i) * (1 << (byte_size * 8 - 1)) / src_samples_num;
        memcpy(src_data + i * byte_size, &val, byte_size);
    }

    constexpr auto src_total_size = src_samples_num * byte_size;


    auto time = time_us_32();

    constexpr uint8_t dst_bits = 16;

    const int src_part_size = src_total_size;
    const int dst_part_size = sizeof(dst_data);

    const processing::converter::config config = {
        .src_bits = byte_size * 8,
        .src_stride = byte_size,
        .src_freq = 1000,
        .dst_bits = dst_bits,
        .dst_stride = dst_bits / 8,
        .dst_freq = 2300,
        .channels = 1,
        .use_interp = use_interp
    };

    processing::converter converter;
    converter.setup(config);

    size_t dst_offset = 0;
    size_t src_offset = 0;
    while(src_offset < src_total_size)
    {
        auto result = converter.apply( 
            src_data + src_offset, src_data + src_offset + src_part_size, 
            dst_data + dst_offset, dst_data + dst_offset + dst_part_size);
        dst_offset += result.dst_advanced_bytes;
        src_offset += result.src_advanced_bytes;
    }

    dbg_printf("spend %s %u\n", use_interp ? "interp" : "instr", time_us_32() - time);
}

void test_mixer(uint8_t bits, bool use_interp)
{
    dbg_printf("test_mixer bits=%u interop=%s\n", bits, use_interp ? "on" : "off");

    static std::array<uint32_t, 64>  src1_array;
    static std::array<uint32_t, 64>  src2_array;

    processing::mixer::config mixer_config = {
        .bits = bits,
        .stride = sizeof(uint32_t),
        .channels = 1,
        .use_interp = use_interp
    };

    processing::mixer mixer;
    mixer.setup(mixer_config);

    const int32_t value_min = std::numeric_limits<int>::min()>>(32-bits);
    const int32_t value_max = std::numeric_limits<int>::max()>>(32-bits);
    const uint32_t value_range = 0xffffffffUL>>(32-bits);

    bool result;

    auto print_src = [&](const char* label){
        dbg_printf("%s: ", label);
        for(size_t i = 0; i < src2_array.size(); ++i)
        {
            int value = 0;
            auto p = (uint8_t*)&src2_array[i];
            switch(bits)
            {
                case 16: value = bytes_to_dword<16, true>(p); break;
                case 20: value = bytes_to_dword<20, true>(p); break;
                case 24: value = bytes_to_dword<24, true>(p); break;
                case 32: value = bytes_to_dword<32, true>(p); break;
            }
            dbg_printf("%d ", value);
        }
        dbg_printf("\n");
    };
    
    uint32_t value = value_min;
    for(size_t i = 0; i < src1_array.size(); ++i)
        src1_array[i] = value, value += (value_range/src1_array.size());
    src2_array.fill(0xcccccccc);

    mixer.apply(0x80, (uint8_t*)src1_array.begin(), (uint8_t*)src1_array.end(), (uint8_t*)src2_array.begin(), (uint8_t*)src2_array.end(), true);
    print_src("half");    

    src2_array.fill(value_max/2);
    mixer.apply(0xff, (uint8_t*)src1_array.begin(), (uint8_t*)src1_array.end(), (uint8_t*)src2_array.begin(), (uint8_t*)src2_array.end(), false);
    print_src("max_sat");

    src2_array.fill(value_min/2);
    mixer.apply(0xff, (uint8_t*)src1_array.begin(), (uint8_t*)src1_array.end(), (uint8_t*)src2_array.begin(), (uint8_t*)src2_array.end(), false);
    print_src("min_sat");

}


void measure_mixer(uint8_t bits, bool use_interp)
{
    dbg_printf("test_mixer bits=%u interop=%s\n", bits, use_interp ? "on" : "off");

    constexpr uint32_t samples = 1024;

    static std::array<uint32_t, samples>  src1_array;
    static std::array<uint32_t, samples>  src2_array;

    processing::mixer::config mixer_config = {
        .bits = bits,
        .stride = sizeof(uint32_t),
        .channels = 1,
        .use_interp = use_interp
    };

    processing::mixer mixer;
    mixer.setup(mixer_config);

    const int32_t value_min = std::numeric_limits<int>::min()>>(32-bits);
    const int32_t value_max = std::numeric_limits<int>::max()>>(32-bits);
    const uint32_t value_range = 0xffffffffUL>>(32-bits);

    uint32_t value = value_min;
    for(size_t i = 0; i < src1_array.size(); ++i)
        src1_array[i] = value, value += (value_range/src1_array.size());

    auto time = time_us_32();

    mixer.apply(0x80, (uint8_t*)src1_array.begin(), (uint8_t*)src1_array.end(), (uint8_t*)src2_array.begin(), (uint8_t*)src2_array.end(), true);

    mixer.apply(0xff, (uint8_t*)src1_array.begin(), (uint8_t*)src1_array.end(), (uint8_t*)src2_array.begin(), (uint8_t*)src2_array.end(), false);

    dbg_printf("spent %u\n", time_us_32() - time);
}

void test_start(void *)
{

#if 0
    while(true)
    {
        dbg_printf("upsampling \n");
        test_conversion(16, 1000, 16, 3333, true);
        test_conversion(20, 1000, 16, 3333, true);
        test_conversion(24, 1000, 16, 3333, true);
        test_conversion(32, 1000, 16, 3333, true);
        test_conversion(16, 1000, 16, 3333, false);
        test_conversion(20, 1000, 16, 3333, false);
        test_conversion(24, 1000, 16, 3333, false);
        test_conversion(32, 1000, 16, 3333, false);
        
        test_conversion(16, 1000, 20, 3333, true);
        test_conversion(20, 1000, 20, 3333, true);
        test_conversion(24, 1000, 20, 3333, true);
        test_conversion(32, 1000, 20, 3333, true);
        test_conversion(16, 1000, 20, 3333, false);
        test_conversion(20, 1000, 20, 3333, false);
        test_conversion(24, 1000, 20, 3333, false);
        test_conversion(32, 1000, 20, 3333, false);

        test_conversion(16, 1000, 24, 3333, true);
        test_conversion(20, 1000, 24, 3333, true);
        test_conversion(24, 1000, 24, 3333, true);
        test_conversion(24, 1000, 24, 3333, true);
        test_conversion(16, 1000, 24, 3333, false);
        test_conversion(20, 1000, 24, 3333, false);
        test_conversion(24, 1000, 24, 3333, false);
        test_conversion(24, 1000, 24, 3333, false);

        test_conversion(16, 1000, 32, 3333, true);        
        test_conversion(20, 1000, 32, 3333, true);
        test_conversion(24, 1000, 32, 3333, true);
        test_conversion(32, 1000, 32, 3333, true);
        test_conversion(16, 1000, 32, 3333, false);        
        test_conversion(20, 1000, 32, 3333, false);
        test_conversion(24, 1000, 32, 3333, false);
        test_conversion(32, 1000, 32, 3333, false);

        dbg_printf("downsampling \n");
        test_conversion(16, 1000, 16, 300, true);
        test_conversion(20, 1000, 16, 300, true);
        test_conversion(24, 1000, 16, 300, true);
        test_conversion(32, 1000, 16, 300, true);
        test_conversion(16, 1000, 16, 300, false);
        test_conversion(20, 1000, 16, 300, false);
        test_conversion(24, 1000, 16, 300, false);
        test_conversion(32, 1000, 16, 300, false);

        test_conversion(16, 1000, 20, 300, true);
        test_conversion(20, 1000, 20, 300, true);
        test_conversion(24, 1000, 20, 300, true);
        test_conversion(32, 1000, 20, 300, true);
        test_conversion(16, 1000, 20, 300, false);
        test_conversion(20, 1000, 20, 300, false);
        test_conversion(24, 1000, 20, 300, false);
        test_conversion(32, 1000, 20, 300, false);

        test_conversion(16, 1000, 24, 300, true);
        test_conversion(20, 1000, 24, 300, true);
        test_conversion(24, 1000, 24, 300, true);
        test_conversion(32, 1000, 24, 300, true);
        test_conversion(16, 1000, 24, 300, false);
        test_conversion(20, 1000, 24, 300, false);
        test_conversion(24, 1000, 24, 300, false);
        test_conversion(32, 1000, 24, 300, false);

        test_conversion(16, 1000, 32, 300, true);
        test_conversion(20, 1000, 32, 300, true);
        test_conversion(24, 1000, 32, 300, true);
        test_conversion(32, 1000, 32, 300, true);
        test_conversion(16, 1000, 32, 300, false);
        test_conversion(20, 1000, 32, 300, false);
        test_conversion(24, 1000, 32, 300, false);
        test_conversion(32, 1000, 32, 300, false);

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
#endif

    // bool interp = false;
    // while(true)
    // {
    //     measure_conversion(interp);
    //     interp = !interp;

    //     vTaskDelay(10);
    // }

#if 0
    while(true)
    {
        dbg_printf("start\n");
        test_mixer(16, true);
        test_mixer(16, false);
        test_mixer(20, true);
        test_mixer(20, false);
        test_mixer(24, true);
        test_mixer(24, false);
        test_mixer(32, true);
        test_mixer(32, false);

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
#endif

    while(true)
    {
        dbg_printf("start\n");

        measure_mixer(16, true);
        measure_mixer(20, true);
        measure_mixer(24, true);
        measure_mixer(32, true);

        measure_mixer(16, false);
        measure_mixer(20, false);
        measure_mixer(24, false);
        measure_mixer(32, false);
    }
    


    while (true)
        tight_loop_contents();
}

