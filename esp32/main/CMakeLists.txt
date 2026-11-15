# lv_font_arabic_16: DejaVu Sans letters plus the keyboard icons (LV_SYMBOL_*)
# the Arabic keyboard needs, made by lv_font_conv (command line at the top of
# the file).
idf_component_register(SRCS "main.c" "app.c" "uart_keypad.c" "font_arabic_16_icons.c"
                            "board_qemu.c" "board_cyd.c"
                       INCLUDE_DIRS "."
                       REQUIRES lvgl arabic_input esp_lcd esp_driver_uart esp_driver_spi esp_driver_gpio
                                esp_timer)
