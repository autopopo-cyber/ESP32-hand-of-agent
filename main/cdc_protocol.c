#include "cdc_protocol.h"
#include "tusb.h"
#include <string.h>

// 已知的指令编码长度表 (不含 opcode 自身, -1=变长, -128=未知)
static const int8_t cmd_payload_len[256] = {
    [CMD_MOUSE_MOVE]    = 4,   // int16 dx + int16 dy
    [CMD_MOUSE_MOVE_TO] = 4,   // uint16 x + uint16 y
    [CMD_MOUSE_CLICK]   = 1,
    [CMD_MOUSE_PRESS]   = 1,
    [CMD_MOUSE_RELEASE] = 1,
    [CMD_MOUSE_SCROLL]  = 1,
    [CMD_KEY_PRESS]     = 1,
    [CMD_KEY_RELEASE]   = 1,
    [CMD_KEY_TAP]       = 2,
    [CMD_KEY_TYPE]      = -1,  // 变长: 1字节长度 + N字节内容
    [CMD_SYNC]          = 2,
    [CMD_NOOP]          = 2,
    [CMD_LED_SET]       = 3,
    [CMD_ACK_REQUEST]   = 0,
    [CMD_RESET]         = 0,
    // default: -128 = 未知指令 (由 memcmp 初始化保证)
};

void cdc_protocol_init(void) {
    // 预留
}

int cdc_parse_command(const uint8_t *raw_data, size_t len,
                      cmd_t *out, size_t *consumed) {
    if (len < 1) return 0;

    uint8_t opcode = raw_data[0];
    int8_t fixed_len = cmd_payload_len[opcode];

    // 未知指令: 跳过 1 字节
    if (fixed_len == -128) {
        cdc_protocol_send_error(0x01);
        *consumed = 1;
        return 0;
    }

    size_t payload_len;
    if (fixed_len == -1) {
        // 变长指令 (KEY_TYPE): 第二字节是字符串长度
        if (len < 2) return 0;
        payload_len = 1 + raw_data[1]; // 1字节 len + 字符串内容
        if (len < 1 + payload_len) return 0;
    } else {
        payload_len = (size_t)fixed_len;
        if (len < 1 + payload_len) return 0;
    }

    out->opcode = opcode;
    out->payload_len = payload_len;
    memcpy(out->payload, raw_data + 1, payload_len);
    *consumed = 1 + payload_len;

    // 跟踪 seq_id
    static uint16_t current_seq = 0;
    if (opcode == CMD_SYNC && payload_len >= 2) {
        current_seq = (uint16_t)out->payload[0]
                    | ((uint16_t)out->payload[1] << 8);
    }
    out->seq_id = current_seq;

    return 1;
}

void cdc_protocol_send_ack(uint16_t seq_id, uint8_t status) {
    uint8_t buf[4] = {
        UPLINK_ACK,
        (uint8_t)(seq_id & 0xFF),
        (uint8_t)((seq_id >> 8) & 0xFF),
        status
    };
    tud_cdc_write(buf, sizeof(buf));
    tud_cdc_write_flush();
}

void cdc_protocol_send_status(const status_t *status) {
    uint8_t buf[1 + sizeof(status_t)];
    buf[0] = UPLINK_STATUS;
    memcpy(buf + 1, status, sizeof(status_t));
    tud_cdc_write(buf, sizeof(buf));
    tud_cdc_write_flush();
}

void cdc_protocol_send_error(uint8_t code) {
    uint8_t buf[2] = { UPLINK_ERROR, code };
    tud_cdc_write(buf, sizeof(buf));
    tud_cdc_write_flush();
}
