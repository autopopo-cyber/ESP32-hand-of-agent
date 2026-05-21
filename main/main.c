#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lcd_display.h"

static const char *TAG = "color_test";

void app_main(void) {
    ESP_LOGI(TAG, "=== Color bar test - portrait ===");

    lcd_display_init();

    const uint16_t colors[] = {
        0xF800, // 1 Red
        0x07E0, // 2 Green
        0x001F, // 3 Blue
        0xFFFF, // 4 White
        0x0000, // 5 Black
        0xF81F, // 6 Magenta
        0x07FF, // 7 Cyan
        0xFFE0, // 8 Yellow
    };

    for (int y = 0; y < 172; y++) {
        for (int x = 0; x < 320; x++) {
            int band = 7 - (x / 40);
            uint16_t c = ~colors[band] & 0xFFFF;
            fb[y * 320 + x] = c;
        }
    }

    for (int b = 0; b < 8; b++) {
        int x = (7 - b) * 40 + 8;
        char num[2] = { '1' + b, 0 };
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                fb_draw_string(x + dx, 30 + dy, num, 0x0000, 3);
        fb_draw_string(x, 30, num, 0xFFFF, 3);
    }

    fb_flush();
    ESP_LOGI(TAG, "[ok] 1=Red 2=Green 3=Blue 4=White 5=Black 6=Magenta 7=Cyan 8=Yellow");
    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
