#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lcd_display.h"

static const char *TAG = "splash";

void app_main(void) {
    ESP_LOGI(TAG, "=== LCD splash test ===");
    lcd_display_init();
    lcd_draw_splash();
    ESP_LOGI(TAG, "[ok] Splash displayed");
    while (1) {
        lcd_check_button();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
