
#include <tusb.h>
#include <array>
#include "usb_descriptors.h"

/* A combination of interfaces must have a unique product id, since PC will save device driver after the first plug.
 * Same VID/PID with different interface e.g MSC (first), then CDC (later) will possibly cause system error on PC.
 *
 * Auto ProductID layout's Bitmap:
 *   [MSB]     AUDIO | MIDI | HID | MSC | CDC          [LSB]
 */
#define _PID_MAP(itf, n) ((CFG_TUD_##itf) << (n))
#define USB_PID (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | \
                 _PID_MAP(MIDI, 3) | _PID_MAP(AUDIO, 4) | _PID_MAP(VENDOR, 5))


//--------------------------------------------------------------------+
// String Table
//--------------------------------------------------------------------+

// array of pointer to string descriptors
char const *string_desc_arr[] ={
     (const char[]){0x09, 0x04}, // 0: is supported language is English (0x0409)
        "RSPG",                     // 1: Manufacturer
        "Pico Audio",               // 2: Product
        "000001",                   // 3: Serials, should use chip ID
        "Pico Audio Speakers",      // 4: Audio Interface
        "Pico Audio LineIn",        // 5: Audio Interface
        "Pico Audio Control",       // 6: Audio Interface 
        "Debug Serial Port"
};
enum DESCRIPTOR_STRING {
    STR_NULL,
    STR_MANUFACTURER,
    STR_PRODUCT,
    STR_SERIAL,
    STR_AUDIO_INTERFACE0,
    STR_AUDIO_INTERFACE1,
    STR_AUDIO_INTERFACE2,
    STR_DEBUG_INTERFACE,
};

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device =
    {
        .bLength = sizeof(tusb_desc_device_t),
        .bDescriptorType = TUSB_DESC_DEVICE,
#if USB_IF_CONTROL_ENABLE
        .bcdUSB = 0x0210,
#else
        .bcdUSB = 0x0200,
#endif

        // Use Interface Association Descriptor (IAD) for CDC
        // As required by USB Specs IAD's subclass must be common class (2) and protocol must be IAD (1)
        .bDeviceClass = TUSB_CLASS_MISC,
        .bDeviceSubClass = MISC_SUBCLASS_COMMON,
        .bDeviceProtocol = MISC_PROTOCOL_IAD,
        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

        .idVendor = 0xcafe,
        .idProduct = USB_PID,
        .bcdDevice = 0x0100,

        .iManufacturer = STR_MANUFACTURER,
        .iProduct = STR_PRODUCT,
        .iSerialNumber = STR_SERIAL,

        .bNumConfigurations = 0x01 };

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
extern "C" uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

template <size_t N>
class descriptor
{
public:
    constexpr uint8_t &operator[](size_t i)
    {
        return m_array[i];
    }
    constexpr uint8_t operator[](size_t i) const
    {
        return m_array[i];
    }
    constexpr size_t size() const
    {
        return N;
    }
    constexpr const uint8_t *data() const
    {
        return m_array;
    }

    template <size_t M>
    constexpr auto operator+(const descriptor<M> &src2) const
    {
        descriptor<N + M> desc = {};
        for (size_t i = 0; i < N; ++i)
            desc[i] = m_array[i];
        for (size_t i = 0; i < M; ++i)
            desc[i + N] = src2[i];
        return desc;
    }

    uint8_t m_array[N];
};
template <class... U>
descriptor(U...) -> descriptor<sizeof...(U)>;

#if USB_IF_AUDIO_ENABLE

#define TUD_AUDIO_DESC_SELECTOR_TWO_LEN (8 + 1)
#define TUD_AUDIO_DESC_SELECTOR_TWO(_unitid, _src1, _src2, _ctrl, _stridx) \
    TUD_AUDIO_DESC_SELECTOR_TWO_LEN, TUSB_DESC_CS_INTERFACE, AUDIO_CS_AC_INTERFACE_SELECTOR_UNIT, _unitid, 2, _src1, src_2, _ctrl, _stridx

