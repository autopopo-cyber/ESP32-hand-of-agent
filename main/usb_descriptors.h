#pragma once

#include <stdint.h>
#include "tinyusb.h"

#ifdef __cplusplus
extern "C" {
#endif

// 接口索引
enum {
    ITF_NUM_CDC_CTRL = 0,   // CDC 通信接口
    ITF_NUM_CDC_DATA = 1,   // CDC 数据接口
    ITF_NUM_HID_KBD   = 2,  // HID 键盘接口
    ITF_NUM_HID_MOUSE = 3,  // HID 鼠标接口
    ITF_NUM_TOTAL     = 4
};

// 端点地址
enum {
    // CDC
    CDC_NOTIF_EP  = 0x81,   // IN, interrupt
    CDC_OUT_EP    = 0x02,   // OUT, bulk
    CDC_IN_EP     = 0x82,   // IN, bulk
    // HID Keyboard
    HID_KBD_EP    = 0x83,   // IN, interrupt
    // HID Mouse
    HID_MOUSE_EP  = 0x84,   // IN, interrupt
};

// HID instance indices (TinyUSB sequential order, not interface numbers)
enum {
    HID_INST_KBD = 0,
    HID_INST_MOUSE = 1,
};

// 字符串索引
enum {
    STRID_LANGID    = 0,
    STRID_MANUF     = 1,
    STRID_PRODUCT   = 2,
    STRID_SERIAL    = 3,
    STRID_CDC       = 4,
    STRID_HID_KBD   = 5,
    STRID_HID_MOUSE = 6,
};

extern const tinyusb_config_t tusb_cfg;

#ifdef __cplusplus
}
#endif
