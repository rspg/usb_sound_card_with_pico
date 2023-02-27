#pragma once

#include "usb_config.h"

#ifdef __cplusplus
extern "C"{
#endif

#if !defined(DBG_ASSERT_ENABLE)
#if NDEBUG
#define DBG_ASSERT_ENABLE   0
#else
#define DBG_ASSERT_ENABLE   1
#endif
#endif


void dbg_print_init();
int dbg_printf(const char *format, ...);

#if USB_IF_DEBUG_CDC_ENABLE
void dbg_flush_cdc();
#endif

#if DBG_ASSERT_ENABLE
inline void dbg_assert(bool cond) { while(!cond) __breakpoint();  }
#else
inline void dbg_assert(bool cond) {}
#endif

#ifdef __cplusplus
}
#endif