#define TUD_AUDIO_DESC_MIXER_STEREO_TWO_PINS_LEN (13 + 2 + 1)
#define TUD_AUDIO_DESC_MIXER_STEREO_TWO_PINS(_unitid, _src1, _src2, _idxchannelnames, _mixerctrl, _ctrl, _stridx) \
    TUD_AUDIO_DESC_MIXER_STEREO_TWO_PINS_LEN, TUSB_DESC_CS_INTERFACE, AUDIO_CS_AC_INTERFACE_MIXER_UNIT, _unitid, 2, _src1, _src2, 2, U32_TO_U8S_LE(AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT), _idxchannelnames, _mixerctrl, _ctrl, _stridx

constexpr auto audio_device_descriptor_units_generator()
{
    return descriptor{
        /* Clock Source Descriptor(4.7.2.1) */
        TUD_AUDIO_DESC_CLK_SRC(/*_clkid*/ UAC2_ENTITY_USB_INPUT_CLOCK, /*_attr*/ AUDIO_CLOCK_SOURCE_ATT_INT_PRO_CLK, /*_ctrl*/ (AUDIO_CTRL_RW << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS), /*_assocTerm*/ 0x00, /*_stridx*/ STR_NULL),
        /* Clock Source Descriptor(4.7.2.1) */
        TUD_AUDIO_DESC_CLK_SRC(/*_clkid*/ UAC2_ENTITY_LINEIN_CLOCK, /*_attr*/ AUDIO_CLOCK_SOURCE_ATT_INT_PRO_CLK, /*_ctrl*/ (AUDIO_CTRL_RW << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS), /*_assocTerm*/ 0x00, /*_stridx*/ STR_NULL),

        /* Input Terminal Descriptor(4.7.2.4) */
        TUD_AUDIO_DESC_INPUT_TERM(/*_termid*/ UAC2_ENTITY_USB_INPUT_TERMINAL, /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING, /*_assocTerm*/ 0x00, /*_clkid*/ UAC2_ENTITY_USB_INPUT_CLOCK, /*_nchannelslogical*/ 0x02, /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT, /*_idxchannelnames*/ 0x00, /*_ctrl*/ 0, /*_stridx*/ STR_NULL),
        /* Output Terminal Descriptor(4.7.2.5) */
        TUD_AUDIO_DESC_OUTPUT_TERM(/*_termid*/ UAC2_ENTITY_SPEAKER_OUTPUT_TERMINAL, /*_termtype*/ AUDIO_TERM_TYPE_OUT_GENERIC_SPEAKER, /*_assocTerm*/ 0x00, /*_srcid*/ UAC2_ENTITY_USB_INPUT_TERMINAL, /*_clkid*/ UAC2_ENTITY_USB_INPUT_CLOCK, /*_ctrl*/ 0x0000, /*_stridx*/ STR_NULL),

        /* Input Terminal Descriptor(4.7.2.4) */
        TUD_AUDIO_DESC_INPUT_TERM(/*_termid*/ UAC2_ENTITY_LINEIN_INPUT_TERMINAL, /*_termtype*/ AUDIO_TERM_TYPE_EXTERNAL_LINE, /*_assocTerm*/ 0x00, /*_clkid*/ UAC2_ENTITY_LINEIN_CLOCK, /*_nchannelslogical*/ 0x02, /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT, /*_idxchannelnames*/ 0x00, /*_ctrl*/ 0, /*_stridx*/ STR_NULL),
        /* Output Terminal Descriptor(4.7.2.5) */
        TUD_AUDIO_DESC_OUTPUT_TERM(/*_termid*/ UAC2_ENTITY_LINEIN_OUTPUT_TERMINAL, /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING, /*_assocTerm*/ 0x00, /*_srcid*/ UAC2_ENTITY_LINEIN_INPUT_TERMINAL, /*_clkid*/ UAC2_ENTITY_LINEIN_CLOCK, /*_ctrl*/ 0x0000, /*_stridx*/ STR_NULL),
    };
}

