/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#include "usb_config.h"

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

// defined by compiler flags for flexibility
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#if CFG_TUSB_MCU == OPT_MCU_LPC43XX || CFG_TUSB_MCU == OPT_MCU_LPC18XX || CFG_TUSB_MCU == OPT_MCU_MIMXRT10XX
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED)
#else
#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_DEVICE
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              0
#endif

// Enable Device stack
#define CFG_TUD_ENABLED             1

#define CFG_TUSB_DEBUG_PRINTF       dbg_printf

// CFG_TUSB_DEBUG is defined by compiler in DEBUG build
// #define CFG_TUSB_DEBUG           0

/* USB DMA on some MCUs can only access a specific SRAM region with restriction on alignment.
 * Tinyusb use follows macros to declare transferring memory so that they can be put
 * into those specific section.
 * e.g
 * - CFG_TUSB_MEM SECTION : __attribute__ (( section(".usb_ram") ))
 * - CFG_TUSB_MEM_ALIGN   : __attribute__ ((aligned(4)))
 */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

//------------- CLASS -------------//
#define CFG_TUD_CDC               (USB_IF_DEBUG_CDC_ENABLE ? 1 : 0)
#define CFG_TUD_MSC               0
#define CFG_TUD_HID               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_AUDIO             (USB_IF_AUDIO_ENABLE ? 1 : 0)
#define CFG_TUD_VENDOR            (USB_IF_CONTROL_ENABLE ? 1 : 0)
#define CFG_TUD_DFU               0

//--------------------------------------------------------------------
// AUDIO CLASS DRIVER CONFIGURATION
//--------------------------------------------------------------------

#if CFG_TUD_AUDIO

extern const unsigned long tud_audio_headset_stereo_desc_len;

#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN             tud_audio_headset_stereo_desc_len

// EP and buffer size - for isochronous EP´s, the buffer and EP size are equal (different sizes would not make sense)
#define CFG_TUD_AUDIO_ENABLE_EP_IN                1

#define CFG_TUD_AUDIO_FUNC_1_EP_SZ_IN             TUD_AUDIO_EP_SIZE(96000, 4, 2)

#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX         CFG_TUD_AUDIO_FUNC_1_EP_SZ_IN // Maximum EP IN size for all AS alternate settings used
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ      CFG_TUD_AUDIO_FUNC_1_EP_SZ_IN+1

// EP and buffer size - for isochronous EP´s, the buffer and EP size are equal (different sizes would not make sense)
#define CFG_TUD_AUDIO_ENABLE_EP_OUT               1

#define CFG_TUD_AUDIO_FUNC_1_EP_SZ_OUT            TUD_AUDIO_EP_SIZE(96000, 4, 2)

#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX        CFG_TUD_AUDIO_FUNC_1_EP_SZ_OUT // Maximum EP IN size for all AS alternate settings used
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ     CFG_TUD_AUDIO_FUNC_1_EP_SZ_OUT+1

// Number of Standard AS Interface Descriptors (4.9.1) defined per audio function - this is required to be able to remember the current alternate settings of these interfaces - We restrict us here to have a constant number for all audio functions (which means this has to be the maximum number of AS interfaces an audio function has and a second audio function with less AS interfaces just wastes a few bytes)
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT 	          4

// Size of control request buffer
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ	      64

#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP          1
// #define CFG_TUD_AUDIO_ENABLE_FEEDBACK_FORMAT_CORRECTION  1

#endif

#if CFG_TUD_VENDOR

// Vendor FIFO size of TX and RX
// If not configured vendor endpoints will not be buffered
#define CFG_TUD_VENDOR_RX_BUFSIZE (64)
#define CFG_TUD_VENDOR_TX_BUFSIZE (64)

#endif

#if CFG_TUD_CDC

// CDC FIFO size of TX and RX
#define CFG_TUD_CDC_RX_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)
#define CFG_TUD_CDC_TX_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)

// CDC Endpoint transfer buffer size, more is faster
#define CFG_TUD_CDC_EP_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)

#endif

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
