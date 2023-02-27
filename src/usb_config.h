#pragma once

#ifdef __cplusplus
extern "C"{
#endif

#define USB_IF_AUDIO_ENABLE     1
#define USB_IF_CONTROL_ENABLE   1
#if (CFG_TUSB_DEBUG > 0) || PROFILE || TRACE
    #define USB_IF_DEBUG_CDC_ENABLE 1
#else
    #define USB_IF_DEBUG_CDC_ENABLE 0
#endif

#define PRINT_STATS     USB_IF_DEBUG_CDC_ENABLE

#ifdef __cplusplus
}
#endif