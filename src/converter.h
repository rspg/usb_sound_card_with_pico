#pragma once

#include <stdint.h>
#include <hardware/interp.h>

namespace processing
{

class converter
{
public:
    struct apply_result
    {
        size_t src_advanced_bytes;
        size_t dst_advanced_bytes;
    };

    struct config
    {
        uint8_t src_bits;
        uint8_t src_stride;
        uint32_t src_freq;
        uint8_t dst_bits;
        uint8_t dst_stride;
        uint32_t dst_freq;
        uint8_t channels;
        bool    use_interp;
    };

    void setup(const config &);

    apply_result apply(const uint8_t *src_begin, const uint8_t *src_end, uint8_t *dst_begin, uint8_t *dst_end);
    uint32_t get_requirement_src_samples(uint32_t dst_samples) const;
    uint32_t get_requirement_src_bytes(uint32_t dst_bytes) const;

    const config& get_config() const { return m_config; }

private:

    using fn_sampling_t = apply_result(converter::*)(uint8_t ch, const uint8_t *src_begin, const uint8_t *src_end, uint8_t *dst_begin, uint8_t *dst_end);

    config m_config;
    interp_config m_lane0;
    interp_config m_lane1;
    uint32_t m_step;
    struct {
        uint32_t count;
        uint32_t base0;
        uint32_t base1;
    } m_ch_state[4];
    fn_sampling_t m_fn_sampling = nullptr;


    template<uint8_t DstBits, bool IsSrcStridePow2> 
        fn_sampling_t get_upsampling_method(const config& cfg);
    template<bool IsSrcStridePow2> 
        fn_sampling_t get_upsampling_method(const config& cfg);
    template<uint8_t SrcBits, uint8_t DstBits, bool IsSrcStridePow2> 
        apply_result upsampling(uint8_t ch, const uint8_t *src_begin, const uint8_t *src_end, uint8_t *dst_begin, uint8_t *dst_end);
    template<uint8_t SrcBits, uint8_t DstBits, bool IsSrcStridePow2> 
        apply_result upsampling_with_interp(uint8_t ch, const uint8_t *src_begin, const uint8_t *src_end, uint8_t *dst_begin, uint8_t *dst_end);

    fn_sampling_t get_downsampling_method(const config& cfg);
    template<uint8_t DstBits> 
        fn_sampling_t get_downsampling_method(const config& cfg);
    template<uint8_t SrcBits, uint8_t DstBits> 
        apply_result downsampling(uint8_t ch, const uint8_t *src_begin, const uint8_t *src_end, uint8_t *dst_begin, uint8_t *dst_end);
    template<uint8_t SrcBits, uint8_t DstBits> 
        apply_result downsampling_with_interp(uint8_t ch, const uint8_t *src_begin, const uint8_t *src_end, uint8_t *dst_begin, uint8_t *dst_end);
};

}