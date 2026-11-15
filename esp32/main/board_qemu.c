/**
 * @file board_qemu.c
 * Espressif QEMU: virtual RGB panel, no touchscreen (input is the serial keyboard).
 */

#include "sdkconfig.h"
#if CONFIG_APP_BOARD_QEMU

#include <assert.h>
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_qemu_rgb.h"
#include "board.h"

#define LCD_H_RES LV_HOR_RES_MAX
#define LCD_V_RES LV_VER_RES_MAX

/*Partial rendering through a small internal-RAM buffer, like a real SPI panel*/
#define DRAW_BUF_LINES 40

static void flush_cb(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_p)
{
    esp_lcd_panel_handle_t panel = drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
    lv_disp_flush_ready(drv);
}

void board_display_init(void)
{
    esp_lcd_panel_handle_t panel = NULL;
    const esp_lcd_rgb_qemu_config_t panel_cfg = {
        .width = LCD_H_RES,
        .height = LCD_V_RES,
        .bpp = RGB_QEMU_BPP_16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_qemu(&panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    const uint32_t buf_px = LCD_H_RES * DRAW_BUF_LINES;
    lv_color_t * buf = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(buf);

    static lv_disp_buf_t disp_buf;
    lv_disp_buf_init(&disp_buf, buf, NULL, buf_px);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.buffer = &disp_buf;
    disp_drv.flush_cb = flush_cb;
    disp_drv.user_data = panel;
    lv_disp_drv_register(&disp_drv);
}

lv_indev_t * board_touch_init(void)
{
    return NULL;
}

#endif /*CONFIG_APP_BOARD_QEMU*/
