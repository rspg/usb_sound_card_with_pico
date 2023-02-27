#include <hardware/interp.h>
#include <hardware/sync.h>
#include <hardware/divider.h>
#include <hardware/timer.h>
#include <pico/platform.h>
#include <cstring>
#include <type_traits>
#include <algorithm>
#include "support.h"
#include "debug.h"
#include "converter.h"
#include "profiler.h"
#include "profile_measurement_list.h"

namespace processing
{
    using namespace support;

    constexpr uint32_t count_flags_mask = 0xf0000000;

    template<uint8_t SrcBit, uint8_t DstBit, bool Signed> inline uint32_t bit_convert(uint32_t value_u32)
    {
        using value_t = typename std::conditional<Signed, int32_t, uint32_t>::type;

        const auto value = (value_t)value_u32;

        if constexpr (SrcBit < DstBit)
            return value << (DstBit - SrcBit);
        else
            return value >> (SrcBit - DstBit);
    }

    template<uint8_t SrcBits, uint8_t DstBits> 
    converter::apply_result converter::downsampling_with_interp(uint8_t ch, const uint8_t *src_begin, const uint8_t *src_end, uint8_t *dst_begin, uint8_t *dst_end)
    {
        constexpr uint32_t sample_continue_flag = 0x80000000;
        constexpr uint32_t lack_first_sample_flag = 0x40000000;
        
        PROFILE_MEASURE_BEGIN(PROF_DWSMP_IP_SETUP);

        const auto step = m_step;
        const auto src_stride = m_config.src_stride*m_config.channels;
        const auto dst_stride = m_config.dst_stride*m_config.channels;

        auto src = src_begin;
        auto dst = dst_begin;

        auto update_src_addr = [&](){ src = src_begin + interp0->peek[2]*src_stride; };

        interp0->base[0] = m_ch_state[ch].base0;
        interp0->base[1] = m_ch_state[ch].base1;
        interp0->base[2] = 0;
        interp0->accum[0] = m_ch_state[ch].count&~count_flags_mask;
        if((m_ch_state[ch].count&sample_continue_flag) == 0)
        {
            if((m_ch_state[ch].count&lack_first_sample_flag) == 0)
            {
                interp0->base[0] = bytes_to_dword<SrcBits, true>(src);
                interp0->add_raw[0] = step & ~0xff;
            }
            update_src_addr();
            if(src >= src_end)
            {
                m_ch_state[ch].base0 = interp0->base[0];
                m_ch_state[ch].count = ((src - src_end)/src_stride << 8) | lack_first_sample_flag;
                return { (size_t)(src_end - src_begin), 0 };
            }
            interp0->base[1] = bytes_to_dword<SrcBits, true>(src);
            interp0->add_raw[0] = step;
        }

        PROFILE_MEASURE_END();

        PROFILE_MEASURE_BEGIN(PROF_DWSMP_IP_LOOP);
        
        update_src_addr();
        while(src < src_end && dst < dst_end)
        {
            copy_dword<DstBits>(dst, bit_convert<SrcBits, DstBits, true>(interp0->peek[1]));
            dst += dst_stride;

            interp0->base[0] = interp0->base[1];
            interp0->base[1] = bytes_to_dword<SrcBits, true>(src);    

            interp0->add_raw[0] = step;
            update_src_addr();
        }

        PROFILE_MEASURE_END();

        PROFILE_MEASURE_BEGIN(PROF_DWSMP_IP_SAVE);

        m_ch_state[ch].base0 = interp0->base[0];
        m_ch_state[ch].base1 = interp0->base[1];
        m_ch_state[ch].count = interp0->peek[0] | sample_continue_flag;
        if(src >= src_end)
        {
            m_ch_state[ch].count |= ((src - src_end)/src_stride << 8);
            src = src_end;
        }

        PROFILE_MEASURE_END();

        return { (size_t)(src - src_begin), (size_t)(dst - dst_begin) };
    }

