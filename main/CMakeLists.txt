idf_component_register(
    SRCS
        "main.c"
        "cdc_protocol.c"
        "hid_executor.c"
        "lcd_display.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_tinyusb
        driver
        esp_lcd
        esp_timer
        nvs_flash
        spi_flash
)
