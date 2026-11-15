/**
 * @file board_cyd.c
 * ESP32-2432S028R "Cheap Yellow Display": 2.8" 320x240 SPI panel (ILI9341 or
 * ST7789, detected at boot) and XPT2046 resistive touch on a second SPI bus.
 *
 * Pins (fixed on the board):
 *   panel  HSPI  SCLK 14  MOSI 13  MISO 12  CS 15  DC 2  backlight 21
 *   touch  VSPI  SCLK 25  MOSI 32  MISO 39  CS 33  IRQ 36
 *   RGB LED 4 / 16 / 17 (active low)
 */

#include "sdkconfig.h"
#if CONFIG_APP_BOARD_CYD

#include <assert.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"
#include "board.h"

#define LCD_H_RES LV_HOR_RES_MAX
#define LCD_V_RES LV_VER_RES_MAX
#define DRAW_BUF_LINES 40

#define LCD_HOST    SPI2_HOST
#define PIN_SCLK    14
#define PIN_MOSI    13
#define PIN_MISO    12
#define PIN_CS      15
#define PIN_DC      2
#define PIN_BL      21
#define LCD_PCLK_HZ (40 * 1000 * 1000)

#define TOUCH_HOST  SPI3_HOST
#define PIN_T_SCLK  25
#define PIN_T_MOSI  32
#define PIN_T_MISO  39
#define PIN_T_CS    33

/*bool Kconfig options are undefined when off*/
#ifdef CONFIG_APP_CYD_INVERT_COLORS
#define CYD_INVERT true
#else
#define CYD_INVERT false
#endif
#ifdef CONFIG_APP_CYD_ROTATE_180
#define CYD_ROTATE_180 true
#else
#define CYD_ROTATE_180 false
#endif
#ifdef CONFIG_APP_CYD_BGR
#define CYD_ORDER LCD_RGB_ELEMENT_ORDER_BGR
#else
#define CYD_ORDER LCD_RGB_ELEMENT_ORDER_RGB
#endif

static const char * TAG = "cyd";

/*********************************************************************
 * Display
 *********************************************************************/

static bool flush_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t * edata, void * ctx)
{
    (void)io;
    (void)edata;
    lv_disp_flush_ready((lv_disp_drv_t *)ctx);
    return false;
}

static void flush_cb(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_p)
{
    /*SPI panels take RGB565 big-endian: swap the bytes LVGL rendered*/
    uint16_t * px = (uint16_t *)color_p;
    uint32_t n = (uint32_t)(area->x2 - area->x1 + 1) * (uint32_t)(area->y2 - area->y1 + 1);
    for(uint32_t i = 0; i < n; i++) px[i] = (uint16_t)((px[i] >> 8) | (px[i] << 8));

    esp_lcd_panel_handle_t panel = drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
    /*lv_disp_flush_ready() comes from flush_done() when the DMA transfer ends*/
}

/*Reads the controller ID at a slow clock. true: ST7789, false: ILI9341 (or unknown).*/
static bool panel_is_st7789(void)
{
#if CONFIG_APP_CYD_PANEL_ST7789
    return true;
#elif CONFIG_APP_CYD_PANEL_ILI9341
    return false;
#else
    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t cfg = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = 4 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 2,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &cfg, &io));
    esp_lcd_panel_io_tx_param(io, 0x01, NULL, 0); /*software reset*/
    vTaskDelay(pdMS_TO_TICKS(150));

    uint8_t d3[4] = {0};
    uint8_t id[4] = {0};
    esp_lcd_panel_io_rx_param(io, 0xD3, d3, 4); /*ILI9341: xx 00 93 41*/
    esp_lcd_panel_io_rx_param(io, 0x04, id, 4); /*ST7789: 85 85 52, shifted by its dummy clock*/
    esp_lcd_panel_io_del(io);

    ESP_LOGI(TAG, "panel ID: D3h = %02X %02X %02X %02X, 04h = %02X %02X %02X %02X",
             d3[0], d3[1], d3[2], d3[3], id[0], id[1], id[2], id[3]);

    if(d3[2] == 0x93 && d3[3] == 0x41) return false;
    for(int i = 0; i < 2; i++) {
        if((id[i] == 0x85 && id[i + 1] == 0x85 && id[i + 2] == 0x52) ||
           (id[i] == 0x42 && id[i + 1] == 0xC2 && id[i + 2] == 0xA9)) return true;
    }
    return false; /*the 2432S028R ships with an ILI9341*/
#endif
}

