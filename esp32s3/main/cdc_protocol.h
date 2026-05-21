#pragma once

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- 指令操作码 ----
enum {
    CMD_MOUSE_MOVE      = 0x01,  // int16 dx, int16 dy
    CMD_MOUSE_MOVE_TO   = 0x02,  // uint16 x, uint16 y (0-32767)
    CMD_MOUSE_CLICK     = 0x03,  // uint8 button
    CMD_MOUSE_PRESS     = 0x04,  // uint8 button
    CMD_MOUSE_RELEASE   = 0x05,  // uint8 button
    CMD_MOUSE_SCROLL    = 0x06,  // int8 delta
    CMD_KEY_PRESS       = 0x07,  // uint8 hid_code
    CMD_KEY_RELEASE     = 0x08,  // uint8 hid_code
    CMD_KEY_TAP         = 0x09,  // uint8 hid_code, uint8 count
    CMD_KEY_TYPE        = 0x0A,  // uint8 len, char[len]
    CMD_SYNC            = 0x0B,  // uint16 seq_id
    CMD_NOOP            = 0x0C,  // uint16 delay_ms
    CMD_LED_SET         = 0xFD,  // uint8 r, g, b
    CMD_ACK_REQUEST     = 0xFE,  // (no payload)
    CMD_RESET           = 0xFF,  // (no payload)
};

// 按钮常量
enum {
    BTN_LEFT   = 1,
    BTN_RIGHT  = 2,
    BTN_MIDDLE = 4,
};

// ---- 上行消息类型 ----
enum {
    UPLINK_ACK       = 0x01,
    UPLINK_STATUS    = 0x03,
    UPLINK_ERROR     = 0xFF,
};

// 状态结构体 (20 bytes, packed)
typedef struct {
    uint16_t seq_id;       // 最后执行的 seq
    uint8_t  cmd_queued;   // 缓冲区中待执行指令数
    uint8_t  kb_leds;      // 键盘 LED 状态
    int16_t  mouse_x;      // 最后鼠标位置 X
    int16_t  mouse_y;      // 最后鼠标位置 Y
    uint32_t uptime_ms;    // 设备运行时间
    uint8_t  reserved[8];
} __attribute__((packed)) status_t;

// 解析后的指令
typedef struct {
    uint8_t  opcode;
    uint8_t  payload[128]; // 最大变长 payload
    uint8_t  payload_len;
    uint16_t seq_id;       // 从 SYNC 帧提取
} cmd_t;

// ---- API ----
void cdc_protocol_init(void);
void cdc_protocol_send_ack(uint16_t seq_id, uint8_t status);
void cdc_protocol_send_status(const status_t *status);
void cdc_protocol_send_error(uint8_t code);

// 从 CDC 数据流中解析一条指令
int cdc_parse_command(const uint8_t *raw_data, size_t len,
                      cmd_t *out, size_t *consumed);

#ifdef __cplusplus
}
#endif
