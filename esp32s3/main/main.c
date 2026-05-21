#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "usb_descriptors.h"
#include "cdc_protocol.h"
#include "hid_executor.h"
#include "lcd_display.h"

static const char *TAG = "oda_main";

// CDC 接收回调 — 解析指令并提交到 HID 队列
static void cdc_rx_callback(int itf, cdcacm_event_t *event) {
    if (event->type != CDC_EVENT_RX) return;

    uint8_t buf[256];
    size_t rx_size = 0;
    if (tinyusb_cdcacm_read(itf, buf, sizeof(buf), &rx_size) != ESP_OK) return;

    size_t offset = 0;
    while (offset < rx_size) {
        cmd_t cmd;
        size_t consumed = 0;
        int parsed = cdc_parse_command(buf + offset, rx_size - offset,
                                       &cmd, &consumed);
        if (parsed > 0) {
            hid_executor_submit(&cmd);
        }
        offset += consumed;
        if (consumed == 0) break;
    }
}

void app_main(void) {
    // NVS 初始化 (TinyUSB 需要)
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_LOGI(TAG, "=== ODA HID Bridge starting ===");

    // Step 1: USB 初始化
    ESP_LOGI(TAG, "[stage] USB init");
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    // Step 2: CDC ACM 初始化
    ESP_LOGI(TAG, "[stage] CDC init");
    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = cdc_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));

    // Step 3: 创建 FreeRTOS 任务
    ESP_LOGI(TAG, "[stage] Tasks init");

    // HID 执行器任务 (高优先级: 保证 HID 响应)
    xTaskCreate(hid_executor_task, "hid_exec", 4096,
                NULL, configMAX_PRIORITIES - 2, NULL);

    // LCD 渲染任务 (普通优先级)
    xTaskCreate(lcd_render_task, "lcd", 4096,
                NULL, configMAX_PRIORITIES - 3, NULL);

    // Step 4: 初始化完成
    ESP_LOGI(TAG, "[ok] ODA HID Bridge ready");

    // main 线程空闲: 看门狗喂狗
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
