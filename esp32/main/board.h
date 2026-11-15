/**
 * @file board.h
 * Board support: everything that differs between QEMU and real hardware.
 */

#ifndef BOARD_H
#define BOARD_H

#include "lvgl.h"

/** Bring up the panel and register it with LVGL (call after lv_init()). */
void board_display_init(void);

/** Register the touchscreen with LVGL; NULL if the board has none. */
lv_indev_t * board_touch_init(void);

#endif /*BOARD_H*/
