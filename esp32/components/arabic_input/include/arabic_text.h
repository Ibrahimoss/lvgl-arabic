/**
 * @file arabic_text.h
 * Arabic shaping and right-to-left layout for display. No LVGL dependency.
 *
 * Input is logical text: code points in the order they were typed.
 * ar_layout() turns it into display text for a renderer that only draws
 * glyphs left to right (like LVGL v6):
 *   1. shaping: each Arabic letter becomes its isolated / initial / medial /
 *      final presentation form, lam + alef become one ligature
 *   2. bidi: levels from a subset of the Unicode Bidirectional Algorithm
 *      (weak types, bracket pairs, neutrals, implicit levels; no explicit
 *      embeddings or isolates, whose format characters are ignored), so Arabic
 *      runs right to left while Latin words and numbers inside it stay left
 *      to right
 *   3. line breaking at spaces for a given width, then per-line reordering
 * It also builds two maps between caret positions in the logical text and
 * caret spots in the display text, which is what lets an editor keep the
 * logical text as its source of truth and only show the display text.
 *
 * Caret spots: the display text of a line with n glyphs has n + 1 spots, 0 at
 * its left edge. Spots are numbered across lines like characters of the
 * display text, so a line's last spot is the index of the '\n' after it.
 * Where two directions meet, one logical position can be drawn at two spots;
 * to_display[] picks one, ar_layout_spot_after() the other.
 */

#ifndef ARABIC_TEXT_H
#define ARABIC_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest logical text ar_layout() handles, in code points. */
#ifndef AR_MAX_CHARS
#define AR_MAX_CHARS 128
#endif

/** Most display lines ar_layout() produces. Every line holds at least one
 *  character or ends a paragraph, so with AR_MAX_CHARS + 1 the text always
 *  fits: only text longer than AR_MAX_CHARS is ever truncated. */
#ifndef AR_MAX_LINES
#define AR_MAX_LINES (AR_MAX_CHARS + 1)
#endif

/** ar_layout_t.char_disp of a character that has no glyph (a paragraph's '\n'). */
#define AR_NO_GLYPH 0xFFFF

typedef enum {
    AR_DIR_LTR = 0,
    AR_DIR_RTL = 1,
    AR_DIR_AUTO = 2, /**< from the first strong character of each paragraph; RTL if none */
} ar_dir_t;

/** One glyph to draw. */
typedef struct {
    uint32_t cp;     /**< code point to draw (presentation form, ligature, mirrored bracket or as-is) */
    uint16_t src;    /**< first logical character it came from */
    uint8_t src_len; /**< logical characters it covers: 1, or 2 for a lam-alef ligature */
    uint8_t level;   /**< bidi level; odd = right to left */
} ar_glyph_t;

/** Advance width of `cp` when followed by `next` (0 at the end), in pixels. */
typedef int32_t (*ar_width_cb_t)(uint32_t cp, uint32_t next, void * ctx);

typedef struct {
    uint16_t display_start; /**< index of the line's first character in the display text */
    uint16_t count;         /**< characters on the line, without the '\n' after it */
    uint16_t src_start;     /**< logical range covered by the line */
    uint16_t src_end;
    uint8_t rtl;            /**< paragraph direction of the line */
} ar_line_t;

/** Result of ar_layout(): the display text and the caret maps. */
typedef struct {
    char utf8[AR_MAX_CHARS * 4 + AR_MAX_LINES + 1];    /**< display text, lines separated by '\n' */
    uint16_t display_len;                              /**< characters in utf8, '\n' included */
    uint16_t logical_len;
    uint16_t to_display[AR_MAX_CHARS + 1];             /**< logical caret -> caret spot (the higher bidi level wins) */
    uint16_t to_logical[AR_MAX_CHARS + AR_MAX_LINES + 1]; /**< caret spot -> logical caret, never inside a ligature */
    uint8_t caret_ok[AR_MAX_CHARS + 1];                /**< 0 inside a ligature (between lam and alef) */
    uint8_t char_rtl[AR_MAX_CHARS];                    /**< logical char is drawn right to left */
    uint16_t char_disp[AR_MAX_CHARS];                  /**< display index of the glyph a logical char is in */
    ar_line_t lines[AR_MAX_LINES];
    uint16_t line_cnt;
    bool truncated;                                    /**< text or lines did not fit the limits */
} ar_layout_t;

