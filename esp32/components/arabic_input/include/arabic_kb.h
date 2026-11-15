/**
 * @file arabic_kb.h
 * Arabic on-screen keyboard for LVGL v6, whose own keyboard (lv_kb) is English
 * only.
 */

#ifndef ARABIC_KB_H
#define ARABIC_KB_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create an lv_kb with Arabic, English and number layouts. Keys go to the text
 * area set with lv_kb_set_ta(): letters through lv_ta_add_text(), the arrow
 * keys and Backspace as LV_SIGNAL_CONTROL keys, as a keypad sends them;
 * OK sends LV_EVENT_APPLY to that text area. Do not call lv_kb_set_mode() on
 * it: that brings back the stock maps.
 * @param font  font for the key labels, shared by all Arabic keyboards: ASCII,
 *              Arabic U+0621-064A and U+0660-0669 with ، ؛ ؟, the presentation
 *              forms U+FE91 U+FEAE U+FECB U+FEF2 U+FEFB, and the
 *              LV_SYMBOL_BACKSPACE/NEW_LINE/LEFT/RIGHT/OK icons
 */
lv_obj_t * arabic_kb_create(lv_obj_t * parent, const lv_font_t * font);

#ifdef __cplusplus
}
#endif

#endif /*ARABIC_KB_H*/
