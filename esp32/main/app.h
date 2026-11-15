/**
 * @file app.h
 * Arabic input test scene. Board independent: main.c sets up the display and
 * input for QEMU (or a real board), then calls app_create().
 */

#ifndef APP_H
#define APP_H

#include "lvgl.h"

/**
 * Build the test screen.
 * @param group the keypad's group; the textarea and keyboard are added to it
 */
void app_create(lv_group_t * group);

/** Show/hide the hand-shaped "target" row (rebuilds the screen, text area starts empty). */
void app_toggle_target(void);

#endif /*APP_H*/