constexpr auto audio_device_descriptor_interface_alternate_generator(uint8_t itf, uint8_t altset, uint8_t itfname, uint8_t termid, uint8_t channels, uint8_t subslotsize, uint8_t bitresolution, uint8_t ep, uint8_t epAttr, uint16_t maxEPsize, uint8_t lockdelayunit, uint16_t lockdelay)
{
    return descriptor{
        /* Standard AS Interface Descriptor(4.9.1) */
        /* Interface 2, Alternate 1 - alternate interface for data streaming */
        TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)(itf), /*_altset*/ altset, /*_nEPs*/ 0x01, /*_stridx*/ itfname),
        /* Class-Specific AS Interface Descriptor(4.9.2) */
        TUD_AUDIO_DESC_CS_AS_INT(/*_termid*/ termid, /*_ctrl*/ AUDIO_CTRL_NONE, /*_formattype*/ AUDIO_FORMAT_TYPE_I, /*_formats*/ AUDIO_DATA_FORMAT_TYPE_I_PCM, /*_nchannelsphysical*/ channels, /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, /*_stridx*/ STR_NULL),
        /* Type I Format Type Descriptor(2.3.1.6 - Audio Formats) */
        TUD_AUDIO_DESC_TYPE_I_FORMAT(subslotsize, bitresolution),
        /* Standard AS Isochronous Audio Data Endpoint Descriptor(4.10.1.1) */
        TUD_AUDIO_DESC_STD_AS_ISO_EP(/*_ep*/ ep, /*_attr*/ epAttr, /*_maxEPsize*/ maxEPsize, /*_interval*/ 0x01),
        /* Class-Specific AS Isochronous Audio Data Endpoint Descriptor(4.10.1.2) */
        TUD_AUDIO_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, /*_ctrl*/ AUDIO_CTRL_NONE, /*_lockdelayunit*/ lockdelayunit, /*_lockdelay*/ lockdelay)};
}

constexpr auto audio_device_descriptor_interface_alternate_generator(uint8_t itf, uint8_t altset, uint8_t itfname, uint8_t termid, uint8_t channels, uint8_t subslotsize, uint8_t bitresolution, uint8_t ep, uint8_t epAttr, uint16_t maxEPsize, uint8_t lockdelayunit, uint16_t lockdelay, uint8_t epfb)
{
    return descriptor{
        /* Standard AS Interface Descriptor(4.9.1) */
        /* Interface 2, Alternate 1 - alternate interface for data streaming */
        TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)(itf), /*_altset*/ altset, /*_nEPs*/ 0x02, /*_stridx*/ itfname),
        /* Class-Specific AS Interface Descriptor(4.9.2) */
        TUD_AUDIO_DESC_CS_AS_INT(/*_termid*/ termid, /*_ctrl*/ AUDIO_CTRL_NONE, /*_formattype*/ AUDIO_FORMAT_TYPE_I, /*_formats*/ AUDIO_DATA_FORMAT_TYPE_I_PCM, /*_nchannelsphysical*/ channels, /*_channelcfg*/ AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, /*_stridx*/ STR_NULL),
        /* Type I Format Type Descriptor(2.3.1.6 - Audio Formats) */
        TUD_AUDIO_DESC_TYPE_I_FORMAT(subslotsize, bitresolution),
        /* Standard AS Isochronous Audio Data Endpoint Descriptor(4.10.1.1) */
        TUD_AUDIO_DESC_STD_AS_ISO_EP(/*_ep*/ ep, /*_attr*/ epAttr, /*_maxEPsize*/ maxEPsize, /*_interval*/ 0x01),
        /* Class-Specific AS Isochronous Audio Data Endpoint Descriptor(4.10.1.2) */
        TUD_AUDIO_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, /*_ctrl*/ AUDIO_CTRL_NONE, /*_lockdelayunit*/ lockdelayunit, /*_lockdelay*/ lockdelay),
        /* Standard AS Isochronous Feedback Endpoint Descriptor(4.10.2.1) */
        TUD_AUDIO_DESC_STD_AS_ISO_FB_EP(/*_ep*/ epfb, /*_interval*/ 0x01) };
}

