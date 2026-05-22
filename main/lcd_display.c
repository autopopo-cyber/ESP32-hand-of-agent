#include "lcd_display.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "hid_executor.h"
#include "tusb.h"
#include "splash.h"
#include "font16.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

// Waveshare ESP32-S3-LCD-1.47 引脚
#define LCD_MOSI  GPIO_NUM_45
#define LCD_SCLK  GPIO_NUM_40
#define LCD_CS    GPIO_NUM_42
#define LCD_DC    GPIO_NUM_41
#define LCD_RST   GPIO_NUM_39
#define LCD_BL    GPIO_NUM_48
#define LCD_HOST  SPI2_HOST

// swap_xy 后: x(0..319)↓物理行, y(0..171)→物理列
#define LCD_H_RES 320
#define LCD_V_RES 172
#define LCD_BITS_PER_PIXEL 16

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;

static lcd_orientation_t s_orientation = LCD_ORIENT_DOWN;
#define BTN_GPIO GPIO_NUM_0

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

// draw_buf: 工作缓冲区, 始终 UP 坐标, 所有绘制写这里
uint16_t draw_buf[LCD_H_RES * LCD_V_RES];
// disp_buf: 显示缓冲区, 由 draw_buf 按方向变换后输出到 ST7789
static uint16_t disp_buf[LCD_H_RES * LCD_V_RES];

// ---- Framebuffer 基础操作 ----

static void fb_clear(uint16_t color) {
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) draw_buf[i] = color;
}

static void fb_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_H_RES) w = LCD_H_RES - x;
    if (y + h > LCD_V_RES) h = LCD_V_RES - y;
    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            draw_buf[row * LCD_H_RES + col] = color;
        }
    }
}

static void fb_draw_char(int x, int y, char c, uint16_t color, uint8_t scale) {
    if (c < ' ' || c > '~') c = '?';
    uint8_t idx = (uint8_t)(c - ' ');
    for (int row = 0; row < FONT_H; row++) {
        for (int col = 0; col < FONT_W; col++) {
            int bo = row * FONT_BPW + (col / 8);
            int bit = 7 - (col % 8);
            if (font_data[idx][bo] & (1 << bit)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + (FONT_H - 1 - row) * scale + sy;  // 垂直 (mirror_y 补偿)
                        int py = y + col * scale + sx;  // 水平
                        if (px >= 0 && px < LCD_H_RES && py >= 0 && py < LCD_V_RES)
                            draw_buf[py * LCD_H_RES + px] = color;
                    }
                }
            }
        }
    }
}

void fb_draw_string(int x, int y, const char *s, uint16_t color, uint8_t scale) {
    while (*s) {
        fb_draw_char(x, y, *s, color, scale);
        y += 6 * scale; // 文字沿水平方向(y)排列
        if (y > LCD_V_RES - 6) break;
        s++;
    }
}

static void fb_draw_progress(int x, int y, int w, int h, uint8_t pct) {
    fb_fill_rect(x, y, w, h, COLOR_PROGRESS_BG);
    int fill_w = (w * pct) / 100;
    if (fill_w > 0) fb_fill_rect(x, y, fill_w, h, COLOR_PROGRESS_FG);
}

void fb_flush(void) {
    int total = LCD_H_RES * LCD_V_RES;
    if (s_orientation == LCD_ORIENT_DOWN) {
        for (int i = 0; i < total; i++) {
            disp_buf[i] = draw_buf[total - 1 - i];
        }
    } else {
        memcpy(disp_buf, draw_buf, total * 2);
    }
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0,
        LCD_H_RES, LCD_V_RES, disp_buf);
}

// ---- 方向切换 & 按钮 ----

void lcd_set_orientation(lcd_orientation_t ori) {
    s_orientation = ori;
}

lcd_orientation_t lcd_get_orientation(void) {
    return s_orientation;
}

static const char *TAG_BTN = "btn";

