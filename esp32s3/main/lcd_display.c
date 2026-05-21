#include "lcd_display.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "hid_executor.h"
#include "tusb.h"
#include "splash.h"
#include <stdio.h>
#include <string.h>

// Waveshare ESP32-S3-LCD-1.47 引脚
#define LCD_MOSI  GPIO_NUM_45
#define LCD_SCLK  GPIO_NUM_40
#define LCD_CS    GPIO_NUM_42
#define LCD_DC    GPIO_NUM_41
#define LCD_RST   GPIO_NUM_39
#define LCD_BL    GPIO_NUM_48
#define LCD_HOST  SPI2_HOST

#define LCD_H_RES 172
#define LCD_V_RES 320
#define LCD_BITS_PER_PIXEL 16

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;

// 颜色定义 (RGB565)
#define COLOR_BG          0x0000
#define COLOR_STATUS_RUN  0x07E0  // 绿
#define COLOR_STATUS_WAIT 0x001F  // 蓝
#define COLOR_STATUS_ERR  0xFFE0  // 黄
#define COLOR_TEXT        0xFFFF  // 白
#define COLOR_TEXT_DIM    0x8410  // 灰
#define COLOR_CARD_BG     0x18E3  // 深蓝灰
#define COLOR_PROGRESS_BG 0x2104  // 深绿灰
#define COLOR_PROGRESS_FG 0x07E0  // 绿

static uint16_t fb[LCD_H_RES * LCD_V_RES];

// ---- 5x7 字体表 (ASCII 32-126, 95 chars × 5 bytes) ----
static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // SPACE
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x00, 0x08, 0x14, 0x22, 0x41}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x41, 0x22, 0x14, 0x08, 0x00}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x01, 0x01}, // F
    {0x3E, 0x41, 0x41, 0x51, 0x32}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x03, 0x04, 0x78, 0x04, 0x03}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x00, 0x7F, 0x41, 0x41}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
    {0x41, 0x41, 0x7F, 0x00, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
    {0x08, 0x14, 0x54, 0x54, 0x3C}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
    {0x00, 0x7F, 0x10, 0x28, 0x44}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
    {0x00, 0x08, 0x36, 0x41, 0x00}, // {
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // |
    {0x00, 0x41, 0x36, 0x08, 0x00}, // }
    {0x08, 0x08, 0x2A, 0x1C, 0x08}, // ~
};

// ---- Framebuffer 基础操作 ----

static void fb_clear(uint16_t color) {
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) fb[i] = color;
}

static void fb_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_H_RES) w = LCD_H_RES - x;
    if (y + h > LCD_V_RES) h = LCD_V_RES - y;
    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            fb[row * LCD_H_RES + col] = color;
        }
    }
}

static void fb_draw_char(int x, int y, char c, uint16_t color, uint8_t scale) {
    if (c < ' ' || c > '~') c = '?';
    uint8_t idx = (uint8_t)(c - ' ');
    for (int row = 0; row < 7; row++) {
        uint8_t line = font5x7[idx][(row < 5) ? row : 4];
        // Use last row data for rows 5-6 (descender area), but only top 5 are populated
        if (row >= 5) line = 0; // No descenders in this font
        for (int col = 0; col < 5; col++) {
            if (line & (1 << (4 - col))) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        int py = y + row * scale + sy;
                        if (px >= 0 && px < LCD_H_RES && py >= 0 && py < LCD_V_RES)
                            fb[py * LCD_H_RES + px] = color;
                    }
                }
            }
        }
    }
}

static void fb_draw_string(int x, int y, const char *s, uint16_t color, uint8_t scale) {
    while (*s) {
        fb_draw_char(x, y, *s, color, scale);
        x += 6 * scale; // 5px width + 1px spacing
        if (x > LCD_H_RES - 6) break;
        s++;
    }
}

static void fb_draw_progress(int x, int y, int w, int h, uint8_t pct) {
    fb_fill_rect(x, y, w, h, COLOR_PROGRESS_BG);
    int fill_w = (w * pct) / 100;
    if (fill_w > 0) fb_fill_rect(x, y, fill_w, h, COLOR_PROGRESS_FG);
}

static void fb_flush(void) {
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0,
        LCD_H_RES, LCD_V_RES, fb);
}

// ---- LCD 初始化 ----

static void lcd_draw_splash(void) {
    // 全屏 splash 图片
    memcpy(fb, splash_image, SPLASH_W * SPLASH_H * 2);

    // 底部半透明文字覆盖
    fb_fill_rect(0, LCD_V_RES - 36, LCD_H_RES, 36, 0x0000);
    fb_draw_string(8, LCD_V_RES - 28, "ODA HID Bridge", 0x07E0, 1);

    fb_flush();
}

