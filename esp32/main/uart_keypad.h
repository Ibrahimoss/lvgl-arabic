/**
 * @file uart_keypad.h
 * The serial console (UART0) as an LVGL keypad input device.
 */

#ifndef UART_KEYPAD_H
#define UART_KEYPAD_H

#include "lvgl.h"

/**
 * Install the UART0 driver and create a keypad indev fed by it.
 * printf() keeps working on the same UART.
 * @return the keypad; assign it to a group with lv_indev_set_group()
 */
lv_indev_t * uart_keypad_create(void);

/**
 * Control characters that are not keys (everything below 0x20 except Tab,
 * Enter, Backspace and Esc) are passed to this callback, for debug commands.
 * Runs in the LVGL task.
 */
void uart_keypad_set_command_cb(void (*cb)(uint8_t ctrl));

#endif /*UART_KEYPAD_H*/