void board_display_init(void)
{
    /*backlight on, RGB LED off*/
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_BL) | (1ULL << 4) | (1ULL << 16) | (1ULL << 17),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out);
    gpio_set_level(4, 1);
    gpio_set_level(16, 1);
    gpio_set_level(17, 1);
    gpio_set_level(PIN_BL, 0);

    const spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCLK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * DRAW_BUF_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    bool st7789 = panel_is_st7789();
    ESP_LOGI(TAG, "panel driver: %s", st7789 ? "ST7789" : "ILI9341");

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = LCD_PCLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = flush_done,
        .user_ctx = &disp_drv,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    esp_lcd_panel_handle_t panel = NULL;
    const esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = -1, /*not wired; the software reset is used*/
        .rgb_ele_order = CYD_ORDER,
        .bits_per_pixel = 16,
    };
    if(st7789) ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &dev_cfg, &panel));
    else ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &dev_cfg, &panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, CYD_INVERT));
    /*landscape: the controllers are portrait 240x320*/
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, CYD_ROTATE_180, CYD_ROTATE_180));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    const uint32_t buf_px = LCD_H_RES * DRAW_BUF_LINES;
    lv_color_t * buf = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(buf);
    static lv_disp_buf_t disp_buf;
    lv_disp_buf_init(&disp_buf, buf, NULL, buf_px);

    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.buffer = &disp_buf;
    disp_drv.flush_cb = flush_cb;
    disp_drv.user_data = panel;
    lv_disp_drv_register(&disp_drv);

    gpio_set_level(PIN_BL, 1);
}

/*********************************************************************
 * Touch (XPT2046)
 *********************************************************************/

static spi_device_handle_t touch_dev;

static uint16_t xpt_read(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 24,
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
    };
    t.tx_data[0] = cmd;
    spi_device_polling_transmit(touch_dev, &t);
    return (uint16_t)(((t.rx_data[1] << 8) | t.rx_data[2]) >> 3);
}

/*Median of five samples, so one noisy reading can't move the point*/
static uint16_t xpt_median(uint8_t cmd)
{
    uint16_t s[5];
    for(int i = 0; i < 5; i++) s[i] = xpt_read(cmd);
    for(int i = 1; i < 5; i++) {
        uint16_t v = s[i];
        int j = i - 1;
        while(j >= 0 && s[j] > v) {
            s[j + 1] = s[j];
            j--;
        }
        s[j + 1] = v;
    }
    return s[2];
}

static int32_t map_axis(int32_t raw, int32_t raw_min, int32_t raw_max, int32_t size)
{
    int32_t v = (raw - raw_min) * size / (raw_max - raw_min);
    if(v < 0) v = 0;
    if(v >= size) v = size - 1;
    return v;
}

static bool touch_read(lv_indev_drv_t * drv, lv_indev_data_t * data)
{
    static lv_point_t last;
    static bool was_pressed;
    (void)drv;

    int32_t z = (int32_t)xpt_read(0xB0) + 4095 - (int32_t)xpt_read(0xC0);
    bool pressed = z >= CONFIG_APP_CYD_TOUCH_Z_MIN;

    if(pressed) {
        int32_t rx = xpt_median(0x90);
        int32_t ry = xpt_median(0xD0);
        int32_t x = map_axis(rx, CONFIG_APP_CYD_TOUCH_X_MIN, CONFIG_APP_CYD_TOUCH_X_MAX, LCD_H_RES);
        int32_t y = map_axis(ry, CONFIG_APP_CYD_TOUCH_Y_MIN, CONFIG_APP_CYD_TOUCH_Y_MAX, LCD_V_RES);
        if(CYD_ROTATE_180) {
            x = LCD_H_RES - 1 - x;
            y = LCD_V_RES - 1 - y;
        }
        last.x = (lv_coord_t)x;
        last.y = (lv_coord_t)y;
        if(!was_pressed) printf("[touch] raw x=%d y=%d z=%d -> (%d, %d)\n", (int)rx, (int)ry, (int)z, (int)x, (int)y);
    }
    was_pressed = pressed;

    data->point = last;
    data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    return false;
}

lv_indev_t * board_touch_init(void)
{
    const spi_bus_config_t bus = {
        .sclk_io_num = PIN_T_SCLK,
        .mosi_io_num = PIN_T_MOSI,
        .miso_io_num = PIN_T_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TOUCH_HOST, &bus, SPI_DMA_DISABLED));
    const spi_device_interface_config_t dev = {
        .clock_speed_hz = 2 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_T_CS,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(TOUCH_HOST, &dev, &touch_dev));

    static lv_indev_drv_t drv;
    lv_indev_drv_init(&drv);
    drv.type = LV_INDEV_TYPE_POINTER;
    drv.read_cb = touch_read;
    return lv_indev_drv_register(&drv);
}

#endif /*CONFIG_APP_BOARD_CYD*/