constexpr auto audio_device_descriptor_output_interface_generator(uint8_t itf, uint8_t itfname, uint8_t termid, uint8_t epout, uint8_t epoutfb)
{
    const uint8_t epAttr = (TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS | TUSB_ISO_EP_ATT_DATA);

    return descriptor{
               /* Standard AS Interface Descriptor(4.9.1) */
               /* Interface 2, Alternate 0 - default alternate setting with 0 bandwidth */
               TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)(itf), /*_altset*/ 0x00, /*_nEPs*/ 0x00, /*_stridx*/ itfname),
           } 
           + audio_device_descriptor_interface_alternate_generator(itf, 0x01, itfname, termid, 2, 2, 16, epout, epAttr, TUD_AUDIO_EP_SIZE(48000, 2, 2), AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0, epoutfb) 
           + audio_device_descriptor_interface_alternate_generator(itf, 0x02, itfname, termid, 2, 3, 24, epout, epAttr, TUD_AUDIO_EP_SIZE(48000, 3, 2), AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0, epoutfb)
           + audio_device_descriptor_interface_alternate_generator(itf, 0x03, itfname, termid, 2, 2, 16, epout, epAttr, TUD_AUDIO_EP_SIZE(96000, 2, 2), AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0, epoutfb) 
           + audio_device_descriptor_interface_alternate_generator(itf, 0x04, itfname, termid, 2, 3, 24, epout, epAttr, TUD_AUDIO_EP_SIZE(96000, 3, 2), AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0, epoutfb)
           ;
}

constexpr auto audio_device_descriptor_input_interface_generator(uint8_t itf, uint8_t itfname, uint8_t termid, uint8_t epin)
{
    const uint8_t epAttr = (TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS | TUSB_ISO_EP_ATT_DATA);

    return descriptor{
               /* Standard AS Interface Descriptor(4.9.1) */
               /* Interface 2, Alternate 0 - default alternate setting with 0 bandwidth */
               TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)(itf), /*_altset*/ 0x00, /*_nEPs*/ 0x00, /*_stridx*/ itfname),
           } 
           + audio_device_descriptor_interface_alternate_generator(itf, 0x01, itfname, termid, 2, 2, 16, epin, epAttr, TUD_AUDIO_EP_SIZE(48000, 2, 2), AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0) 
           + audio_device_descriptor_interface_alternate_generator(itf, 0x02, itfname, termid, 2, 3, 24, epin, epAttr, TUD_AUDIO_EP_SIZE(48000, 3, 2), AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0) 
           + audio_device_descriptor_interface_alternate_generator(itf, 0x03, itfname, termid, 2, 2, 16, epin, epAttr, TUD_AUDIO_EP_SIZE(96000, 2, 2), AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0) 
           + audio_device_descriptor_interface_alternate_generator(itf, 0x04, itfname, termid, 2, 3, 24, epin, epAttr, TUD_AUDIO_EP_SIZE(96000, 3, 2), AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0);
}