/**
 * Shape logical text: presentation forms and lam-alef ligatures.
 * @param text  logical code points
 * @param n     number of code points
 * @param out   at least n glyphs; levels are set to 0
 * @return number of glyphs written (n minus one per ligature)
 */
size_t ar_shape(const uint32_t * text, size_t n, ar_glyph_t * out);

/**
 * Resolve bidi levels of shaped glyphs of one paragraph. Zero-width and
 * embedding/isolate format characters are left out (UBA X9) and get the level
 * of the character before them.
 * @param base_rtl paragraph direction
 */
void ar_bidi_levels(ar_glyph_t * g, size_t n, bool base_rtl);

/**
 * Visual order of one line: order[k] is the glyph drawn k-th from the left.
 * Trailing spaces take the paragraph level (UBA rule L1).
 */
void ar_line_order(const ar_glyph_t * g, size_t n, bool base_rtl, uint16_t * order);

/** Direction of the first strong character; `fallback` if there is none. */
bool ar_text_is_rtl(const uint32_t * text, size_t n, bool fallback);

/** Mirrored bracket for right-to-left runs, or `cp` itself. */
uint32_t ar_mirror(uint32_t cp);

/**
 * Full layout: paragraphs split at paragraph separators (LF, CR, CRLF as one,
 * NEL, U+2029), shaping, bidi, line breaking and caret maps.
 * @param text      logical code points
 * @param n         number of code points (at most AR_MAX_CHARS are used)
 * @param dir       paragraph direction
 * @param max_width line width in pixels; <= 0 means no line breaking
 * @param width_cb  glyph widths (needed when max_width > 0)
 * @param out       result
 */
void ar_layout(const uint32_t * text, size_t n, ar_dir_t dir, int32_t max_width,
               ar_width_cb_t width_cb, void * ctx, ar_layout_t * out);

/**
 * Move a caret spot one step visually left (dir < 0) or right (dir > 0). Past
 * the visual edge of a line it continues on the next line (the key that reads
 * forward in the text's first paragraph) or the previous one, entering it at
 * the edge it moves away from; at the ends of the text it stays put (returns
 * `spot`). The logical caret of the new spot is to_logical[spot].
 */
uint16_t ar_layout_step(const ar_layout_t * lay, uint16_t spot, int dir);

/**
 * The spot right after the character before a logical caret, on that
 * character's side: where the caret belongs after typing or deleting. At the
 * start of a paragraph, the paragraph's reading-start edge.
 */
uint16_t ar_layout_spot_after(const ar_layout_t * lay, uint16_t caret);

/** Reading-start (end = false) or reading-end (end = true) spot of a display line. */
uint16_t ar_layout_line_edge(const ar_layout_t * lay, uint16_t line, bool end);

/** Display line a caret spot is on. */
uint16_t ar_layout_line_of(const ar_layout_t * lay, uint16_t spot);

/** UTF-8 to code points; stops at `max`. Malformed sequences become U+FFFD. @return number of code points */
size_t ar_utf8_decode(const char * s, uint32_t * out, size_t max);

/** One code point of UTF-8 (U+FFFD if malformed). @return bytes used, 0 at the end of the string */
size_t ar_utf8_next(const char * s, uint32_t * cp);

/** Code points to UTF-8; `out` needs 4*n+1 bytes. @return bytes written (without the '\0') */
size_t ar_utf8_encode(const uint32_t * cps, size_t n, char * out);

#ifdef __cplusplus
}
#endif

#endif /*ARABIC_TEXT_H*/
