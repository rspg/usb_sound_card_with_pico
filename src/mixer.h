#pragma once

#include <stdint.h>
#include <hardware/interp.h>

namespace processing
{

class mixer
{
public:
    struct apply_result
    {
        size_t src_advanced_bytes;
        size_t dst_advanced_bytes;
    };

    struct config
    {
        uint8_t bits;
        uint8_t stride;
        uint8_t channels;
        bool use_interp;
    };

    void setup(const config&);
    apply_result apply(uint8_t volume, const uint8_t* src_begin, const uint8_t* src_end, uint8_t* dst_begin, uint8_t* dst_end, bool overwrite);

private:
    using fn_combine_t = apply_result(mixer::*)(uint8_t volume, const uint8_t* src_begin, const uint8_t* src_end, uint8_t* dst_begin, uint8_t* dst_end);

    config m_config;
    interp_config m_lane0;
    interp_config m_lane1;
    interp_config m_lane2;
    //interp_config lane3;
    fn_combine_t m_fn_combine;
    fn_combine_t m_fn_combine_ow;

    template<uint8_t Bits, bool Overwrite> 
        apply_result combine_with_interp(uint8_t volume, const uint8_t* src_begin, const uint8_t* src_end, uint8_t* dst_begin, uint8_t* dst_end);
    template<uint8_t Bits, bool Overwrite> 
        apply_result combine(uint8_t volume, const uint8_t* src_begin, const uint8_t* src_end, uint8_t* dst_begin, uint8_t* dst_end);
    template<bool Overwrite>
        fn_combine_t get_combine_method(const config& cfg);
};

}