void lcd_check_button(void) {
    static bool last_btn = true;
    bool btn = gpio_get_level(BTN_GPIO);
    if (last_btn && !btn) {
        s_orientation = (s_orientation == LCD_ORIENT_UP)
                        ? LCD_ORIENT_DOWN : LCD_ORIENT_UP;
        ESP_LOGI(TAG_BTN, "toggle → %s", s_orientation == LCD_ORIENT_UP ? "UP" : "DOWN");
        fb_flush();
    }
    last_btn = btn;
}

// ---- LCD 初始化 ----

void lcd_draw_splash(void) {
    memcpy(draw_buf, splash_image, SPLASH_W * SPLASH_H * 2);

    // 底部黑色条 + 文字 (16px 字体)
    fb_fill_rect(0, 0, 22, 172, ~0x0000 & 0xFFFF);
    fb_draw_string(4, 4, "ODA HID Bridge", ~0x07E0 & 0xFFFF, 1);

    fb_flush();
}

void lcd_load_random_splash(void) {
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "storage");
    if (!part) {
        ESP_LOGW("lcd", "No storage partition, using built-in splash");
        lcd_draw_splash();
        return;
    }

    uint32_t header[4];
    esp_err_t err = esp_partition_read(part, 0, header, sizeof(header));
    if (err != ESP_OK || header[0] != 0x4F444148) {
        ESP_LOGW("lcd", "Storage partition invalid (err=%d magic=%08lx)", err, header[0]);
        lcd_draw_splash();
        return;
    }

    uint32_t count = header[1];
    uint32_t img_w = header[2];
    uint32_t img_h = header[3];

    if (count == 0 || img_w != 172 || img_h != 320) {
        ESP_LOGW("lcd", "Bad image params: count=%lu w=%lu h=%lu", count, img_w, img_h);
        lcd_draw_splash();
        return;
    }

    uint32_t img_bytes = img_w * img_h * 2;
    uint32_t idx;
    nvs_handle_t nvs;
    if (nvs_open("splash", NVS_READWRITE, &nvs) == ESP_OK) {
        uint32_t boot_count = 0;
        nvs_get_u32(nvs, "boot", &boot_count);
        idx = boot_count % count;
        boot_count++;
        nvs_set_u32(nvs, "boot", boot_count);
        nvs_commit(nvs);
        nvs_close(nvs);
    } else {
        idx = esp_random() % count;
    }
    uint32_t offset = 16 + idx * img_bytes;

    ESP_LOGI("lcd", "Loading splash %lu/%lu (offset=%lu)", idx + 1, count, offset);

    err = esp_partition_read(part, offset, draw_buf, img_bytes);
    if (err == ESP_OK) {
        char label[64];
        snprintf(label, sizeof(label), "ODA HID #%lu/%lu", idx + 1, count);
        fb_fill_rect(0, 0, 22, 172, ~0x0000 & 0xFFFF);
        fb_draw_string(4, 4, label, ~0x07E0 & 0xFFFF, 1);
        fb_flush();
        return;
    }

    ESP_LOGW("lcd", "Read failed: err=%d", err);
    lcd_draw_splash();
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

    // BOOT 按钮 (GPIO0, 上拉, 按下=低电平)
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

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
        .spi_mode = 0,       // ST7789 使用 SPI Mode 0
        .pclk_hz = 10 * 1000 * 1000,  // 10 MHz (参考 Waveshare 示例)
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io_handle));

    // ST7789 Panel
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,   // 匹配 ESP32 小端
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle));

    // 初始化序列
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    // ST7789 默认开启反相，需要显式关闭
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, true));
    // ST7789 GRAM 240x320, 物理屏幕 172x320
    // swap_xy 后: GRAM 320x240, 172 列→172 行(row gap=34), 320 行→320 列(col gap=0)
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 34));

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
    lcd_load_random_splash();

    while (1) {
        lcd_check_button();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
