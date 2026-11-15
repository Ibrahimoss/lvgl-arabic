/**
 * @file arabic_term.h
 * Patch B: Arabic the way the Arabic terminals of the 1980s handled it (IBM
 * 3270 Arabic, NCR): shape while typing, and store what the screen shows.
 *
 * The text area holds the text as it is drawn: letters already in their
 * joined forms (Arabic Presentation Forms-B, U+FE80-U+FEFC) and in display
 * order, so stock LVGL draws it as it is. Each keystroke is shaped when it
 * is typed ("contextual shape determination" on input): an Arabic letter goes
 * in left of the caret in the form its neighbours call for, and the letter
 * typed before it is reshaped. Digits and Latin letters open a "push"
 * segment that grows to the right until the next Arabic letter, as with the
 * terminals' autopush; spaces and punctuation typed while pushing stay in it
 * ("3.14", "10:30", "ESP32 2026"), and the ones at its end move to its left
 * when an Arabic letter ends it.
 *
 * What is stored is not what was typed: arabic_term_get_text() turns it back
 * into plain letters in typing order. lv_ta_set_text() takes stored text
 * (arabic_term_to_display() makes it from typed text).
 *
 * Limits: one line; the field is always right to left; at most AR_MAX_CHARS
 * characters. Moving the caret ends the push segment. Spaces or punctuation
 * typed after a number or Latin text, with nothing typed after them, read
 * back in front of it (the stored text is the same as when they were typed
 * first). Where Unicode's BiDi algorithm orders text differently from a push
 * segment, the segment wins and keeps typing order: two numbers separated by a
 * space, "!" or a bracket, a date with hyphens after Arabic letters (Unicode:
 * 28-10-2026), Arabic-Indic digits next to Latin letters. No harakat. Max length, accepted
 * characters, password mode and text selection are turned off. The
 * LV_EVENT_INSERT text cannot be replaced or refused. lv_ta_del_char() called
 * directly deletes on the wrong side (use the Backspace key or
 * arabic_term_backspace()). A copy made with lv_ta_create(parent, field)
 * drops its events until arabic_term_attach(). An LV_EVENT_INSERT or
 * LV_EVENT_VALUE_CHANGED handler must not both type into another text area
 * and delete this one (the stock lv_ta would then go on with a deleted
 * object).
 */

#ifndef ARABIC_TERM_H
#define ARABIC_TERM_H

#include <stddef.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Make a plain one-line lv_ta a terminal-style Arabic field. Replaces its
 * event and signal functions; `user_cb` gets LV_EVENT_INSERT with the raw
 * input, LV_EVENT_VALUE_CHANGED after every edit, and the other events.
 * Up to 4 text areas.
 */
void arabic_term_attach(lv_obj_t * ta, lv_event_cb_t user_cb);

/** Delete the character typed before the caret (the Backspace key) */
void arabic_term_backspace(lv_obj_t * ta);

/**
 * The text in typing order with plain letters, converted back from what the
 * text area stores. Writes at most `size` bytes to `buf` (UTF-8) and returns it.
 */
const char * arabic_term_get_text(lv_obj_t * ta, char * buf, size_t size);

/**
 * Typed text to stored text: shaped and in display order, right to left (what
 * a host had to send such a terminal). `out` needs AR_MAX_CHARS * 4 + 1 bytes.
 */
void arabic_term_to_display(const char * txt, char * out);

/** lv_label_set_text() with arabic_term_to_display() */
void arabic_term_label_set_text(lv_obj_t * label, const char * txt);

#ifdef __cplusplus
}
#endif

#endif /*ARABIC_TERM_H*/