void lcd_display_init(void) {
    // Backlight GPIO
    gpio_config_t bl_cfg = {
        .pin_bit_mask = (1ULL << LCD_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(LCD_BL, 0); // 初始化时关背光

    // SPI 总线
    spi_bus_config_t spi_cfg = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &spi_cfg, SPI_DMA_CH_AUTO));

    // Panel IO (3 线 SPI)
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,  // 40 MHz
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io_handle));

    // ST7789 Panel
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle));

    // 初始化序列
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));

    // 开显示和背光
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    gpio_set_level(LCD_BL, 1);

    // 初始黑屏
    fb_clear(COLOR_BG);
    fb_flush();
}

// ---- 状态 UI 渲染 (4 区布局) ----

void lcd_display_update(const lcd_state_t *state) {
    fb_clear(COLOR_BG);

    // 1. 顶部状态栏 (y=0, h=20)
    uint16_t status_color = state->usb_connected ? COLOR_STATUS_RUN : COLOR_STATUS_ERR;
    fb_fill_rect(0, 0, LCD_H_RES, 20, status_color);
    char buf[64];
    snprintf(buf, sizeof(buf), "ODA LIVE %s",
             state->usb_connected ? "" : "!");
    fb_draw_string(3, 2, buf, COLOR_BG, 1);

    // 2. 任务信息区 (y=24 onwards)
    int y = 30;

    fb_draw_string(3, y, "CONNECTED",
        state->usb_connected ? COLOR_STATUS_RUN : COLOR_STATUS_ERR, 1);
    y += 14;

    snprintf(buf, sizeof(buf), "step: %s",
             state->step_name ? state->step_name : "---");
    fb_draw_string(3, y, buf, COLOR_TEXT, 1);
    y += 14;

    snprintf(buf, sizeof(buf), "target: %s",
             state->app_name ? state->app_name : "---");
    fb_draw_string(3, y, buf, COLOR_TEXT, 1);
    y += 14;

    snprintf(buf, sizeof(buf), "vlm: %s",
             state->vlm_model ? state->vlm_model : "---");
    fb_draw_string(3, y, buf, COLOR_TEXT_DIM, 1);
    y += 14;

    snprintf(buf, sizeof(buf), "calls: %u | time: %lus",
             state->call_count, (unsigned long)(state->elapsed_ms / 1000));
    fb_draw_string(3, y, buf, COLOR_TEXT_DIM, 1);
    y += 20;

    // 进度条
    fb_draw_progress(3, y, LCD_H_RES - 6, 10, state->progress);
    y += 4;
    snprintf(buf, sizeof(buf), "%u%%", state->progress);
    fb_draw_string(LCD_H_RES / 2 - 10, y, buf, COLOR_TEXT, 1);
    y += 24;

    // 3. App Card
    fb_fill_rect(3, y, LCD_H_RES - 6, 40, COLOR_CARD_BG);
    fb_draw_string(10, y + 12, state->app_name ? state->app_name : "NO APP",
                   COLOR_TEXT, 2);
    y += 50;

    // 4. 底部状态条 (y=290, h=30)
    fb_fill_rect(0, 290, LCD_H_RES, 30, COLOR_CARD_BG);
    snprintf(buf, sizeof(buf), "yolo:%s seq:%04u",
             state->yolo_online ? "*" : "-", state->last_seq);
    fb_draw_string(3, 296, buf, COLOR_TEXT_DIM, 1);
    snprintf(buf, sizeof(buf), "q:%u", state->cmd_pending);
    fb_draw_string(LCD_H_RES - 30, 296, buf, COLOR_TEXT, 1);

    fb_flush();
}

// ---- LCD 渲染任务 ----

void lcd_render_task(void *arg) {
    lcd_display_init();

    // 启动画面 (显示 2 秒)
    lcd_draw_splash();
    vTaskDelay(pdMS_TO_TICKS(2000));

    lcd_state_t state = {
        .app_name = "oda_hid",
        .step_name = "ready",
        .usb_connected = false,
        .yolo_online = false,
    };
    lcd_display_update(&state);

    while (1) {
        hid_state_t hid;
        hid_executor_get_state(&hid);

        // 更新状态
        state.last_seq = hid.last_seq;
        state.cmd_pending = hid.cmd_queue_len;
        state.usb_connected = tud_mounted();
        state.elapsed_ms = (esp_timer_get_time() / 1000) - hid.last_action_ms;

        // app_name / step_name / vlm_model 由 PC 端通过 CDC 扩展指令更新
        // (后续扩展阶段实现)

        lcd_display_update(&state);
        vTaskDelay(pdMS_TO_TICKS(100)); // 10fps
    }
}
