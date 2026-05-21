#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "cdc_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// 执行器状态
typedef struct {
    uint16_t last_seq;
    uint8_t  kb_modifier;       // 当前键盘 modifier 位
    uint8_t  kb_keys[6];        // 当前按下的键码
    uint8_t  mouse_buttons;     // 当前按下的鼠标按键
    int16_t  mouse_x;
    int16_t  mouse_y;
    uint8_t  kb_leds;           // Num/Caps/Scroll lock 状态
    uint8_t  cmd_queue_len;     // ring buffer 中等待的指令数
    uint32_t last_action_ms;    // 上次动作的时间戳
} hid_state_t;

void hid_executor_init(void);
int  hid_executor_submit(const cmd_t *cmd);
void hid_executor_task(void *arg);
void hid_executor_release_all(void);
void hid_executor_get_state(hid_state_t *out);

#ifdef __cplusplus
}
#endif
