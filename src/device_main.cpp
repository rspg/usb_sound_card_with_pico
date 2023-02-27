
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <array>
#include <hardware/pll.h>
#include <hardware/clocks.h>
#include <hardware/structs/systick.h>
#include <pico/multicore.h>
#include <tusb.h>
#include "usb_descriptors.h"
#include "device_config.h"
#include "control_request.h"
#include "streaming.h"
#include "job_queue.h"
#include "debug.h"
#include "profiler.h"
#include "profile_measurement_list.h"

//--------------------------------------------------------------------+

#define DEVICE_LOG(...)   TU_LOG1("[DEVICE] " __VA_ARGS__);

#if PRINT_STATS
struct debug_stats
{
    uint32_t    fb_freq;
    uint32_t    fb;
};
#endif


// List of supported sample rates
constexpr uint32_t sample_rates[] = {48000, 96000};

// Resolution per format
constexpr uint8_t resolutions_per_format[2] = {16, 24};

// feedback descriptor bInterval value is supported only 1 on windows uac2 driver.
// the interval is measured manually in the sof callback.
constexpr uint16_t feedback_interval = 4000;

#define N_SAMPLE_RATES TU_ARRAY_SIZE(sample_rates)

std::array<uint32_t, UAC2_ENTITY_CLOCK_END - UAC2_ENTITY_CLOCK_START> g_current_sample_rates;
std::array<uint8_t, ITF_NUM_AUDIO_TOTAL> g_current_resolutions;


#if PRINT_STATS
debug_stats g_debug_stats;
#endif

void core1_loop();
void debug_cdc_job(job_queue::work*);
void tud_update_job(job_queue::work*);
extern "C" __attribute__ ((weak)) void tusb_pico_reserve_buffer(uint8_t ep_adr, uint16_t size);

/*------------- MAIN -------------*/
int main(void)
{
    std::fill(g_current_sample_rates.begin(), g_current_sample_rates.end(), 48000);
    std::fill(g_current_resolutions.begin(), g_current_resolutions.end(), 16);

    clock_configure(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    48 * MHZ,
                    48 * MHZ);

    // Turn off PLL sys for good measure

    const uint32_t vco_freq = 1248 * MHZ;
    const uint32_t div1 = 5;
    const uint32_t div2 = 2;

    pll_deinit(pll_sys);
    pll_init(pll_sys, 1, vco_freq, div1, div2);

    const uint32_t frequency  = vco_freq/div1/div2;
    clock_configure(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    frequency,
                    frequency);

    systick_hw->csr = 0b101;

    tusb_init();

    if(tusb_pico_reserve_buffer)
    {
#if USB_IF_AUDIO_ENABLE
        tusb_pico_reserve_buffer(EP_AUDIO_CONTROL, 256);
        tusb_pico_reserve_buffer(EP_AUDIO_CONTROL | 0x80, 256);
        tusb_pico_reserve_buffer(EP_AUDIO_STREAM, CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ);
        tusb_pico_reserve_buffer(EP_AUDIO_STREAM | 0x80, CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ);
        // tusb_pico_reserve_buffer(EP_AUDIO_STREAM_OUT_FB, 8);
        // tusb_pico_reserve_buffer(EP_AUDIO_STREAM_OUT_FB | 0x80, 8);
#endif
#if USB_IF_CONTROL_ENABLE
        tusb_pico_reserve_buffer(EP_AUDIO_USER_CONTROL, 256);
        tusb_pico_reserve_buffer(EP_AUDIO_USER_CONTROL | 0x80, 256);
#endif
#if USB_IF_DEBUG_CDC_ENABLE
        tusb_pico_reserve_buffer(EP_DEBUG_CDC_NOTIFY | 0x80, 16);
        tusb_pico_reserve_buffer(EP_DEBUG_CDC_DATA, 128);
        tusb_pico_reserve_buffer(EP_DEBUG_CDC_DATA | 0x80, 128);
#endif
    }
    
#if USB_IF_DEBUG_CDC_ENABLE
    dbg_print_init();
#endif

    job_queue::system::init();

    PROFILE_INITIALIZE(2, MAX_MEASUREMENT);
    PROFILE_CREATE_GROUP("core0");
    PROFILE_CREATE_GROUP("core1");

#if USB_IF_AUDIO_ENABLE
    streaming::init();
#endif

    const uint8_t core0mask = (1 << 0);
    const uint8_t core1mask = (1 << 1);

    static job_queue::work_fn usb_job;
    usb_job.set_affinity_mask(core0mask|core1mask);
    usb_job.set_callback(tud_update_job);
    usb_job.activate();
    usb_job.set_pending();

#if USB_IF_DEBUG_CDC_ENABLE
    static job_queue::work_fn debug_cdc;
    debug_cdc.set_affinity_mask(core0mask|core1mask);
    debug_cdc.set_callback(debug_cdc_job);
    debug_cdc.activate();
    debug_cdc.set_pending();
#endif

    multicore_launch_core1(core1_loop);
    while (true)
        job_queue::system::execute();

    return 0;
}

