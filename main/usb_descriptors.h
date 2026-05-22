#pragma once

#include <stdint.h>
#include "tinyusb.h"

#ifdef __cplusplus
extern "C" {
#endif

// 接口索引
enum {
    ITF_NUM_CDC_CTRL = 0,
    ITF_NUM_CDC_DATA = 1,
    ITF_NUM_HID      = 2,
    ITF_NUM_TOTAL    = 3
};

// 端点地址
enum {
    CDC_NOTIF_EP  = 0x81,
    CDC_OUT_EP    = 0x02,
    CDC_IN_EP     = 0x82,
    HID_EP        = 0x83,
};

// 字符串索引
enum {
    STRID_LANGID  = 0,
    STRID_MANUF   = 1,
    STRID_PRODUCT = 2,
    STRID_SERIAL  = 3,
    STRID_CDC     = 4,
    STRID_HID     = 5,
};

extern const tinyusb_config_t tusb_cfg;

#ifdef __cplusplus
}
#endif
