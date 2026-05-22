#include "usb_descriptors.h"
#include "tusb.h"
#include <string.h>

// HID Report Descriptor — 键盘+鼠标, 通过 Report ID 区分
// Report ID: HID_ITF_PROTOCOL_KEYBOARD(1) / HID_ITF_PROTOCOL_MOUSE(2)
static const uint8_t hid_report_desc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE))
};

#define TUSB_DESC_TOTAL_LEN \
    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t hid_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0,
        TUSB_DESC_TOTAL_LEN,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // CDC
    TUD_CDC_DESCRIPTOR(
        ITF_NUM_CDC_CTRL,
        STRID_CDC,
        CDC_NOTIF_EP,
        8,
        CDC_OUT_EP,
        CDC_IN_EP,
        64),

    // HID (键盘+鼠标通过 report ID 区分)
    TUD_HID_DESCRIPTOR(
        ITF_NUM_HID,
        STRID_HID,
        false,
        sizeof(hid_report_desc),
        HID_EP,
        16,
        10),
};

// ---- TinyUSB HID Callbacks ----

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return hid_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
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
    [STRID_LANGID]  = NULL,
    [STRID_MANUF]   = "ODA",
    [STRID_PRODUCT] = "ODA HID Bridge",
    [STRID_SERIAL]  = "0001",
    [STRID_CDC]     = "ODA CDC",
    [STRID_HID]     = "ODA HID",
};

const tinyusb_config_t tusb_cfg = {
    .device_descriptor = NULL,
    .string_descriptor = usb_strings,
    .string_descriptor_count = sizeof(usb_strings) / sizeof(usb_strings[0]),
    .external_phy = false,
    .configuration_descriptor = hid_config_desc,
    .self_powered = false,
    .vbus_monitor_io = 0,
};