constexpr auto audio_device_descriptor_generator()
{
    return descriptor{
               /* Standard Interface Association Descriptor (IAD) */
               TUD_AUDIO_DESC_IAD(/*_firstitfs*/ ITF_NUM_AUDIO_CONTROL, /*_nitfs*/ ITF_NUM_AUDIO_TOTAL, /*_stridx*/ STR_NULL),
               /* Standard AC Interface Descriptor(4.7.1) */
               TUD_AUDIO_DESC_STD_AC(/*_itfnum*/ ITF_NUM_AUDIO_CONTROL, /*_nEPs*/ 0x00, /*_stridx*/ STR_PRODUCT),
               /* Class-Specific AC Interface Header Descriptor(4.7.2) */
               TUD_AUDIO_DESC_CS_AC(/*_bcdADC*/ 0x0200, /*_category*/ AUDIO_FUNC_DESKTOP_SPEAKER, /*_totallen*/ std::size(audio_device_descriptor_units_generator()), /*_ctrl*/ AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS)}

           + audio_device_descriptor_units_generator()

           + audio_device_descriptor_output_interface_generator((uint8_t)(ITF_NUM_AUDIO_STREAMING_HOST_TX), STR_AUDIO_INTERFACE0, UAC2_ENTITY_USB_INPUT_TERMINAL, EP_AUDIO_STREAM, EP_AUDIO_STREAM_OUT_FB | 0x80) 
           + audio_device_descriptor_input_interface_generator((uint8_t)(ITF_NUM_AUDIO_STREAMING_HOST_RX), STR_AUDIO_INTERFACE1, UAC2_ENTITY_LINEIN_OUTPUT_TERMINAL, EP_AUDIO_STREAM | 0x80)

        ;
}

#endif

#if USB_IF_CONTROL_ENABLE

constexpr auto fn_device_descriptor_generator()
{
    return descriptor{
        // Interface number, Alternate count, starting string index, attributes, detach timeout, transfer size
        TUD_VENDOR_DESCRIPTOR(ITF_NUM_FN_CONTROL, STR_AUDIO_INTERFACE2, EP_AUDIO_USER_CONTROL, 0x80 | EP_AUDIO_USER_CONTROL, 64)};
}

#endif

#if USB_IF_DEBUG_CDC_ENABLE

constexpr auto debug_cdc_descriptor_generator()
{
    return descriptor{
        // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
        TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_DEBUG, STR_DEBUG_INTERFACE, EP_DEBUG_CDC_NOTIFY|0x80, 8, EP_DEBUG_CDC_DATA, EP_DEBUG_CDC_DATA|0x80, 64) 
    };
}

#endif

#if USB_IF_AUDIO_ENABLE
extern "C" const unsigned long tud_audio_headset_stereo_desc_len = std::size(audio_device_descriptor_generator());
#endif

constexpr auto desc_configuration_len = TUD_CONFIG_DESC_LEN 
#if USB_IF_AUDIO_ENABLE
    + std::size(audio_device_descriptor_generator()) 
#endif
#if USB_IF_CONTROL_ENABLE
    + std::size(fn_device_descriptor_generator())
#endif
#if USB_IF_DEBUG_CDC_ENABLE
    + std::size(debug_cdc_descriptor_generator())
#endif
    ;

constexpr auto const desc_configuration =
    descriptor{ TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, STR_NULL, desc_configuration_len, 0x00, 100) } 
#if USB_IF_AUDIO_ENABLE
    + audio_device_descriptor_generator() 
#endif
#if USB_IF_CONTROL_ENABLE
    + fn_device_descriptor_generator()
#endif
#if USB_IF_DEBUG_CDC_ENABLE
    + debug_cdc_descriptor_generator()
#endif
    ;

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    return desc_configuration.data();
}

#if USB_IF_CONTROL_ENABLE

//--------------------------------------------------------------------+
// BOS Descriptor
//--------------------------------------------------------------------+

/* Microsoft OS 2.0 registry property descriptor
Per MS requirements https://msdn.microsoft.com/en-us/library/windows/hardware/hh450799(v=vs.85).aspx
device should create DeviceInterfaceGUIDs. It can be done by driver and
in case of real PnP solution device should expose MS "Microsoft OS 2.0
registry property descriptor". Such descriptor can insert any record
into Windows registry per device/configuration/interface. In our case it
will insert "DeviceInterfaceGUIDs" multistring property.

GUID is freshly generated and should be OK to use.

https://developers.google.com/web/fundamentals/native-hardware/build-for-webusb/
(Section Microsoft OS compatibility descriptors)
*/

