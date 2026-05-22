#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hid_executor.h"

#ifdef __cplusplus
extern "C" {
#endif

// 屏幕方向 (BOOT 按钮切换)
typedef enum {
    LCD_ORIENT_UP   = 0,  // USB口朝下，正向
    LCD_ORIENT_DOWN = 1,  // USB口朝上，180°旋转
} lcd_orientation_t;

void lcd_set_orientation(lcd_orientation_t ori);
lcd_orientation_t lcd_get_orientation(void);
void lcd_check_button(void);

// 渲染状态 — 由 PC 端通过 CDC 扩展指令更新
typedef struct {
    const char *app_name;
    const char *step_name;
    const char *vlm_model;
    uint8_t     progress;      // 0-100
    uint16_t    call_count;
    uint32_t    elapsed_ms;
    bool        usb_connected;
    bool        yolo_online;
    uint16_t    last_seq;
    uint8_t     cmd_pending;
} lcd_state_t;

void lcd_display_init(void);
void lcd_display_update(const lcd_state_t *state);
void lcd_draw_splash(void);
void lcd_render_task(void *arg);

// 暴露给测试用 (工作缓冲区, 始终 UP 坐标)
extern uint16_t draw_buf[320 * 172];
void fb_flush(void);
void fb_draw_string(int x, int y, const char *s, uint16_t color, uint8_t scale);

#ifdef __cplusplus
}
#endif
