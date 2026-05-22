#include "hid_executor.h"
#include "tusb.h"
#include "class/hid/hid_device.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "cdc_protocol.h"
#include <stdbool.h>
#include <string.h>

#define CMD_QUEUE_SIZE 64

static cmd_t cmd_queue[CMD_QUEUE_SIZE];
static volatile int cmd_head = 0;
static volatile int cmd_tail = 0;
static hid_state_t g_state = {0};

// USB HID keycode 转 ASCII (US QWERTY 映射, 128 entries)
static const uint8_t ascii_to_hid[128] = {
    ['\b'] = 0x2A,
    ['\t'] = 0x2B,
    ['\n'] = 0x28,
    [' ']  = 0x2C,
    ['!']  = 0x1E,
    ['"']  = 0x34,
    ['#']  = 0x20,
    ['$']  = 0x21,
    ['%']  = 0x22,
    ['&']  = 0x23,
    ['\''] = 0x34,
    ['(']  = 0x25,
    [')']  = 0x26,
    ['*']  = 0x25,
    ['+']  = 0x2E,
    [',']  = 0x36,
    ['-']  = 0x2D,
    ['.']  = 0x37,
    ['/']  = 0x38,
    ['0']  = 0x27,
    ['1']  = 0x1E,
    ['2']  = 0x1F,
    ['3']  = 0x20,
    ['4']  = 0x21,
    ['5']  = 0x22,
    ['6']  = 0x23,
    ['7']  = 0x24,
    ['8']  = 0x25,
    ['9']  = 0x26,
    [':']  = 0x33,
    [';']  = 0x33,
    ['<']  = 0x36,
    ['=']  = 0x2E,
    ['>']  = 0x37,
    ['?']  = 0x38,
    ['@']  = 0x1F,
    ['A']  = 0x04,
    ['B']  = 0x05,
    ['C']  = 0x06,
    ['D']  = 0x07,
    ['E']  = 0x08,
    ['F']  = 0x09,
    ['G']  = 0x0A,
    ['H']  = 0x0B,
    ['I']  = 0x0C,
    ['J']  = 0x0D,
    ['K']  = 0x0E,
    ['L']  = 0x0F,
    ['M']  = 0x10,
    ['N']  = 0x11,
    ['O']  = 0x12,
    ['P']  = 0x13,
    ['Q']  = 0x14,
    ['R']  = 0x15,
    ['S']  = 0x16,
    ['T']  = 0x17,
    ['U']  = 0x18,
    ['V']  = 0x19,
    ['W']  = 0x1A,
    ['X']  = 0x1B,
    ['Y']  = 0x1C,
    ['Z']  = 0x1D,
    ['[']  = 0x2F,
    ['\\'] = 0x31,
    [']']  = 0x30,
    ['^']  = 0x23,
    ['_']  = 0x2D,
    ['`']  = 0x35,
    ['a']  = 0x04,
    ['b']  = 0x05,
    ['c']  = 0x06,
    ['d']  = 0x07,
    ['e']  = 0x08,
    ['f']  = 0x09,
    ['g']  = 0x0A,
    ['h']  = 0x0B,
    ['i']  = 0x0C,
    ['j']  = 0x0D,
    ['k']  = 0x0E,
    ['l']  = 0x0F,
    ['m']  = 0x10,
    ['n']  = 0x11,
    ['o']  = 0x12,
    ['p']  = 0x13,
    ['q']  = 0x14,
    ['r']  = 0x15,
    ['s']  = 0x16,
    ['t']  = 0x17,
    ['u']  = 0x18,
    ['v']  = 0x19,
    ['w']  = 0x1A,
    ['x']  = 0x1B,
    ['y']  = 0x1C,
    ['z']  = 0x1D,
    ['{']  = 0x2F,
    ['|']  = 0x31,
    ['}']  = 0x30,
    ['~']  = 0x35,
};

static bool char_needs_shift(char c) {
    return (c >= 'A' && c <= 'Z')
        || (c >= '!' && c <= '@' && c != '\'' && c != ',' && c != '-'
            && c != '.' && c != '/' && c != ';' && c != '=')
        || c == '_' || c == '{' || c == '}' || c == '|' || c == '~'
        || c == ':' || c == '<' || c == '>' || c == '?' || c == '"';
}

void hid_executor_init(void) {
    memset(&g_state, 0, sizeof(g_state));
    g_state.last_action_ms = esp_timer_get_time() / 1000;
}

int hid_executor_submit(const cmd_t *cmd) {
    int next_head = (cmd_head + 1) % CMD_QUEUE_SIZE;
    if (next_head == cmd_tail) return -1;
    memcpy(&cmd_queue[cmd_head], cmd, sizeof(cmd_t));
    cmd_head = next_head;
    return 0;
}

