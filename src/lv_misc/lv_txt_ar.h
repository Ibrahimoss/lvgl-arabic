/**
 * @file lv_txt_ar.h
 * Patch C: Arabic shaping while drawing. The text keeps one code per letter
 * (U+0621-U+064A, as typed); every place that measures or draws a letter
 * asks here for the form its neighbours call for: isolated, final, initial or
 * medial (Arabic Presentation Forms-B, U+FE80-U+FEF4), and lam + alef as one
 * ligature (U+FEF5-U+FEFC). This is the "contextual analysis" the character
 * generators of 1980s Arabic terminals did in hardware.
 *
 * Limits: no harakat (a mark between two letters stops them joining). The
 * text area's arrow keys stay LVGL's: they step through the text in typing
 * order, so in Arabic Left moves the caret right.
 */

#ifndef LV_TXT_AR_H
#define LV_TXT_AR_H

#include <stdbool.h>
#include <stdint.h>
#include "../lv_font/lv_font.h"

/** lv_txt_get_width() flag: the text is in display order (lv_bidi_process_paragraph() output) */
#define LV_TXT_FLAG_VISUAL 0x10

/**
 * The glyph to draw for `letter`.
 * @param prev  the letter before it in typing order (0: none)
 * @param next  the letter after it in typing order (0: none)
 * @return the glyph's code point; 0 for an alef drawn inside the lam-alef ligature
 */
uint32_t lv_txt_ar_glyph(uint32_t prev, uint32_t letter, uint32_t next);

/**
 * lv_txt_ar_glyph() for text walked in storage order (`visual` false) or in
 * display order (`visual` true: right-to-left letters have their neighbours
 * the other way round).
 * @param before the letter before `letter` in the text being walked (0: none)
 * @param after  the letter after it (0: none)
 */
uint32_t lv_txt_ar_glyph_in(uint32_t before, uint32_t letter, uint32_t after, bool visual);

/** Advance width of `letter` as drawn (0 for an alef inside lam-alef) */
uint16_t lv_txt_ar_width(const lv_font_t * font, uint32_t before, uint32_t letter, uint32_t after, bool visual);

#endif /*LV_TXT_AR_H*/
