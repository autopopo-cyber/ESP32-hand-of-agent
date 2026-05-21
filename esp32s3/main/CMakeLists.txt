idf_component_register(
    SRCS
        "main.c"
        "usb_descriptors.c"
        "cdc_protocol.c"
        "hid_executor.c"
        "lcd_display.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_tinyusb
        driver
        esp_lcd
        nvs_flash
        esp_timer
)