static void process_one_command(void) {
    if (cmd_tail == cmd_head) return;

    cmd_t *cmd = &cmd_queue[cmd_tail];

    switch (cmd->opcode) {
    case CMD_MOUSE_MOVE: {
        if (!tud_hid_ready()) return;
        int16_t dx = (int16_t)(cmd->payload[0] | (cmd->payload[1] << 8));
        int16_t dy = (int16_t)(cmd->payload[2] | (cmd->payload[3] << 8));
        g_state.mouse_x += dx;
        g_state.mouse_y += dy;
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE,g_state.mouse_buttons,
                               dx, dy, 0, 0);
        break;
    }
    case CMD_MOUSE_MOVE_TO: {
        if (!tud_hid_ready()) return;
        uint16_t target_x = cmd->payload[0] | (cmd->payload[1] << 8);
        uint16_t target_y = cmd->payload[2] | (cmd->payload[3] << 8);
        for (int i = 0; i < 80; i++) {
            tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE,0, -127, -127, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(8));
        }
        int32_t dx = (int32_t)target_x;
        int32_t dy = (int32_t)target_y;
        while (dx != 0 || dy != 0) {
            int8_t sx = (dx > 120) ? 120 : (dx < -120) ? -120 : (int8_t)dx;
            int8_t sy = (dy > 120) ? 120 : (dy < -120) ? -120 : (int8_t)dy;
            tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE,0, sx, sy, 0, 0);
            dx -= sx; dy -= sy;
            vTaskDelay(pdMS_TO_TICKS(8));
        }
        g_state.mouse_x = target_x;
        g_state.mouse_y = target_y;
        break;
    }
    case CMD_MOUSE_CLICK: {
        if (!tud_hid_ready()) return;
        uint8_t btn = cmd->payload[0];
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE,btn, 0, 0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE,0, 0, 0, 0, 0);
        break;
    }
    case CMD_MOUSE_PRESS:
        if (!tud_hid_ready()) return;
        g_state.mouse_buttons |= cmd->payload[0];
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE,g_state.mouse_buttons,
                               0, 0, 0, 0);
        break;
    case CMD_MOUSE_RELEASE:
        if (!tud_hid_ready()) return;
        g_state.mouse_buttons &= ~cmd->payload[0];
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE,g_state.mouse_buttons,
                               0, 0, 0, 0);
        break;
    case CMD_MOUSE_SCROLL:
        if (!tud_hid_ready()) return;
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE,g_state.mouse_buttons,
                               0, 0, (int8_t)cmd->payload[0], 0);
        break;
    case CMD_KEY_PRESS: {
        if (!tud_hid_ready()) return;
        uint8_t code = cmd->payload[0];
        for (int i = 0; i < 6; i++) {
            if (g_state.kb_keys[i] == 0) {
                g_state.kb_keys[i] = code;
                break;
            }
        }
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD,g_state.kb_modifier,
                                  g_state.kb_keys);
        break;
    }
    case CMD_KEY_RELEASE: {
        if (!tud_hid_ready()) return;
        uint8_t code = cmd->payload[0];
        for (int i = 0; i < 6; i++) {
            if (g_state.kb_keys[i] == code) {
                g_state.kb_keys[i] = 0;
            }
        }
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD,g_state.kb_modifier,
                                  g_state.kb_keys);
        break;
    }
    case CMD_KEY_TAP: {
        if (!tud_hid_ready()) return;
        uint8_t code = cmd->payload[0];
        uint8_t count = cmd->payload[1];
        for (int i = 0; i < count; i++) {
            uint8_t tmp[6] = { code, 0, 0, 0, 0, 0 };
            tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD,0, tmp);
            vTaskDelay(pdMS_TO_TICKS(10));
            tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD,0, NULL);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        break;
    }
    case CMD_KEY_TYPE: {
        if (!tud_hid_ready()) return;
        uint8_t count = cmd->payload[0];
        const char *str = (const char *)cmd->payload + 1;
        for (int i = 0; i < count; i++) {
            char c = str[i];
            if (c > 127) continue;
            uint8_t hid_code = ascii_to_hid[(uint8_t)c];
            if (hid_code == 0) continue;

            uint8_t modifier = char_needs_shift(c) ? 0x02 : 0x00;
            uint8_t keys[6] = { hid_code, 0, 0, 0, 0, 0 };
            tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD,modifier, keys);
            vTaskDelay(pdMS_TO_TICKS(5));
            tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD,0, NULL);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        break;
    }
    case CMD_SYNC:
        cdc_protocol_send_ack(cmd->seq_id, 0);
        break;
    case CMD_NOOP: {
        uint16_t delay_ms = cmd->payload[0] | (cmd->payload[1] << 8);
        if (delay_ms > 10000) delay_ms = 10000;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        break;
    }
    case CMD_RESET:
        esp_restart();
        break;
    case CMD_ACK_REQUEST:
        break;
    default:
        cdc_protocol_send_error(0x01);
        break;
    }

    g_state.last_seq = cmd->seq_id;
    g_state.last_action_ms = esp_timer_get_time() / 1000;
    cmd_tail = (cmd_tail + 1) % CMD_QUEUE_SIZE;
}

void hid_executor_task(void *arg) {
    hid_executor_init();
    while (1) {
        process_one_command();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void hid_executor_release_all(void) {
    memset(g_state.kb_keys, 0, sizeof(g_state.kb_keys));
    g_state.kb_modifier = 0;
    g_state.mouse_buttons = 0;
    if (tud_hid_ready()) {
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0, 0, 0, 0, 0);
    }
}

void hid_executor_get_state(hid_state_t *out) {
    *out = g_state;
    int pending = cmd_head - cmd_tail;
    if (pending < 0) pending += CMD_QUEUE_SIZE;
    g_state.cmd_queue_len = (uint8_t)pending;
}