    template<uint8_t SrcBits, uint8_t DstBits> 
    converter::apply_result converter::downsampling(uint8_t ch, const uint8_t *src_begin, const uint8_t *src_end, uint8_t *dst_begin, uint8_t *dst_end)
    {
        constexpr uint32_t sample_continue_flag = 0x80000000;
        constexpr uint32_t lack_first_sample_flag = 0x40000000;
        
        PROFILE_MEASURE_BEGIN(PROF_DWSMP_SETUP);

        const auto step = m_step;
        const auto src_stride = m_config.src_stride*m_config.channels;
        const auto dst_stride = m_config.dst_stride*m_config.channels;

        auto src = src_begin;
        auto dst = dst_begin;

        uint32_t base0 = m_ch_state[ch].base0;
        uint32_t base1 = m_ch_state[ch].base1;
        uint32_t total_steps = m_ch_state[ch].count&~count_flags_mask;

        auto update_src_addr = [&](){ src = src_begin + (total_steps>>8)*src_stride; };

        if((m_ch_state[ch].count&sample_continue_flag) == 0)
        {
            if((m_ch_state[ch].count&lack_first_sample_flag) == 0)
            {
                base0 = bytes_to_dword<SrcBits, true>(src);
                total_steps += step & ~0xff;
            }
            update_src_addr();
            if(src >= src_end)
            {
                m_ch_state[ch].base0 = base0;
                m_ch_state[ch].count = ((src - src_end)/src_stride << 8) | lack_first_sample_flag;
                return { (size_t)(src_end - src_begin), 0 };
            }
            base1 = bytes_to_dword<SrcBits, true>(src);
            total_steps += step;
        }

        PROFILE_MEASURE_END();

        PROFILE_MEASURE_BEGIN(PROF_DWSMP_LOOP);
        
        update_src_addr();
        while(src < src_end && dst < dst_end)
        {
            const uint32_t value = blend_value<SrcBits, true>(base0, base1, total_steps&0xff);
            copy_dword<DstBits>(dst, bit_convert<SrcBits, DstBits, true>(value));
            dst += dst_stride;

            base0 = base1;
            base1 = bytes_to_dword<SrcBits, true>(src);    

            total_steps += step;
            update_src_addr();
        }

        PROFILE_MEASURE_END();

        PROFILE_MEASURE_BEGIN(PROF_DWSMP_SAVE);

        m_ch_state[ch].base0 = base0;
        m_ch_state[ch].base1 = base1;
        m_ch_state[ch].count = (total_steps&0xff) | sample_continue_flag;
        if(src >= src_end)
        {
            m_ch_state[ch].count |= ((src - src_end)/src_stride << 8);
            src = src_end;
        }

        PROFILE_MEASURE_END();

        return { (size_t)(src - src_begin), (size_t)(dst - dst_begin) };
    }

    template<uint8_t DstBits> converter::fn_sampling_t converter::get_downsampling_method(const config& cfg)
    {
        if(cfg.use_interp)
        {
            switch(cfg.src_bits)
            {
                case 16: return &converter::downsampling_with_interp<16, DstBits>;
                case 20: return &converter::downsampling_with_interp<20, DstBits>;
                case 24: return &converter::downsampling_with_interp<24, DstBits>;
                case 32: return &converter::downsampling_with_interp<32, DstBits>;
                default:
                    dbg_assert(false && "unsupproted bits");
            }
        }
        else
        {
            switch(cfg.src_bits)
            {
                case 16: return &converter::downsampling<16, DstBits>;
                case 20: return &converter::downsampling<20, DstBits>;
                case 24: return &converter::downsampling<24, DstBits>;
                case 32: return &converter::downsampling<32, DstBits>;
                default:
                    dbg_assert(false && "unsupproted bits");
            }
        }
        return nullptr;
    }

    converter::fn_sampling_t converter::get_downsampling_method(const config& cfg)
    {
        switch(cfg.dst_bits)
        {
            case 16: return get_downsampling_method<16>(cfg);
            case 20: return get_downsampling_method<20>(cfg);
            case 24: return get_downsampling_method<24>(cfg);
            case 32: return get_downsampling_method<32>(cfg);
            default:
                dbg_assert(false && "unsupproted bits");
        }
        return nullptr;
    }


