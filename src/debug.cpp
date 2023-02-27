#include <stdio.h>
#include <stdarg.h>
#include <iterator>
#include <hardware/sync.h>
#include <pico/sync.h>
#include <tusb.h>
#include "debug.h"

#if defined(__cplusplus)
extern "C"{
#endif

static unsigned int g_dbg_print_tail;
static char g_dbg_print_buffers[10240];
static critical_section g_critical_section;


void dbg_print_init()
{
    critical_section_init(&g_critical_section);
}

#if USB_IF_DEBUG_CDC_ENABLE
static void dbg_flush_cdc_internal()
{
    auto adr = g_dbg_print_buffers;
    auto tail = g_dbg_print_buffers + g_dbg_print_tail;

    uint32_t wrote;
    while ((wrote = tud_cdc_write(adr, std::min<size_t>(tail - adr, 64))) > 0)
    {
        adr += wrote;
        tud_cdc_write_flush();
    }

    std::move(adr, tail, g_dbg_print_buffers);
    g_dbg_print_tail = tail - adr;
}

void dbg_flush_cdc()
{
    critical_section_enter_blocking(&g_critical_section);
    dbg_flush_cdc_internal();
    critical_section_exit(&g_critical_section);
}

#endif

int dbg_printf_v(bool in_isr, const char *format, va_list args)
{
#if USB_IF_DEBUG_CDC_ENABLE
    critical_section_enter_blocking(&g_critical_section);

    int ret = vsnprintf(g_dbg_print_buffers + g_dbg_print_tail, std::size(g_dbg_print_buffers) - g_dbg_print_tail, format, args);
#if USB_IF_DEBUG_CDC_ENABLE
    if(g_dbg_print_tail + ret >= std::size(g_dbg_print_buffers))
    {
        if(!in_isr)
            dbg_flush_cdc_internal();
        ret = vsnprintf(g_dbg_print_buffers + g_dbg_print_tail, std::size(g_dbg_print_buffers) - g_dbg_print_tail, format, args);
    }
#endif

    g_dbg_print_tail += ret;
    if(g_dbg_print_tail >= std::size(g_dbg_print_buffers))
        g_dbg_print_tail = 0;

    critical_section_exit(&g_critical_section);

    return ret;
#else
    return 0;
#endif
}

int dbg_printf(const char *format, ...)
{
#if USB_IF_DEBUG_CDC_ENABLE
    va_list args;

    va_start(args, format);
    int ret = dbg_printf_v(false, format, args);
    va_end(args);

    return ret;
#else
    return 0;
#endif
}

#if defined(__cplusplus)
}
#endif