/**
 * @file arabic_input.h
 * Arabic text input for LVGL v6: a double-buffered text area and labels
 * (the Arabic on-screen keyboard is in arabic_kb.h).
 *
 * The double buffer
 * -----------------
 * A stock lv_ta keeps one buffer and draws it as is, so Arabic comes out as
 * unjoined letters running left to right. An arabic_ta keeps two:
 *   - the logical buffer: the letters exactly as typed (U+06xx, typing order).
 *     Every edit (typing, backspace, delete, cursor moves) changes this one.
 *   - the display buffer: rebuilt from the logical buffer after every edit by
 *     ar_layout(): shaped presentation forms, lam-alef ligatures, right-to-left
 *     order, line breaks. This is what the lv_ta's label draws.
 * The caret is a logical position (where typing goes) plus the spot in the
 * display text where it is drawn. After typing or deleting it sits next to
 * the character before it; arrow keys move it one spot on the screen, and
 * past a line's edge onto the next or previous line in reading order.
 *
 * Because the display is always rebuilt from the original letters, shaping
 * can never "stick": deleting or inserting next to a letter reshapes it, and
 * arabic_ta_get_text() returns clean text for storage or sending.
 *
 * Input paths that work unchanged: lv_ta_add_char() / lv_ta_add_text() (a
 * keypad, any keyboard, your code), because the arabic_ta intercepts
 * LV_EVENT_INSERT and applies the text to the logical buffer. lv_ta_set_text()
 * also works: the new text becomes the logical text. Keys
 * (LV_KEY_LEFT/RIGHT/UP/DOWN/HOME/END/BACKSPACE/DEL) and taps are handled
 * through the text area's signal function; lv_ta_set_cursor_click_pos()
 * still turns taps off.
 *
 * Use the arabic_ta_* function instead of the stock one for:
 *   - lv_ta_del_char(): does nothing when the caret is at display position 0,
 *     which is the end of right-to-left text, nor, when a parent gets the text
 *     area's events (lv_obj_set_parent_event()), when the deletion would leave
 *     the caret there (arabic_ta_backspace())
 *   - lv_ta_del_char_forward(): steps right on the screen, which is not the
 *     next character in right-to-left text or at the end of a wrapped line,
 *     then works like lv_ta_del_char() (arabic_ta_delete_forward())
 *   - lv_ta_set_one_line(): call arabic_ta_set_one_line()
 *   - copying: arabic_ta_create(parent, copy). An lv_ta_create(parent,
 *     an arabic_ta) copy drops its events until arabic_ta_attach(), which
 *     takes the source's text while the source still exists.
 * The stock cursor functions (lv_ta_cursor_*(), lv_ta_set_cursor_pos()) work
 * on screen positions; the caret follows them.
 *
 * Limits: AR_MAX_CHARS characters per text area: a keystroke or text that
 * does not fit all of it is refused; text set with arabic_ta_set_text() or
 * lv_ta_set_text() is cut to fit. One alignment for the whole text area, from
 * the direction of its first paragraph. No password mode or text selection;
 * max length and accepted characters are ignored. The LV_EVENT_INSERT text
 * cannot be replaced. Combining marks (harakat) are not positioned, and a mark
 * between lam and alef stops the لا ligature. Hebrew is reversed a second time
 * by LVGL's own BiDi when LV_USE_BIDI is 1. At a paragraph edge next to a
 * number or Latin word, the arrow keys and arabic_ta_set_cursor() draw that
 * edge's logical position next to the number (HOME/END draw it at the edge).
 * A parent with lv_obj_set_parent_event() sees LV_EVENT_VALUE_CHANGED before
 * LV_EVENT_INSERT (and one more VALUE_CHANGED after it when its INSERT
 * handler types into another text area), and an input device's feedback
 * callback one extra LV_EVENT_VALUE_CHANGED per edit. An LV_EVENT_VALUE_CHANGED handler must
 * not both type into another text area and delete this one (the stock lv_ta
 * would then go on with a deleted object). Each text area takes about 4.8 KB
 * of LVGL memory (LV_MEM_SIZE).
 */

#ifndef ARABIC_INPUT_H
#define ARABIC_INPUT_H