void core1_loop()
{
    while(true)
        job_queue::system::execute();
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
}

#if USB_IF_AUDIO_ENABLE

// Helper for clock get requests
static bool tud_audio_clock_get_request(uint8_t rhport, audio_control_request_t const *request)
{
    TU_ASSERT(IS_UNIT_TYPE(request->bEntityID, CLOCK));

    const int clock_index = request->bEntityID - UAC2_ENTITY_CLOCK_START;

    if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ)
    {
        const auto sample_rate = g_current_sample_rates[clock_index];

        if (request->bRequest == AUDIO_CS_REQ_CUR)
        {
            DEVICE_LOG("Clock %d get current freq %u\n", clock_index, g_current_sample_rates[clock_index]);

            audio_control_cur_4_t curf = {(int32_t)tu_htole32(sample_rate)};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
        }
        else if (request->bRequest == AUDIO_CS_REQ_RANGE)
        {
            audio_control_range_4_n_t(N_SAMPLE_RATES) rangef;
            rangef.wNumSubRanges = tu_htole16(N_SAMPLE_RATES);
            DEVICE_LOG("Clock %d get %d freq ranges\n", clock_index, N_SAMPLE_RATES);
            for (uint8_t i = 0; i < N_SAMPLE_RATES; i++)
            {
                rangef.subrange[i].bMin = sample_rates[i];
                rangef.subrange[i].bMax = sample_rates[i];
                rangef.subrange[i].bRes = 0;
                DEVICE_LOG("Range %d (%d, %d, %d)\n", i, (int)rangef.subrange[i].bMin, (int)rangef.subrange[i].bMax, (int)rangef.subrange[i].bRes);
            }

            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &rangef, sizeof(rangef));
        }
    }
    else if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID &&
             request->bRequest == AUDIO_CS_REQ_CUR)
    {
        audio_control_cur_1_t cur_valid = {.bCur = 1};
        DEVICE_LOG("Clock %d get is valid %u\n", clock_index, cur_valid.bCur);
        return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_valid, sizeof(cur_valid));
    }
    DEVICE_LOG("Clock %d get request not supported, entity = %u, selector = %u, request = %u\n",
            clock_index, request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
}

// Helper for clock set requests
static bool tud_audio_clock_set_request(uint8_t rhport, audio_control_request_t const *request, uint8_t const *buf)
{
    (void)rhport;

    TU_ASSERT(IS_UNIT_TYPE(request->bEntityID, CLOCK));
    TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);

    const int clock_index = request->bEntityID - UAC2_ENTITY_CLOCK_START;

    if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ)
    {
        TU_VERIFY(request->wLength == sizeof(audio_control_cur_4_t));

        g_current_sample_rates[clock_index] = (uint32_t)((audio_control_cur_4_t const *)buf)->bCur;

        DEVICE_LOG("Clock %d set current freq: %d\n", clock_index, g_current_sample_rates[clock_index]);

        return true;
    }
    else
    {
        DEVICE_LOG("Clock %d set request not supported, entity = %u, selector = %u, request = %u\n",
                clock_index, request->bEntityID, request->bControlSelector, request->bRequest);
        return false;
    }
}