    template<uint8_t SrcBits, uint8_t DstBits, bool IsSrcStridePow2> 
    converter::apply_result converter::upsampling_with_interp(uint8_t ch, const uint8_t *src_begin, const uint8_t *src_end, uint8_t *dst_begin, uint8_t *dst_end)
    {
        constexpr uint32_t sample_continue_flag = 0x80000000;
        constexpr uint32_t lack_first_sample_flag = 0x40000000;

        PROFILE_MEASURE_BEGIN(PROF_UPSMP_IP_SETUP);

        const auto step = m_step;
        const auto src_stride = m_config.src_stride*m_config.channels;
        const auto dst_stride = m_config.dst_stride*m_config.channels;

        auto src = src_begin;
        auto dst = dst_begin;

        interp0->base[0] = m_ch_state[ch].base0;
        interp0->base[1] = m_ch_state[ch].base1;
        if((m_ch_state[ch].count&sample_continue_flag) == 0)
        {
            if((m_ch_state[ch].count&lack_first_sample_flag) == 0)
            {
                // first set samples to bases
                interp0->base[0] = bytes_to_dword<SrcBits, true>(src);
                src += src_stride;
                if(src >= src_end)
                {
                    m_ch_state[ch].base0 = interp0->base[0];
                    m_ch_state[ch].count = lack_first_sample_flag;
                    return { (size_t)src_stride, 0 };
                }
            }
            interp0->base[1] = bytes_to_dword<SrcBits, true>(src);
            src += src_stride;
        }
        interp0->base[2] = (uintptr_t)src;
        interp0->accum[0] =  m_ch_state[ch].count&~count_flags_mask;

        PROFILE_MEASURE_END();

        PROFILE_MEASURE_BEGIN(PROF_UPSMP_IP_LOOP);

        while(src < src_end && dst < dst_end)
        {
            while(src == (uint8_t*)interp0->peek[2] && dst < dst_end)
             {
                auto srcval = bit_convert<SrcBits, DstBits, true>(interp0->peek[1]);
                copy_dword<DstBits>(dst, srcval);
                
                dst += dst_stride;
                interp0->add_raw[0] = step;
            } 

            if(src != (uint8_t*)interp0->peek[2])
            {
                interp0->base[0] = interp0->base[1];
                interp0->base[1] = bytes_to_dword<SrcBits, true>(src);

                // When stride size is not 2^n, interp can not caclulates address completely.
                // add insufficient bytes to base address for fill up.
                if constexpr (!IsSrcStridePow2) 
                    interp0->base[2] += src_stride - 1;
                src = (uint8_t*)interp0->peek[2];
            }
        }

        PROFILE_MEASURE_END();

        PROFILE_MEASURE_BEGIN(PROF_UPSMP_IP_SAVE);

        m_ch_state[ch].base0 = interp0->base[0];
        m_ch_state[ch].base1 = interp0->base[1];
        m_ch_state[ch].count = interp0->peek[0] | sample_continue_flag;

        PROFILE_MEASURE_END();

        return { (size_t)(src - src_begin), (size_t)(dst - dst_begin) };
    }

    template<uint8_t SrcBits, uint8_t DstBits, bool IsSrcStridePow2> 
    converter::apply_result converter::upsampling(uint8_t ch, const uint8_t *src_begin, const uint8_t *src_end, uint8_t *dst_begin, uint8_t *dst_end)
    {
        constexpr uint32_t sample_continue_flag = 0x80000000;
        constexpr uint32_t lack_first_sample_flag = 0x40000000;

        PROFILE_MEASURE_BEGIN(PROF_UPSMP_SETUP);

        const auto step = m_step;
        const auto src_stride = m_config.src_stride*m_config.channels;
        const auto dst_stride = m_config.dst_stride*m_config.channels;

        auto src = src_begin;
        auto dst = dst_begin;

        uint32_t base0 = m_ch_state[ch].base0;
        uint32_t base1 = m_ch_state[ch].base1;
        if((m_ch_state[ch].count&sample_continue_flag) == 0)
        {
            if((m_ch_state[ch].count&lack_first_sample_flag) == 0)
            {
                // first set samples to bases
                base0 = bytes_to_dword<SrcBits, true>(src);
                src += src_stride;
                if(src >= src_end)
                {
                    m_ch_state[ch].base0 = base0;
                    m_ch_state[ch].count = lack_first_sample_flag;
                    return { (size_t)src_stride, 0 };
                }
            }
            base1 = bytes_to_dword<SrcBits, true>(src);
            src += src_stride;
        }
        
        uint32_t total_steps = m_ch_state[ch].count&~count_flags_mask;

        PROFILE_MEASURE_END();

        PROFILE_MEASURE_BEGIN(PROF_UPSMP_LOOP);

        while(src < src_end && dst < dst_end)
        {
            while((total_steps>>8) == 0 && dst < dst_end)
             {
                const uint32_t value = blend_value<SrcBits, true>(base0, base1, total_steps&0xff);
                auto srcval = bit_convert<SrcBits, DstBits, true>(value);
                copy_dword<DstBits>(dst, srcval);
                
                dst += dst_stride;
                total_steps += step;
            } 

            if(total_steps>>8)
            {
                base0 = base1;
                base1 = bytes_to_dword<SrcBits, true>(src);
                src += src_stride;
                total_steps &= 0xff;
            }
        }

        PROFILE_MEASURE_END();

        PROFILE_MEASURE_BEGIN(PROF_UPSMP_SAVE);

        m_ch_state[ch].base0 = base0;
        m_ch_state[ch].base1 = base1;
        m_ch_state[ch].count = total_steps | sample_continue_flag;

        PROFILE_MEASURE_END();

        return { (size_t)(src - src_begin), (size_t)(dst - dst_begin) };
    }