// Descriptor Length
#define TUD_BOS_CONTROLLER_DESC_LEN         24

// Vendor Code, iLandingPage
#define TUD_BOS_CONTROLLER_DESCRIPTOR(_vendor_code, _ipage) \
  TUD_BOS_PLATFORM_DESCRIPTOR(TUD_BOS_CONTROLLER_UUID, U16_TO_U8S_LE(0x0100), _vendor_code, _ipage)

#define TUD_BOS_CONTROLLER_UUID   \
  0xd4, 0x39, 0x9a, 0xc5, 0x27, 0x77, 0x4b, 0xd9, \
  0xb9, 0x69, 0xf9, 0x4e, 0x5e, 0x59, 0xa4, 0xa4


#define MS_OS_20_DESC_LEN 0xB2
#define BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_CONTROLLER_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)


// BOS Descriptor is required for webUSB
constexpr auto bos_descriptor_generator()
{
    return descriptor{
        // total length, number of device caps
        TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 2),

        // Vendor Code, iLandingPage
        TUD_BOS_CONTROLLER_DESCRIPTOR(VENDOR_REQUEST_CONTROLLER, 1),

        // Microsoft OS 2.0 descriptor
        TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT)};
}

extern "C" uint8_t const *tud_descriptor_bos_cb(void)
{
    static auto s_desc = bos_descriptor_generator();
    return s_desc.data();
}

constexpr auto msos2_descriptor_generator()
{
    return descriptor{
        // Set header: length, type, windows version, total length
        U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN), //10

        // Configuration subset header: length, type, configuration index, reserved, configuration total length
        U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A), //8

        // Function Subset header: length, type, first interface, reserved, subset length
        U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_NUM_FN_CONTROL, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),

        // MS OS 2.0 Compatible ID descriptor: length, type, compatible ID, sub compatible ID
        U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // sub-compatible

        // MS OS 2.0 Registry property descriptor: length, type
        U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
        U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A), // wPropertyDataType, wPropertyNameLength and PropertyName "DeviceInterfaceGUIDs\0" in UTF-16
        'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00,
        'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00, 0x00, 0x00,
        U16_TO_U8S_LE(0x0050), // wPropertyDataLength
                               // bPropertyData: “{8ED17B68-5386-4AB3-8269-B0E237AF481B}”.
        '{', 0x00, '8', 0x00, 'E', 0x00, 'D', 0x00, '1', 0x00, '7', 0x00, 'B', 0x00, '6', 0x00, '8', 0x00, '-', 0x00,
        '5', 0x00, '3', 0x00, '8', 0x00, '6', 0x00, '-', 0x00, '4', 0x00, 'A', 0x00, 'B', 0x00, '3', 0x00, '-', 0x00,
        '8', 0x00, '2', 0x00, '6', 0x00, '9', 0x00, '-', 0x00, 'B', 0x00, '0', 0x00, 'E', 0x00, '2', 0x00, '3', 0x00,
        '7', 0x00, 'A', 0x00, 'F', 0x00, '4', 0x00, '8', 0x00, '1', 0x00, 'B', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00};
}

const uint8_t* get_msos2_descriptor()
{
    static auto s_desc = msos2_descriptor_generator();
    return s_desc.data();
}

TU_VERIFY_STATIC(std::size(msos2_descriptor_generator()) == MS_OS_20_DESC_LEN, "Incorrect size");

#endif

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
extern "C" uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    uint8_t chr_count;

    if (index == 0)
    {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    }
    else
    {
        // Convert ASCII string into UTF-16

        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
            return NULL;

        const char *str = string_desc_arr[index];

        // Cap at max char
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31)
            chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++)
        {
            _desc_str[1 + i] = str[i];
        }
    }

    // first byte is length (including header), second byte is string type
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return _desc_str;
}