//--------------------------------------------------------------------+
// Application Callback API Implementations
//--------------------------------------------------------------------+

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;

    if (IS_UNIT_TYPE(request->bEntityID, CLOCK))
        return tud_audio_clock_get_request(rhport, request);

    DEVICE_LOG("Get request not handled, entity = %d, selector = %d, request = %d\n",
            request->bEntityID, request->bControlSelector, request->bRequest);

    return false;
}

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf)
{
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;

    if (IS_UNIT_TYPE(request->bEntityID, CLOCK))
        return tud_audio_clock_set_request(rhport, request, buf);

    DEVICE_LOG("Set request not handled, entity = %d, selector = %d, request = %d\n",
            request->bEntityID, request->bControlSelector, request->bRequest);

    return false;
}

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;
    (void)p_request;

    uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
    uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

    DEVICE_LOG("Close interface itf %d alt %d\n", itf, alt);

    return true;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;

    uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
    uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

    DEVICE_LOG("Set interface %d alt %d\n", itf, alt);

    // Clear buffer when streaming format is changed
    // spk_data_size = 0;
    if(alt > 0)
        g_current_resolutions[itf] = resolutions_per_format[(alt - 1)&1];

    switch(itf)
    {
        case ITF_NUM_AUDIO_STREAMING_HOST_TX:
            if(alt)
            {
                streaming::set_rx_format(
                    g_current_sample_rates[UAC2_ENTITY_USB_INPUT_CLOCK - UAC2_ENTITY_CLOCK_START], 
                    g_current_resolutions[ITF_NUM_AUDIO_STREAMING_HOST_TX]);
            }
            else
            {
                streaming::close_rx();
            }
            break;
        case ITF_NUM_AUDIO_STREAMING_HOST_RX:
            if(alt)
            {
                streaming::set_tx_format(
                    g_current_sample_rates[UAC2_ENTITY_LINEIN_CLOCK - UAC2_ENTITY_CLOCK_START],
                    g_current_resolutions[ITF_NUM_AUDIO_STREAMING_HOST_RX]);
            }
            else
            {
                streaming::close_tx();
            }
            break;
    }

    return true;
}

bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    return false;
}

bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff)
{
    return false;
}

bool tud_audio_get_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    return false;
}

bool tud_audio_set_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff)
{
    return false;
}

bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting)
{
    streaming::push_rx_data(
        +[](uint8_t *buffer, size_t size) -> size_t
        {
            return tud_audio_read(buffer, (uint16_t)size);
        },
        n_bytes_received);

    return true;
}

bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
{
    auto sz = streaming::pop_tx_data(+[](const uint8_t *begin, const uint8_t *end) -> size_t
    {
        tud_audio_write(begin, (uint16_t)(end - begin));
        return (end - begin);
    });

    return true;
}

TU_ATTR_FAST_FUNC void tud_audio_feedback_interval_isr(uint8_t func_id, uint32_t frame_number, uint8_t interval_shift)
{
    uint32_t cycles = 0xffffff - systick_hw->cvr;
    systick_hw->cvr = 0xffffff;

    uint32_t feedback = tud_audio_feedback_update(0, cycles);
#if PRINT_STATS
    g_debug_stats.fb_freq = cycles;
    g_debug_stats.fb = feedback;
#endif
}

#endif

#if USB_IF_CONTROL_ENABLE

bool device_control_request(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);

// Invoked when a control transfer occurred on an interface of this class
// Driver response accordingly to the request and the transfer stage (setup/data/ack)
// return false to stall control endpoint (e.g unsupported request)
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    // DEVICE_LOG("ctrl type:%x req:%x idx:%x val:%x len:%d\n", request->bmRequestType_bit.type, request->bRequest, request->wIndex, request->wValue, request->wLength);

    switch (request->bmRequestType_bit.type)
    {
        case TUSB_REQ_TYPE_VENDOR:
            switch (request->bRequest)
            {
                case VENDOR_REQUEST_MICROSOFT:
                    if (request->wIndex == 7)
                    {
                        if (stage == CONTROL_STAGE_SETUP)
                        {
                            // Get Microsoft OS 2.0 compatible descriptor
                            uint16_t total_len;
                            memcpy(&total_len, get_msos2_descriptor() + 8, 2);

                            return tud_control_xfer(rhport, request, (void *)(uintptr_t)get_msos2_descriptor(), total_len);
                        }
                    }
                    else
                    {
                        return false;
                    }
                    break;
            
                case VENDOR_REQUEST_CONTROLLER:
                    return device_control_request(rhport, stage, request);

                default:
                    return false;
            }
            break;        

        default: 
            return false;
    }

    return true;
}

