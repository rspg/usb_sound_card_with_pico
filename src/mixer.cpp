#include <hardware/interp.h>
#include <pico/platform.h>
#include <cstring>
#include <type_traits>
#include <algorithm>
#include "support.h"
#include "debug.h"
#include "mixer.h"

namespace processing
{
    using namespace support;


    template<uint8_t Bits, bool Overwrite> mixer::apply_result mixer::combine_with_interp(uint8_t volume, const uint8_t* src_begin, const uint8_t* src_end, uint8_t* dst_begin, uint8_t* dst_end)
    {
        interp_set_config(interp0, 0, &m_lane0);
        interp_set_config(interp0, 1, &m_lane1);
        interp_set_config(interp1, 0, &m_lane2);

        interp0->accum[1] = volume;
        interp0->base[0] = 0;

        interp1->base[0] = ~(((uint32_t)1<<(Bits - 1)) - 1);
        interp1->base[1] = ((uint32_t)1<<(Bits - 1)) - 1;

        auto src = src_begin;
        auto dst = dst_begin;
        auto stride = m_config.stride;

        while(src < src_end && dst < dst_end)
        {
            interp0->base[1] = bytes_to_dword<Bits, true>(src);
            if constexpr (Overwrite)
            {
                copy_dword<Bits>(dst, interp0->peek[1]);
            }
            else
            {
                if constexpr (Bits == 32) 
                {
                    auto val0 = interp0->peek[1];
                    auto val1 = bytes_to_dword<Bits, true>(dst);
                    
                    uint32_t sum;
                    if(__builtin_sadd_overflow(val0, val1, (int*)&sum))
                    {
                        if(val0&0x80000000)
                            sum = 0x80000000;
                        else
                            sum = 0x7fffffff;
                    }
                    copy_dword<Bits>(dst, sum);
                }
                else
                {
                    interp1->accum[0] = interp0->peek[1];
                    interp1->add_raw[0] = bytes_to_dword<Bits, true>(dst);
                    copy_dword<Bits>(dst, interp1->peek[0]);
                }
            }
            src += stride;
            dst += stride;
        }

        return { (size_t)(src - src_begin), (size_t)(dst - dst_begin) };
    }

    template<uint8_t Bits, bool Overwrite> mixer::apply_result mixer::combine(uint8_t volume, const uint8_t* src_begin, const uint8_t* src_end, uint8_t* dst_begin, uint8_t* dst_end)
    {
        interp0->accum[1] = volume;
        interp0->base[0] = 0;

        interp1->base[0] = ~(((uint32_t)1<<(Bits - 1)) - 1);
        interp1->base[1] = ((uint32_t)1<<(Bits - 1)) - 1;

        auto src = src_begin;
        auto dst = dst_begin;
        auto stride = m_config.stride;

        while(src < src_end && dst < dst_end)
        {
            const uint32_t src_value = blend_value<Bits, true>(0, bytes_to_dword<Bits, true>(src), volume);
            if constexpr (Overwrite)
            {
                copy_dword<Bits>(dst, src_value);
            }
            else
            {
                const uint32_t dst_value = bytes_to_dword<Bits, true>(dst);
                
                uint32_t mixed;
                if constexpr (Bits == 32) 
                {
                    if(__builtin_sadd_overflow(src_value, dst_value, (int*)&mixed))
                        mixed = (src_value&0x80000000) ? 0x80000000 : 0x7fffffff;
                }
                else
                {
                    constexpr int32_t min_value = (int32_t)(0xffffffff << (Bits - 1));
                    constexpr int32_t max_value = (int32_t)((1 << (Bits - 1)) - 1);

                    mixed = src_value + dst_value;
                    if((int32_t)mixed > max_value)
                        mixed = max_value;
                    else if((int32_t)mixed < min_value)
                        mixed = min_value;
                }
                copy_dword<Bits>(dst, mixed);
            }
            src += stride;
            dst += stride;
        }

        return { (size_t)(src - src_begin), (size_t)(dst - dst_begin) };
    }

    template<bool Overwrite>
    mixer::fn_combine_t mixer::get_combine_method(const config& cfg)
    {
        if(cfg.use_interp)
        {
            switch(cfg.bits)
            {
                case 16: return &mixer::combine_with_interp<16, Overwrite>;
                case 20: return &mixer::combine_with_interp<20, Overwrite>;
                case 24: return &mixer::combine_with_interp<24, Overwrite>;
                case 32: return &mixer::combine_with_interp<32, Overwrite>;
                default:
                    dbg_assert(false && "unsupproted bits");
            }
        }
        else
        {
            switch(cfg.bits)
            {
                case 16: return &mixer::combine<16, Overwrite>;
                case 20: return &mixer::combine<20, Overwrite>;
                case 24: return &mixer::combine<24, Overwrite>;
                case 32: return &mixer::combine<32, Overwrite>;
                default:
                    dbg_assert(false && "unsupproted bits");
            }
        }
        return nullptr;
    }

    
    void mixer::setup(const config& cfg)
    {
        m_config = cfg;
        
        if(cfg.use_interp)
        {
            m_lane0 =  interp_default_config();
            interp_config_set_blend(&m_lane0, true);

            m_lane1 = interp_default_config();
            interp_config_set_signed(&m_lane1, true);

            m_lane2 = interp_default_config();
            interp_config_set_clamp(&m_lane2, true);
            interp_config_set_signed(&m_lane2, true);
            interp_config_set_add_raw(&m_lane2, true);
        }

        //m_lane3 = interp_default_config();
        m_fn_combine = get_combine_method<false>(m_config);
        m_fn_combine_ow = get_combine_method<true>(m_config);
    }

    mixer::apply_result mixer::apply(uint8_t volume, const uint8_t* src_begin, const uint8_t* src_end, uint8_t* dst_begin, uint8_t* dst_end, bool overwrite)
    {
        const auto stride = m_config.stride*m_config.channels;

        if(src_begin > src_end || src_end - src_begin < stride)
            return { 0, 0 };
        if(dst_begin > dst_end || dst_end - dst_begin < stride)
            return { 0, 0 };

        dst_end = dst_begin + (dst_end - dst_begin)/stride*stride;
        src_end = src_begin + (src_end - src_begin)/stride*stride;

        return overwrite
            ? (this->*m_fn_combine_ow)(volume, src_begin, src_end, dst_begin, dst_end)
            : (this->*m_fn_combine)(volume, src_begin, src_end, dst_begin, dst_end);
    }
}