#include "lvgl.h"
#include "arabic_text.h"
#include "arabic_kb.h"

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------
 * Text area
 *--------------------------------------------------------------------*/

/**
 * Create an lv_ta that edits Arabic (and mixed) text.
 * @param copy  NULL, an lv_ta or an arabic_ta to copy (text, style, settings, event callback)
 */
lv_obj_t * arabic_ta_create(lv_obj_t * parent, const lv_obj_t * copy);

/**
 * Turn an existing lv_ta into an arabic_ta; its text becomes the logical text
 * (for an lv_ta copied from an arabic_ta: the source's logical text; if the
 * source is gone, nothing happens and a warning is logged). Uses the object's
 * user_data: it must be NULL before (or come from copying an arabic_ta), and
 * must not be changed afterwards. Replaces its signal functions (wrapping
 * them afterwards is fine).
 */
void arabic_ta_attach(lv_obj_t * ta);

/**
 * The arabic_ta uses the text area's event callback itself; register yours
 * here instead of lv_obj_set_event_cb() (or, with lv_obj_set_event_cb(), call
 * the callback you replaced). You get LV_EVENT_INSERT with the raw input
 * (before it is applied), LV_EVENT_VALUE_CHANGED after every edit, and all
 * other events unchanged. A callback set with lv_obj_set_event_cb() also sees
 * the text area's internal events: one more LV_EVENT_VALUE_CHANGED per edit,
 * and one each time the text is laid out again (size, style, direction).
 */
void arabic_ta_set_event_cb(lv_obj_t * ta, lv_event_cb_t cb);

/** Replace the logical text (UTF-8). The caret goes to the end. */
void arabic_ta_set_text(lv_obj_t * ta, const char * txt);

/** Logical text: what was typed, in typing order, plain letters (UTF-8). */
const char * arabic_ta_get_text(const lv_obj_t * ta);

/** Display text: what is drawn (same as lv_ta_get_text()). */
const char * arabic_ta_get_display_text(const lv_obj_t * ta);

/** Paragraph direction (default AR_DIR_AUTO: from the first strong letter, RTL if none). */
void arabic_ta_set_dir(lv_obj_t * ta, ar_dir_t dir);

/** lv_ta_set_one_line() for an arabic_ta: also lays the text out again. In
 *  one-line mode typed line breaks and paragraph separators are left out. */
void arabic_ta_set_one_line(lv_obj_t * ta, bool en);

/** Caret position in the logical text (0 .. number of characters). Setting it
 *  between lam and alef moves it after the alef. */
uint16_t arabic_ta_get_cursor(const lv_obj_t * ta);
void arabic_ta_set_cursor(lv_obj_t * ta, uint16_t pos);

/** Move the caret one step visually to the left / right. */
void arabic_ta_cursor_left(lv_obj_t * ta);
void arabic_ta_cursor_right(lv_obj_t * ta);

/** Delete the character before the caret (logically), like LV_KEY_BACKSPACE. */
void arabic_ta_backspace(lv_obj_t * ta);

/** Delete the character after the caret (logically), like LV_KEY_DEL. */
void arabic_ta_delete_forward(lv_obj_t * ta);

/** The layout behind the display buffer (caret maps, lines), for debugging. */
const ar_layout_t * arabic_ta_get_layout(const lv_obj_t * ta);

/*--------------------------------------------------------------------
 * Label
 *--------------------------------------------------------------------*/

/**
 * Set a label's text from logical UTF-8: shaped and ordered for display.
 * Right-to-left text is right aligned. With LV_LABEL_LONG_BREAK or
 * LV_LABEL_LONG_DOT the label's width is used for line breaking: set the
 * width (and long mode) first. Up to AR_MAX_CHARS characters and AR_MAX_LINES
 * lines are shown. LV_LABEL_LONG_DOT puts its dots at the right end, which is
 * the start of a right-to-left line, and LV_LABEL_LONG_SROLL(_CIRC) scrolls
 * from the left end: use LONG_BREAK, CROP or EXPAND for right-to-left text.
 */
void arabic_label_set_text(lv_obj_t * label, const char * txt);

#ifdef __cplusplus
}
#endif

#endif /*ARABIC_INPUT_H*/