bool device_control_request(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    static uint8_t data;

    switch(request->wIndex)
    {
        case CONTROL_SPDIF_IN_SET_VOLUME:
            if (stage == CONTROL_STAGE_SETUP)
            {
                DEVICE_LOG("vendor spdif in set volume\n");
                return tud_control_xfer(rhport, request, &data, sizeof(uint8_t));
            }
            else if (stage == CONTROL_STAGE_DATA)
            {
                DEVICE_LOG("value %d\n", data);
                streaming::set_spdif_in_volume(data);
            }
            break;
        case CONTROL_SPDIF_IN_GET_VOLUME:
            if (stage == CONTROL_STAGE_SETUP)
            {
                DEVICE_LOG("vendor spdif in get volume\n");
                data = streaming::get_spdif_in_volume();
                return tud_control_xfer(rhport, request, &data, sizeof(uint8_t));
            }
            break;
        case CONTROL_LINE_IN_SET_VOLUME:
            if (stage == CONTROL_STAGE_SETUP)
            {
                DEVICE_LOG("vendor line in set volume\n");
                return tud_control_xfer(rhport, request, &data, sizeof(uint8_t));
            }
            else if (stage == CONTROL_STAGE_DATA)
            {
                DEVICE_LOG("value %d\n", data);
                streaming::set_line_in_volume(data);
            }
            break;
        case CONTROL_LINE_IN_GET_VOLUME:
            if (stage == CONTROL_STAGE_SETUP)
            {
                DEVICE_LOG("vendor line in get volume\n");
                data = streaming::get_line_in_volume();
                return tud_control_xfer(rhport, request, &data, sizeof(uint8_t));
            }
            break;

        default:
            return false;
    }
    
    return true;
}

#endif

//--------------------------------------------------------------------+
// AUDIO Task
//--------------------------------------------------------------------+

void tud_update_job(job_queue::work *job)
{
    PROFILE_MEASURE_BEGIN(PERF_TUD_TASK);
    tud_task(); // tinyusb device task
    PROFILE_MEASURE_END();

    job->set_pending();
}

#if PRINT_STATS
void print_debug_stats()
{
    dbg_printf("stats:\n");
    dbg_printf("  time: %f\n", time_us_64()/1000000.);
    dbg_printf("  fb: %u  freq: %u\n", g_debug_stats.fb, g_debug_stats.fb_freq);
    streaming::print_debug_stats();
}
#endif

#if USB_IF_DEBUG_CDC_ENABLE

void debug_cdc_job(job_queue::work *job)
{
    static bool stats_on = false;
    static uint32_t stats_timer = 0;

    if(tud_cdc_available())
    {
        char line_buf[64];

        const auto sz = tud_cdc_read(line_buf, std::size(line_buf));
        line_buf[sz] = '\0';

        if(strcmp(line_buf, "perf") == 0)
        {
            PROFILE_PRINT();
        }
        else if(strcmp(line_buf, "reset") == 0)
        {
            PROFILE_RESET();
        }
        else if(strstr(line_buf, "stats") == line_buf)
        {
            stats_on = strstr(line_buf, "on") ? true : false;
        }
    }

#if PRINT_STATS
    if(stats_on && time_us_32() > stats_timer)
    {
        print_debug_stats();
        stats_timer = time_us_32() + 500000;
    }
#endif

    PROFILE_MEASURE_BEGIN(PERF_CDC_FLUSH);
    dbg_flush_cdc();
    PROFILE_MEASURE_END();

    job->set_pending_delay_us(1000);
}

#endif