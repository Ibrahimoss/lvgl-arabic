/**
 * @file main.c
 * Firmware entry: board bring-up, input devices, then the LVGL loop (LVGL v6.1.2).
 *
 * The board (QEMU or the ESP32-2432S028R "Cheap Yellow Display") is picked in
 * menuconfig, see main/Kconfig.projbuild; only board_*.c differ.
 * The serial console is always a keyboard as well (uart_keypad.c).
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "app.h"
#include "board.h"
#include "uart_keypad.h"

static const char * TAG = "main";

/*Ctrl+R on the serial console hides or shows the target row*/
static void command(uint8_t ctrl)
{
    if(ctrl == 0x12) app_toggle_target();
}

void app_main(void)
{
    ESP_LOGI(TAG, "LVGL %d.%d.%d, %dx%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
             LV_HOR_RES_MAX, LV_VER_RES_MAX);

    lv_init(); /*the tick comes from esp_timer through LV_TICK_CUSTOM in lv_conf.h*/
    board_display_init();
    board_touch_init(); /*a touch press is a pointer: it focuses and clicks what it hits*/

    /*The keypad drives whichever widget of this group is focused*/
    lv_group_t * group = lv_group_create();
    lv_indev_set_group(uart_keypad_create(), group);
    uart_keypad_set_command_cb(command);

    app_create(group);

    /*All LVGL calls happen in this task, so no locking is needed*/
    while(1) {
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
