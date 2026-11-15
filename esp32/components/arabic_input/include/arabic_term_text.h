/**
 * @file arabic_term_text.h
 * Patch B's text rules, without LVGL (arabic_term.c puts them in an lv_ta).
 * The stored text is what the screen
 * shows: joined forms (U+FE80-U+FEFC) in display order. In right-to-left text
 * the character typed before a letter is on its right and the one typed after
 * it on its left, so a letter's form comes from its two neighbours.
 */

#ifndef ARABIC_TERM_TEXT_H
#define ARABIC_TERM_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "arabic_utf8.h" /*AR_MAX_CHARS*/

#ifdef __cplusplus
extern "C" {
#endif

/** A terminal-style field: the stored text, the caret and the push segment */
typedef struct {
    uint32_t c[AR_MAX_CHARS]; /**< stored characters, left to right as drawn */
    uint16_t n;
    uint16_t caret;           /**< caret: before c[caret] */
    bool push;                /**< digits / Latin go in left to right at push_end */
    uint16_t push_start;      /**< the push segment is c[push_start .. push_end - 1] */
    uint16_t push_end;
} ar_term_t;

/** One typed character at the caret (control characters are ignored) */
void ar_term_type(ar_term_t * t, uint32_t c);

/** Backspace: delete the character typed before the caret. @return false if there is none */
bool ar_term_backspace(ar_term_t * t);

/** Delete: delete the character typed after the caret. @return false if there is none */
bool ar_term_delete(ar_term_t * t);

/** Typed text to stored text (the caret goes to the end of the text, its left edge) */
void ar_term_from_logical(ar_term_t * t, const uint32_t * in, size_t n);

/**
 * Stored text back to typed text: plain letters in typing order.
 * @param out at least 2 * AR_MAX_CHARS code points (a ligature becomes two)
 * @return the number of code points
 */
size_t ar_term_to_logical(const ar_term_t * t, uint32_t * out);

#ifdef __cplusplus
}
#endif

#endif /*ARABIC_TERM_TEXT_H*/
