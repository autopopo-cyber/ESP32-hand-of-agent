#include "usb_descriptors.h"
#include "tusb.h"
#include <string.h>

// ---- HID Report Descriptors ----

// 键盘 Report Descriptor (Boot Protocol, 8 bytes, no report ID)
static const uint8_t hid_keyboard_report_desc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

// 鼠标 Report Descriptor (no report ID)
static const uint8_t hid_mouse_report_desc[] = {
    TUD_HID_REPORT_DESC_MOUSE()
};

// ---- 组合 Configuration Descriptor ----
static const uint8_t hid_config_desc[] = {
    // 配置头
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0,
        TUD_CONFIG_DESC_LEN
        + TUD_CDC_DESC_LEN          // CDC (2 interfaces + 2 EPs)
        + TUD_HID_DESC_LEN          // HID Keyboard (1 interface + 1 EP)
        + TUD_HID_DESC_LEN,         // HID Mouse (1 interface + 1 EP)
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // === CDC ===
    // Interface 0: CDC Communication
    TUD_CDC_DESCRIPTOR(
        ITF_NUM_CDC_CTRL,
        STRID_CDC,
        CDC_NOTIF_EP,
        8,                          // notification EP max packet
        CDC_OUT_EP,                 // data OUT EP
        CDC_IN_EP,                  // data IN EP
        64),                        // data EP max packet

    // === HID Keyboard ===
    // Interface 2, Boot Protocol, Report ID=1
    TUD_HID_DESCRIPTOR(
        ITF_NUM_HID_KBD,
        STRID_HID_KBD,
        HID_ITF_PROTOCOL_KEYBOARD,
        sizeof(hid_keyboard_report_desc),
        HID_KBD_EP,
        CFG_TUD_HID_EP_BUFSIZE,
        1),                         // polling interval = 1ms

    // === HID Mouse ===
    // Interface 3, Report Protocol, Report ID=2
    TUD_HID_DESCRIPTOR(
        ITF_NUM_HID_MOUSE,
        STRID_HID_MOUSE,
        HID_ITF_PROTOCOL_MOUSE,
        sizeof(hid_mouse_report_desc),
        HID_MOUSE_EP,
        CFG_TUD_HID_EP_BUFSIZE,
        1),
};

// ---- TinyUSB HID Callbacks ----

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    switch (instance) {
    case HID_INST_KBD:
        return hid_keyboard_report_desc;
    case HID_INST_MOUSE:
        return hid_mouse_report_desc;
    default:
        return NULL;
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen) {
    (void)report_id;
    if (report_type == HID_REPORT_TYPE_INPUT) {
        if (instance == HID_INST_KBD && reqlen >= 8) {
            memset(buffer, 0, 8);
            return 8;
        }
        if (instance == HID_INST_MOUSE && reqlen >= 4) {
            memset(buffer, 0, 4);
            return 4;
        }
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const *buffer, uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

// ---- 字符串描述符 ----
static const char *usb_strings[] = {
    [STRID_LANGID]    = NULL,          // 0: reserved for LANGID
    [STRID_MANUF]     = "ODA",
    [STRID_PRODUCT]   = "ODA HID Bridge",
    [STRID_SERIAL]    = "0001",
    [STRID_CDC]       = "ODA CDC",
    [STRID_HID_KBD]   = "ODA Keyboard",
    [STRID_HID_MOUSE] = "ODA Mouse",
};

// ---- TinyUSB 配置 ----
const tinyusb_config_t tusb_cfg = {
    .device_descriptor = NULL,
    .string_descriptor = usb_strings,
    .string_descriptor_count = sizeof(usb_strings) / sizeof(usb_strings[0]),
    .external_phy = false,
    .configuration_descriptor = hid_config_desc,
    .self_powered = false,
    .vbus_monitor_io = 0,
};
