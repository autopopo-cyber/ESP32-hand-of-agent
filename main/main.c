#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include "cdc_protocol.h"
#include "hid_executor.h"
#include "lcd_display.h"

#define APP_BUTTON (GPIO_NUM_0)
static const char *TAG = "oda_main";

/************* TinyUSB descriptors ****************/

enum {
    ITF_NUM_CDC_CTRL = 0,
    ITF_NUM_CDC_DATA = 1,
    ITF_NUM_HID      = 2,
    ITF_NUM_TOTAL    = 3
};

enum {
    CDC_NOTIF_EP = 0x81,
    CDC_OUT_EP   = 0x02,
    CDC_IN_EP    = 0x82,
    HID_EP       = 0x83,
};

enum {
    STRID_LANGID  = 0,
    STRID_MANUF   = 1,
    STRID_PRODUCT = 2,
    STRID_SERIAL  = 3,
    STRID_CDC     = 4,
    STRID_HID     = 5,
};

#define TUSB_DESC_TOTAL_LEN \
    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)

const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE))
};

const char* usb_strings[] = {
    (char[]){0x09, 0x04},
    "ODA",
    "ODA HID Bridge",
    "0001",
    "ODA CDC",
    "ODA HID",
};

static const uint8_t config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0,
        TUSB_DESC_TOTAL_LEN,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(
        ITF_NUM_CDC_CTRL, STRID_CDC,
        CDC_NOTIF_EP, 8,
        CDC_OUT_EP, CDC_IN_EP, 64),
    TUD_HID_DESCRIPTOR(
        ITF_NUM_HID, STRID_HID, false,
        sizeof(hid_report_descriptor),
        HID_EP, 16, 10),
};

/********* TinyUSB HID callbacks ***************/

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
    hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
    hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer; (void)bufsize;
}

/********* CDC callback ***************/

static void cdc_rx_callback(int itf, cdcacm_event_t *event) {
    (void)itf;
    if (event->type != CDC_EVENT_RX) return;

    uint8_t buf[256];
    size_t rx_size = 0;
    esp_err_t err = tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf, sizeof(buf), &rx_size);
    if (err != ESP_OK || rx_size == 0) return;

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
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "=== ODA HID Bridge starting ===");

    // Button
    const gpio_config_t btn_cfg = {
        .pin_bit_mask = BIT64(APP_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    gpio_config(&btn_cfg);

    // USB
    ESP_LOGI(TAG, "[init] USB");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = usb_strings,
        .string_descriptor_count = sizeof(usb_strings) / sizeof(usb_strings[0]),
        .external_phy = false,
        .configuration_descriptor = config_desc,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    // CDC
    ESP_LOGI(TAG, "[init] CDC");
    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = cdc_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));

    // Tasks
    ESP_LOGI(TAG, "[init] tasks");
    xTaskCreate(hid_executor_task, "hid_exec", 4096,
                NULL, configMAX_PRIORITIES - 2, NULL);
    xTaskCreate(lcd_render_task, "lcd", 4096,
                NULL, configMAX_PRIORITIES - 3, NULL);

    ESP_LOGI(TAG, "[ok] ready");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