    template<uint8_t DstBits, bool IsSrcStridePow2> 
    converter::fn_sampling_t converter::get_upsampling_method(const config& cfg)
    {
        if(cfg.use_interp)
        {
            switch(cfg.src_bits)
            {
                case 16: return &converter::upsampling_with_interp<16, DstBits, IsSrcStridePow2>;
                case 20: return &converter::upsampling_with_interp<20, DstBits, IsSrcStridePow2>;
                case 24: return &converter::upsampling_with_interp<24, DstBits, IsSrcStridePow2>;
                case 32: return &converter::upsampling_with_interp<32, DstBits, IsSrcStridePow2>;
                default:
                    dbg_assert(false && "not supproted bits");
            }
        }
        else
        {
            switch(cfg.src_bits)
            {
                case 16: return &converter::upsampling<16, DstBits, IsSrcStridePow2>;
                case 20: return &converter::upsampling<20, DstBits, IsSrcStridePow2>;
                case 24: return &converter::upsampling<24, DstBits, IsSrcStridePow2>;
                case 32: return &converter::upsampling<32, DstBits, IsSrcStridePow2>;
                default:
                    dbg_assert(false && "not supproted bits");
            }
        }
        return nullptr;
    }

    template<bool IsSrcStridePow2>
    converter::fn_sampling_t converter::get_upsampling_method(const config& cfg)
    {
        switch(cfg.dst_bits)
        {
            case 16: return get_upsampling_method<16, IsSrcStridePow2>(cfg);
            case 20: return get_upsampling_method<20, IsSrcStridePow2>(cfg);
            case 24: return get_upsampling_method<24, IsSrcStridePow2>(cfg);
            case 32: return get_upsampling_method<32, IsSrcStridePow2>(cfg);
            default:
                dbg_assert(false && "not supproted bits");
        }
        return nullptr;
    }
    
    void converter::setup(const config& cfg)
    {
        m_config = cfg;
        m_step = cfg.src_freq*256/cfg.dst_freq;

        const auto src_stride = cfg.src_stride*cfg.channels;
        if(m_config.use_interp)
        {
            m_lane0 = interp_default_config();
            if(cfg.src_freq < cfg.dst_freq)
            {
                // upsampling
                const auto fsb = is_power2(src_stride) ? __builtin_ctz((uint32_t)src_stride) : 0;
                interp_config_set_shift(&m_lane0, 8 - fsb);
                interp_config_set_mask(&m_lane0, fsb, 31);
            }
            else
            {
                // downsampling
                interp_config_set_shift(&m_lane0, 8);
                //interp_config_set_mask(&m_lane0, 0, 31);
            }
            interp_config_set_blend(&m_lane0, true);

            m_lane1 = interp_default_config();
            interp_config_set_signed(&m_lane1, true);
            interp_config_set_cross_input(&m_lane1, true); // signed blending
        }

        std::memset(m_ch_state, 0, sizeof(m_ch_state));

        if(m_step > 0xff)
        {
            m_fn_sampling = get_downsampling_method(m_config);
        }
        else
        {
            if(is_power2(src_stride))
                m_fn_sampling = get_upsampling_method<true>(m_config);
            else
                m_fn_sampling = get_upsampling_method<false>(m_config);
        }
    }

    converter::apply_result converter::apply(const uint8_t *src_begin, const uint8_t *src_end, uint8_t *dst_begin, uint8_t *dst_end)
    {
        const auto src_stride = m_config.src_stride*m_config.channels;
        const auto dst_stride = m_config.dst_stride*m_config.channels;

        if(src_begin > src_end || src_end - src_begin < src_stride)
            return { 0, 0 };
        if(dst_begin > dst_end || dst_end - dst_begin < dst_stride)
            return { 0, 0 };

        dst_end = dst_begin + (dst_end - dst_begin)/dst_stride*dst_stride;
        src_end = src_begin + (src_end - src_begin)/src_stride*src_stride;
        
        if(m_config.use_interp)
        {
            interp_set_config(interp0, 0, &m_lane0);
            interp_set_config(interp0, 1, &m_lane1);
        }

        apply_result total_result = { 0, 0 };
        for(int i = 0; i < m_config.channels; ++i)
        {
            auto result = (this->*m_fn_sampling)(i, src_begin + i*m_config.src_stride, src_end, dst_begin + i*m_config.dst_stride, dst_end);

            total_result.dst_advanced_bytes = std::max(total_result.dst_advanced_bytes, result.dst_advanced_bytes);
            total_result.src_advanced_bytes = std::max(total_result.src_advanced_bytes, result.src_advanced_bytes);
        } 

        return total_result;
    }

    uint32_t converter::get_requirement_src_samples(uint32_t dst_samples) const
    {
        if(dst_samples == 0)
            return 0;
        return (((dst_samples - 1) * m_step) >> 8) + 2;
    }

    uint32_t converter::get_requirement_src_bytes(uint32_t dst_bytes) const
    {
        return get_requirement_src_samples(dst_bytes/m_config.dst_stride)*m_config.src_stride;
    }
}