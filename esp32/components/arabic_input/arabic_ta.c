/**
 * @file arabic_ta.c
 * The double-buffered text area and arabic_label_set_text(). See arabic_input.h.
 *
 * An arabic_ta is a stock lv_ta with three hooks:
 *   - its event callback: lv_ta_add_char() / lv_ta_add_text() / lv_ta_del_char()
 *     announce every edit with LV_EVENT_INSERT before doing it. ta_event()
 *     cancels the stock edit (lv_ta_set_insert_replace(ta, "")), does it on
 *     the logical buffer and rebuilds the display buffer.
 *   - its signal function: cursor keys, Backspace and Delete work on the
 *     logical buffer; taps are placed with the fix's own layout.
 *   - the signal function of its scrollable part, which gets most taps.
 * The lv_ta itself only ever holds the display buffer (lv_ta_set_text()).
 *
 * The caret has two parts: the logical caret (where typing goes) and its
 * spot in the display text (where it is drawn, ar_layout_step() and friends).
 * Where two directions meet one logical position can be drawn at two spots,
 * so the spot is chosen by what happened last: after typing, next to the
 * character just typed; after an arrow key or a tap, where it moved to.
 */

#include "arabic_input.h"
#include <string.h>

/*********************************************************************
 * Types and state
 *********************************************************************/

/*Per text area*/
typedef struct ar_ta_s {
    struct ar_ta_s * next;           /*all states, see `live`*/
    lv_obj_t * self;                 /*the text area this state belongs to*/
    lv_signal_cb_t signal_orig;      /*the signal functions the hooks replaced*/
    lv_signal_cb_t scrl_signal_orig;
    uint32_t text[AR_MAX_CHARS];     /*logical buffer: code points in typing order*/
    uint16_t len;
    uint16_t caret;                  /*logical caret, 0..len: where typing goes*/
    uint16_t spot;                   /*where the caret is drawn: index into the display text*/
    lv_coord_t valid_x;              /*x the caret keeps on UP/DOWN, relative to the label*/
    ar_dir_t dir;
    bool busy;                       /*in show(): our own lv_ta_set_text() is not an edit*/
    bool forward_delete;             /*the next LV_KEY_DEL insert deletes after the caret*/
    bool own_delete;                 /*the next LV_KEY_DEL insert comes from send_delete()*/
    bool after_cr;                   /*the last input ended with a lone CR: a LF next completes it*/
    uint32_t serial;                 /*tags the text area's user_data, see attach()*/
    bool echo_armed;                 /*the last input, while the stock edit of it may still follow*/
    bool echo_del;
    uint16_t echo_spot;
    size_t echo_len;
    uint32_t echo_hash;
    lv_event_cb_t user_cb;
    char utf8[AR_MAX_CHARS * 4 + 1]; /*logical buffer as UTF-8, for arabic_ta_get_text()*/
    ar_layout_t lay;                 /*display buffer and caret maps*/
} ar_ta_t;

/*Every arabic_ta's state. Found by object, so nothing reads a state through
 *a pointer that may be stale (an lv_ta copy has its source's user_data).*/
static ar_ta_t * live;
static uint32_t serials;

/*An arabic_ta's user_data is a tag, not a pointer: an lv_ta_create() copy
 *inherits it, and attach() finds the source by it. Low byte 0xA5 cannot be an
 *aligned pointer; serials are never reused.*/
#define TAG_OF(serial) ((void *)(uintptr_t)(((uintptr_t)(serial) << 8) | 0xA5))
#define TAG_SERIAL(ud) ((((uintptr_t)(ud)) & 0xFF) == 0xA5 ? (uint32_t)((uintptr_t)(ud) >> 8) : 0)

/*Text areas whose user callbacks are running, to notice when a callback
 *deletes its text area (LV_SIGNAL_CLEANUP marks them)*/
#define WATCH_DEPTH 8
static struct {
    lv_obj_t * obj;
    bool deleted;
} watch[WATCH_DEPTH];
static uint8_t watch_n;

static lv_res_t ta_signal(lv_obj_t * ta, lv_signal_t sign, void * param);
static lv_res_t scrl_signal(lv_obj_t * scrl, lv_signal_t sign, void * param);
static void ta_event(lv_obj_t * ta, lv_event_t e);

static ar_ta_t * state_of(const lv_obj_t * ta)
{
    for(ar_ta_t * st = live; st; st = st->next) {
        if(st->self == ta) return st;
    }
    return NULL;
}

/*NULL if `ta` is not an arabic_ta*/
static ar_ta_t * get(const lv_obj_t * ta)
{
    return ta ? state_of(ta) : NULL;
}

static const lv_style_t * text_style(lv_obj_t * ta)
{
    lv_ta_ext_t * ext = lv_obj_get_ext_attr(ta);
    return lv_obj_get_style(ext->label);
}

/*UTF-8 to code points; line ends "\r\n" and "\r" become "\n"*/
static uint16_t decode_text(const char * txt, uint32_t * out)
{
    size_t n = ar_utf8_decode(txt, out, AR_MAX_CHARS);
    size_t k = 0;
    for(size_t i = 0; i < n; i++) {
        if(out[i] == '\r') {
            out[k++] = '\n';
            if(i + 1 < n && out[i + 1] == '\n') i++;
        }
        else {
            out[k++] = out[i];
        }
    }
    return (uint16_t)k;
}

/*Call the user's event callback. LV_RES_INV: it deleted the text area (and `st`).
 *Called directly, not with lv_event_send(): LVGL passes the event to the
 *parent and the input device's feedback callback itself.*/
static lv_res_t call_user(lv_obj_t * ta, ar_ta_t * st, lv_event_t e)
{
    lv_event_cb_t cb = st->user_cb;
    if(cb == NULL) return LV_RES_OK;
    if(watch_n == WATCH_DEPTH) return lv_event_send_func(cb, ta, e, lv_event_get_data()); /*LVGL's own tracking*/
    uint8_t w = watch_n++;
    watch[w].obj = ta;
    watch[w].deleted = false;
    cb(ta, e);
    watch_n--;
    return watch[w].deleted ? LV_RES_INV : LV_RES_OK;
}

/*********************************************************************
 * Display buffer
 *********************************************************************/

/*Advance width the way LVGL measures a line (lv_txt_get_width())*/
static int32_t glyph_width(uint32_t cp, uint32_t next, void * ctx)
{
    const lv_style_t * style = ctx;
    int32_t w = lv_font_get_glyph_width(style->text.font, cp, next);
    return w > 0 ? w + style->text.letter_space : 0;
}

/*Line width for ar_layout(): a line may end with a space that does not fit,
 *and a label that wraps would break such a line again, so keep a space in reserve*/
static int32_t wrap_width(lv_coord_t w, const lv_style_t * style)
{
    return w - lv_font_get_glyph_width(style->text.font, ' ', 0) - style->text.letter_space;
}

/*Rebuild the display buffer from the logical one. Only text longer than
 *AR_MAX_CHARS can be truncated, and the logical buffer never is.*/
static void layout(lv_obj_t * ta, ar_ta_t * st)
{
    const lv_style_t * style = text_style(ta);
    int32_t max_w = lv_ta_get_one_line(ta) ? 0 : wrap_width(lv_page_get_fit_width(ta), style);

    ar_layout(st->text, st->len, st->dir, max_w, glyph_width, (void *)style, &st->lay);
    if(st->caret > st->len) st->caret = st->len;
    ar_utf8_encode(st->text, st->len, st->utf8);
}

/*x of every caret spot of display line `li`, relative to the label, the way
 *lv_label_get_letter_pos() places the cursor. @return the number of spots*/
static uint16_t spot_xs(lv_obj_t * ta, ar_ta_t * st, uint16_t li, lv_coord_t * xs)
{
    lv_ta_ext_t * ext = lv_obj_get_ext_attr(ta);
    const lv_style_t * style = text_style(ta);
    const ar_line_t * L = &st->lay.lines[li];

    const char * txt = st->lay.utf8; /*the line's text: after `li` line breaks*/
    for(uint16_t k = 0; k < li; k++) txt = strchr(txt, '\n') + 1;

    lv_coord_t x = 0;
    uint32_t i = 0;
    xs[0] = 0;
    for(uint16_t k = 0; k < L->count; k++) {
        uint32_t letter = lv_txt_encoded_next(txt, &i);
        uint32_t next = k + 1 < L->count ? lv_txt_encoded_next(&txt[i], NULL) : 0;
        lv_coord_t w = lv_font_get_glyph_width(style->text.font, letter, next);
        if(w > 0) x += w + style->text.letter_space;
        xs[k + 1] = x;
    }
    lv_coord_t line_w = x > 0 ? x - style->text.letter_space : 0;

    lv_coord_t x0 = 0;
    lv_label_align_t align = lv_label_get_align(ext->label);
    if(align == LV_LABEL_ALIGN_RIGHT) x0 = lv_obj_get_width(ext->label) - line_w;
    else if(align == LV_LABEL_ALIGN_CENTER) x0 = lv_obj_get_width(ext->label) / 2 - line_w / 2;
    for(uint16_t k = 0; k <= L->count; k++) xs[k] += x0;
    return (uint16_t)(L->count + 1);
}

/*The spot of line `li` nearest to x (relative to the label)*/
static uint16_t spot_near(lv_obj_t * ta, ar_ta_t * st, uint16_t li, lv_coord_t x)
{
    lv_coord_t xs[AR_MAX_CHARS + 1];
    uint16_t n = spot_xs(ta, st, li, xs);
    uint16_t best = 0;
    for(uint16_t k = 1; k < n; k++) {
        if(LV_MATH_ABS(xs[k] - x) < LV_MATH_ABS(xs[best] - x)) best = k;
    }
    return (uint16_t)(st->lay.lines[li].display_start + best);
}

static lv_coord_t spot_x(lv_obj_t * ta, ar_ta_t * st, uint16_t spot)
{
    lv_coord_t xs[AR_MAX_CHARS + 1];
    uint16_t li = ar_layout_line_of(&st->lay, spot);
    spot_xs(ta, st, li, xs);
    return xs[spot - st->lay.lines[li].display_start];
}

/*Draw the caret at st->spot*/
static void place_cursor(lv_obj_t * ta, ar_ta_t * st, bool keep_x)
{
    lv_ta_ext_t * ext = lv_obj_get_ext_attr(ta);
    /*lv_ta_set_cursor_pos() does nothing (no scrolling either) when the index
     *is the same as before, but the text around it may have changed*/
    ext->cursor.pos = (uint16_t)(st->spot + 1);
    lv_ta_set_cursor_pos(ta, st->spot);
    if(!keep_x) st->valid_x = spot_x(ta, st, st->spot);
}

/*Put the display buffer and the caret into the lv_ta*/
static void show(lv_obj_t * ta, ar_ta_t * st)
{
    lv_ta_ext_t * ext = lv_obj_get_ext_attr(ta);
    const lv_style_t * style = text_style(ta);

    /*lv_ta_set_text() has to just take the text: with a max length or a list
     *of accepted characters it would type it in character by character (both
     *unsupported, so they are cleared); the old cursor index may be past the
     *end of the new text, and lv_ta reads the letter there; and its
     *VALUE_CHANGED is internal, so it must not reach the parent*/
    ext->accapted_chars = NULL;
    ext->max_length = 0;
    ext->cursor.pos = 0;
    uint8_t parent_event = ta->parent_event;
    ta->parent_event = 0;
    st->busy = true;
    lv_ta_set_text(ta, st->lay.utf8);
    ta->parent_event = parent_event;

    /*Right-to-left text is right aligned. In one-line mode the stock lv_ta only
     *scrolls left-aligned text (lv_ta_set_text_align()), so a line wider than
     *the box is left aligned instead and scrolls with the caret.*/
    bool right = st->lay.lines[0].rtl;
    if(right && lv_ta_get_one_line(ta)) {
        lv_coord_t w = lv_txt_get_width(st->lay.utf8, (uint16_t)strlen(st->lay.utf8), style->text.font,
                                        style->text.letter_space, LV_TXT_FLAG_NONE);
        if(w + 2 > lv_page_get_fit_width(ta)) right = false; /*+2: room for the caret*/
    }
    lv_label_align_t align = right ? LV_LABEL_ALIGN_RIGHT : LV_LABEL_ALIGN_LEFT;
    if(lv_label_get_align(ext->label) != align) lv_ta_set_text_align(ta, align);

    place_cursor(ta, st, false);
    st->busy = false;
}

/*After an edit of the logical buffer: the caret goes next to the character
 *before it, then show it and tell the user. LV_RES_INV: the user deleted the
 *text area.*/
static lv_res_t commit(lv_obj_t * ta, ar_ta_t * st)
{
    st->spot = ar_layout_spot_after(&st->lay, st->caret);
    show(ta, st);
    return lv_event_send(ta, LV_EVENT_VALUE_CHANGED, NULL);
}

/*The display buffer was changed behind our back (lv_ta_set_text(), or the
 *stock lv_ta_set_text() typing in character by character): that text is the
 *new logical text*/
static bool adopt_display_text(lv_obj_t * ta, ar_ta_t * st)
{
    if(strcmp(lv_ta_get_text(ta), st->lay.utf8) == 0) return false;
    st->after_cr = false;
    st->len = decode_text(lv_ta_get_text(ta), st->text);
    st->caret = st->len;
    layout(ta, st);
    st->spot = ar_layout_spot_after(&st->lay, st->caret);
    return true;
}

/*After ta_event() handled an LV_EVENT_INSERT, the stock lv_ta_add_char() /
 *lv_ta_add_text() / lv_ta_del_char() looks at the cancel again. A callback
 *that called lv_ta_add_text() on any text area has reset it, and then the
 *stock code edits the display buffer and sends LV_EVENT_VALUE_CHANGED. Such
 *an edit is the old display text with exactly that input at the caret.*/
static uint32_t hash_bytes(const char * s, size_t n)
{
    uint32_t h = 2166136261u; /*FNV-1a*/
    for(size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
    return h;
}

static void arm_echo(ar_ta_t * st, const char * in)
{
    st->echo_armed = true;
    st->echo_del = in[0] == LV_KEY_DEL && in[1] == '\0';
    st->echo_len = strlen(in);
    st->echo_hash = hash_bytes(in, st->echo_len); /*any length: the input itself is not kept*/
    st->echo_spot = st->spot;
}

static bool is_stock_echo(lv_obj_t * ta, ar_ta_t * st)
{
    const char * old = st->lay.utf8;
    const char * now = lv_ta_get_text(ta);
    size_t b = 0; /*byte offset of the caret spot*/
    uint32_t c;
    for(uint16_t i = 0; i < st->echo_spot; i++) b += ar_utf8_next(old + b, &c);

    if(st->echo_del) { /*lv_ta_del_char(): the character before the cursor*/
        if(st->echo_spot == 0) return false;
        size_t prev = 0;
        for(uint16_t i = 0; i + 1 < st->echo_spot; i++) prev += ar_utf8_next(old + prev, &c);
        return strncmp(now, old, prev) == 0 && strcmp(now + prev, old + b) == 0;
    }
    /*lv_ta_add_text() / lv_ta_add_char(): the input at the cursor*/
    if(strlen(now) != strlen(old) + st->echo_len) return false;
    return memcmp(now, old, b) == 0 && hash_bytes(now + b, st->echo_len) == st->echo_hash &&
           strcmp(now + b + st->echo_len, old + b) == 0;
}

/*Whether an event sent to `ta` goes on to a handler of an ancestor (parent_event)*/
static bool parent_gets_events(const lv_obj_t * ta)
{
    for(const lv_obj_t * o = ta; o->parent_event && o->par; o = o->par) {
        if(o->par->event_cb) return true;
    }
    return false;
}

/*********************************************************************
 * Caret
 *********************************************************************/

/*Caret at a display spot (arrow keys, taps); the logical caret follows*/
static void set_spot(lv_obj_t * ta, ar_ta_t * st, uint16_t spot, bool keep_x)
{
    if(spot > st->lay.display_len) spot = st->lay.display_len;
    st->spot = spot;
    st->caret = st->lay.to_logical[spot];
    st->after_cr = false;
    place_cursor(ta, st, keep_x);
}

/*The stock lv_ta moved its cursor itself (lv_ta_cursor_*() or
 *lv_ta_set_cursor_pos() called directly): take the caret from there*/
static void sync_from_display(lv_obj_t * ta, ar_ta_t * st)
{
    uint16_t d = lv_ta_get_cursor_pos(ta);
    if(d != st->spot) set_spot(ta, st, d, false);
}

/*Lay out again for a new width, style or direction*/
static void refresh(lv_obj_t * ta, ar_ta_t * st)
{
    sync_from_display(ta, st);
    layout(ta, st);
    st->spot = ar_layout_spot_after(&st->lay, st->caret);
    show(ta, st);
}

static void step(lv_obj_t * ta, ar_ta_t * st, int dir)
{
    sync_from_display(ta, st);
    uint16_t spot = ar_layout_step(&st->lay, st->spot, dir);
    if(spot != st->spot) set_spot(ta, st, spot, false); /*at an end of the text nothing changes*/
}

/*UP/DOWN: the nearest spot to the remembered x on the line above or below*/
static void vertical(lv_obj_t * ta, ar_ta_t * st, int dir)
{
    sync_from_display(ta, st);
    int li = ar_layout_line_of(&st->lay, st->spot) + dir;
    if(li < 0 || li >= st->lay.line_cnt) return;
    set_spot(ta, st, spot_near(ta, st, (uint16_t)li, st->valid_x), true);
}

/*HOME/END: start or end of the caret's line, at its visual edge*/
static void line_edge(lv_obj_t * ta, ar_ta_t * st, bool end)
{
    sync_from_display(ta, st);
    uint16_t li = ar_layout_line_of(&st->lay, st->spot);
    st->caret = end ? st->lay.lines[li].src_end : st->lay.lines[li].src_start;
    st->spot = ar_layout_line_edge(&st->lay, li, end);
    st->after_cr = false;
    place_cursor(ta, st, false);
}

/*A tap: the spot nearest to the point*/
static void tap(lv_obj_t * ta, ar_ta_t * st, lv_indev_t * indev)
{
    lv_ta_ext_t * ext = lv_obj_get_ext_attr(ta);
    if(indev == NULL || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return;
    if(ext->cursor.type == LV_CURSOR_NONE) return;

    const lv_style_t * style = text_style(ta);
    lv_point_t p;
    lv_area_t a;
    lv_indev_get_point(indev, &p);
    lv_obj_get_coords(ext->label, &a);
    lv_coord_t line_h = lv_font_get_line_height(style->text.font) + style->text.line_space;
    int li = (p.y - a.y1) / line_h;
    if(li < 0) li = 0;
    if(li >= st->lay.line_cnt) li = st->lay.line_cnt - 1;
    set_spot(ta, st, spot_near(ta, st, (uint16_t)li, p.x - a.x1), false);
}

static bool is_press_signal(lv_signal_t sign)
{
    return sign == LV_SIGNAL_PRESSED || sign == LV_SIGNAL_PRESSING || sign == LV_SIGNAL_PRESS_LOST ||
           sign == LV_SIGNAL_RELEASED;
}

/*Run a stock signal function with its tap-to-cursor code off (lv_ta's own
 *hit test goes through v6's BiDi, which does not know the display buffer),
 *then place the caret ourselves if tapping is enabled*/
static lv_res_t press_signal(lv_signal_cb_t orig, lv_obj_t * obj, lv_obj_t * ta, ar_ta_t * st, lv_signal_t sign,
                             void * param)
{
    lv_ta_ext_t * ext = lv_obj_get_ext_attr(ta);
    uint8_t click_pos = ext->cursor.click_pos;
    ext->cursor.click_pos = 0;
    lv_res_t res = orig(obj, sign, param);
    if(res != LV_RES_OK) return res;
    ext->cursor.click_pos = click_pos;
    if(sign == LV_SIGNAL_PRESSED && click_pos) tap(ta, st, param);
    return res;
}

/*********************************************************************
 * Editing the logical buffer
 *********************************************************************/

/*The next character of typed or pasted text that goes into the text: CRLF and
 *CR become one line break; control keys, and paragraph separators in one-line
 *mode, are left out. @return false at the end of the text*/
static bool next_input(const char ** p, bool one_line, uint32_t * c)
{
    size_t used;
    while((used = ar_utf8_next(*p, c)) > 0) {
        *p += used;
        if(*c == '\r') {
            *c = '\n';
            if(**p == '\n') (*p)++;
        }
        bool separator = *c == '\n' || *c == 0x85 || *c == 0x2029; /*bidi class B (U+001C-1E are controls)*/
        if(separator ? !one_line : (*c >= 0x20 && *c != 0x7F)) return true;
    }
    return false;
}

/*All of `txt` or nothing, if it does not fit AR_MAX_CHARS. @return the number of characters inserted*/
static uint16_t insert(lv_obj_t * ta, ar_ta_t * st, const char * txt)
{
    bool one_line = lv_ta_get_one_line(ta);
    /*CR and LF typed one at a time (lv_ta_add_char(), or lv_ta_set_text() with
     *a max length) are one line break, as in a stock lv_ta*/
    bool cr_lf = st->after_cr && txt[0] == '\n' && st->caret > 0 && st->text[st->caret - 1] == '\n';
    st->after_cr = false;
    if(cr_lf) txt++;

    const char * p = txt;
    uint32_t c;
    size_t k = 0;
    while(next_input(&p, one_line, &c)) k++;
    if(k == 0 || st->len + k > AR_MAX_CHARS) return 0;

    memmove(&st->text[st->caret + k], &st->text[st->caret], (st->len - st->caret) * sizeof(uint32_t));
    p = txt;
    for(size_t i = 0; i < k && next_input(&p, one_line, &c); i++) st->text[st->caret + i] = c;
    st->caret = (uint16_t)(st->caret + k);
    st->len = (uint16_t)(st->len + k);
    size_t n = strlen(txt);
    st->after_cr = !one_line && n > 0 && txt[n - 1] == '\r';
    return (uint16_t)k;
}

static bool delete_at(ar_ta_t * st, uint16_t pos)
{
    if(pos >= st->len) return false;
    memmove(&st->text[pos], &st->text[pos + 1], (st->len - pos - 1) * sizeof(uint32_t));
    st->len--;
    return true;
}

/*An LV_EVENT_INSERT: do it on the logical buffer. @return true if the text changed*/
static bool apply_insert(lv_obj_t * ta, ar_ta_t * st, const char * in, bool forward, bool stock_delete)
{
    if(!(in[0] == LV_KEY_DEL && in[1] == '\0')) {
        if(insert(ta, st, in) == 0) return false;
        layout(ta, st);
        return true;
    }

    /*lv_ta_del_char(), arabic_ta_backspace(), arabic_ta_delete_forward()*/
    st->after_cr = false;
    uint16_t caret = st->caret;
    if(forward ? caret >= st->len : caret == 0) return false;
    uint16_t pos = forward ? caret : (uint16_t)(caret - 1);
    uint32_t gone = st->text[pos];
    delete_at(st, pos);
    st->caret = pos;
    layout(ta, st);

    /*After the event the stock lv_ta_del_char() deletes the character before
     *its cursor if a parent's handler re-enabled the edit (see arm_echo()),
     *without looking at the cursor again: at display position 0 it would cut
     *at -1. Such a deletion is refused; the stock one is then undone.*/
    if(stock_delete && parent_gets_events(ta) && ar_layout_spot_after(&st->lay, st->caret) == 0) {
        memmove(&st->text[pos + 1], &st->text[pos], (st->len - pos) * sizeof(uint32_t));
        st->text[pos] = gone;
        st->len++;
        st->caret = caret;
        layout(ta, st);
        return false;
    }
    return true;
}

/*Announce a deletion the way lv_ta_del_char() does and let ta_event() do it.
 *lv_ta_del_char() itself stops at display position 0, which is where the end
 *of right-to-left text is drawn.*/
static lv_res_t send_delete(lv_obj_t * ta, ar_ta_t * st, bool forward)
{
    static const char del[2] = {LV_KEY_DEL, '\0'};
    sync_from_display(ta, st);
    if(forward ? st->caret >= st->len : st->caret == 0) return LV_RES_OK; /*nothing to delete: no event*/
    st->forward_delete = forward;
    st->own_delete = true;
    return lv_event_send(ta, LV_EVENT_INSERT, del);
}

/*********************************************************************
 * Hooks
 *********************************************************************/

static void ta_event(lv_obj_t * ta, lv_event_t e)
{
    ar_ta_t * st = get(ta);
    if(st == NULL) return; /*a copy made by lv_ta_create(parent, arabic_ta): see arabic_ta_attach()*/
    if(e != LV_EVENT_VALUE_CHANGED) st->echo_armed = false; /*a stock edit would have come first, see arm_echo()*/

    if(e == LV_EVENT_INSERT) {
        bool forward = st->forward_delete;
        bool own = st->own_delete;
        st->forward_delete = false;
        st->own_delete = false;
        if(st->busy) return; /*not an edit: show() is putting the display buffer in*/

        /*An edit is about to happen on the display buffer: cancel it first
         *(after the user's callback the text area may be gone), then do it on
         *the logical buffer. lv_ta_add_text() on any text area resets the
         *cancel, and the callbacks below may call it, so it is set again;
         *a parent's handler (parent_event) runs after this function, see arm_echo().*/
        const char * in = lv_event_get_data();
        lv_ta_set_insert_replace(ta, "");
        if(call_user(ta, st, e) != LV_RES_OK) return;
        lv_ta_set_insert_replace(ta, "");
        adopt_display_text(ta, st); /*the stock lv_ta_set_text() typing it in: it cleared the label first*/
        sync_from_display(ta, st);
        if(apply_insert(ta, st, in, forward, !own) && commit(ta, st) != LV_RES_OK) return;
        lv_ta_set_insert_replace(ta, "");
        if(!own) arm_echo(st, in); /*send_delete() has no stock edit after the event*/
        return;
    }

    if(e == LV_EVENT_VALUE_CHANGED) {
        if(st->busy) return; /*our own lv_ta_set_text()*/
        if(st->echo_armed) {
            st->echo_armed = false;
            if(is_stock_echo(ta, st)) { /*the cancelled stock edit happened after all: undo it*/
                show(ta, st);
                return; /*the user already had VALUE_CHANGED for this input*/
            }
        }
        if(adopt_display_text(ta, st)) show(ta, st); /*somebody called lv_ta_set_text()*/
    }

    call_user(ta, st, e);
}

static lv_res_t ta_signal(lv_obj_t * ta, lv_signal_t sign, void * param)
{
    /*Found by object, not with lv_obj_get_user_data(): that asserts the
     *object's type (LV_ASSERT_OBJ), which asks this function again*/
    ar_ta_t * st = state_of(ta);

    /*First, like every v6 signal function: the getters below ask for the type too*/
    if(sign == LV_SIGNAL_GET_TYPE) return st->signal_orig(ta, sign, param);

    if(sign == LV_SIGNAL_CONTROL) {
        switch(*(uint32_t *)param) {
            case LV_KEY_LEFT: step(ta, st, -1); return LV_RES_OK;
            case LV_KEY_RIGHT: step(ta, st, +1); return LV_RES_OK;
            case LV_KEY_UP: vertical(ta, st, -1); return LV_RES_OK;
            case LV_KEY_DOWN: vertical(ta, st, +1); return LV_RES_OK;
            case LV_KEY_HOME: line_edge(ta, st, false); return LV_RES_OK;
            case LV_KEY_END: line_edge(ta, st, true); return LV_RES_OK;
            case LV_KEY_BACKSPACE: return send_delete(ta, st, false);
            case LV_KEY_DEL: return send_delete(ta, st, true);
            default: return st->signal_orig(ta, sign, param); /*typed text: comes back as LV_EVENT_INSERT*/
        }
    }

    if(is_press_signal(sign)) return press_signal(st->signal_orig, ta, ta, st, sign, param);

    lv_res_t res = st->signal_orig(ta, sign, param);
    if(res != LV_RES_OK) return res;

    if(sign == LV_SIGNAL_CLEANUP) {
        /*LVGL still asks the type after this: from now on the stock function answers*/
        ta->signal_cb = st->signal_orig;
        for(uint8_t i = 0; i < watch_n; i++) {
            if(watch[i].obj == ta) watch[i].deleted = true;
        }
        for(ar_ta_t ** p = &live; *p; p = &(*p)->next) {
            if(*p == st) {
                *p = st->next;
                break;
            }
        }
        if(TAG_SERIAL(ta->user_data) == st->serial) ta->user_data = NULL;
        lv_mem_free(st);
    }
    else if(sign == LV_SIGNAL_STYLE_CHG) { /*new font or padding: new line breaks*/
        if(!st->busy) refresh(ta, st);
    }
    else if(sign == LV_SIGNAL_CORD_CHG) { /*new width, or lv_ta_set_one_line() changed the height*/
        const lv_area_t * old = param;
        if(!st->busy && (lv_obj_get_width(ta) != lv_area_get_width(old) || lv_obj_get_height(ta) != lv_area_get_height(old)))
            refresh(ta, st);
    }
    return res;
}

static lv_res_t scrl_signal(lv_obj_t * scrl, lv_signal_t sign, void * param)
{
    /*the text area outlives its scrollable part, so its state is still there*/
    lv_obj_t * ta = scrl->par;
    ar_ta_t * st = state_of(ta);
    if(sign == LV_SIGNAL_GET_TYPE || !is_press_signal(sign)) return st->scrl_signal_orig(scrl, sign, param);
    return press_signal(st->scrl_signal_orig, scrl, ta, st, sign, param);
}

/*********************************************************************
 * Text area API
 *********************************************************************/

/*Logical state of `src` into the new arabic_ta `st`*/
static void copy_state(lv_obj_t * ta, ar_ta_t * st, const ar_ta_t * src)
{
    memcpy(st->text, src->text, src->len * sizeof(uint32_t));
    st->len = src->len;
    st->caret = src->caret;
    st->dir = src->dir;
    layout(ta, st);
    st->spot = ar_layout_spot_after(&st->lay, st->caret);
    show(ta, st);
}

/*Make `ta` an arabic_ta. `src`: the arabic_ta it was copied from (by
 *lv_ta_create(parent, src)), whose logical text it takes; its own label holds
 *the source's display text, which cannot be turned back into typed text.*/
static void attach(lv_obj_t * ta, const ar_ta_t * src)
{
    /*The event callback of a copy came from the source: the hook, or the app's
     *callback set with lv_obj_set_event_cb() in front of it, which may call the
     *hook and so stays where it is. Any other callback becomes the user's.*/
    lv_event_cb_t cb = ta->event_cb;
    bool keep_cb = false;
    if(src && (cb == ta_event || cb == src->self->event_cb)) {
        keep_cb = cb != ta_event;
        cb = src->user_cb;
    }
    else if(cb == ta_event) {
        cb = NULL;
    }

    ar_ta_t * st = lv_mem_alloc(sizeof(ar_ta_t));
    LV_ASSERT_MEM(st);
    if(st == NULL) return;
    memset(st, 0, sizeof(ar_ta_t));
    st->self = ta;
    st->serial = ++serials;
    st->dir = AR_DIR_AUTO;
    st->user_cb = cb;
    st->signal_orig = ta->signal_cb;
    st->scrl_signal_orig = lv_page_get_scrl(ta)->signal_cb;

    /*features that would edit the display buffer behind our back*/
    lv_ta_set_pwd_mode(ta, false);
    lv_ta_set_accepted_chars(ta, NULL);
    lv_ta_set_max_length(ta, 0);
    lv_ta_set_text_sel(ta, false);

    /*the display buffer is already in screen order: LVGL's own BiDi must
     *leave it alone even when the screen's base direction is right to left*/
    lv_ta_ext_t * ext = lv_obj_get_ext_attr(ta);
    lv_obj_set_base_dir(ext->label, LV_BIDI_DIR_LTR);

    st->next = live;
    live = st;
    ta->user_data = TAG_OF(st->serial); /*so that lv_ta_create() copies of this one find it*/
    lv_obj_set_signal_cb(lv_page_get_scrl(ta), scrl_signal);
    lv_obj_set_signal_cb(ta, ta_signal);
    if(!keep_cb) ta->event_cb = ta_event;

    if(src) {
        ext->cursor.pos = 0; /*the copied index belongs to the source's text*/
        copy_state(ta, st, src);
        return;
    }
    /*the text already in the text area becomes the logical text*/
    st->len = decode_text(lv_ta_get_text(ta), st->text);
    st->caret = st->len;
    layout(ta, st);
    st->spot = ar_layout_spot_after(&st->lay, st->caret);
    show(ta, st);
}

lv_obj_t * arabic_ta_create(lv_obj_t * parent, const lv_obj_t * copy)
{
    ar_ta_t * src = get(copy); /*by object: works whatever the copy's user_data is*/
    lv_obj_t * ta = lv_ta_create(parent, copy);
    if(ta == NULL) return NULL;
    if(copy == NULL) {
        lv_ta_ext_t * ext = lv_obj_get_ext_attr(ta);
        ext->cursor.pos = 0; /*see show()*/
        lv_ta_set_text(ta, ""); /*a new lv_ta says "Text area"*/
    }
    attach(ta, src);
    return ta;
}

void arabic_ta_attach(lv_obj_t * ta)
{
    LV_ASSERT_OBJ(ta, "lv_ta");
    if(get(ta)) return; /*already one*/

    /*An lv_ta copied from an arabic_ta carries the source's tag in user_data*/
    uint32_t serial = TAG_SERIAL(ta->user_data);
    ar_ta_t * src = NULL;
    for(ar_ta_t * s = live; s && serial; s = s->next) {
        if(s->serial == serial) src = s;
    }
    if(serial && src == NULL) {
        LV_LOG_WARN("arabic_ta_attach: a copy of a deleted arabic_ta; create copies with arabic_ta_create()");
        return;
    }
    attach(ta, src);
}

void arabic_ta_set_event_cb(lv_obj_t * ta, lv_event_cb_t cb)
{
    ar_ta_t * st = get(ta);
    if(st) st->user_cb = cb == ta_event ? NULL : cb;
    else lv_obj_set_event_cb(ta, cb);
}

void arabic_ta_set_text(lv_obj_t * ta, const char * txt)
{
    ar_ta_t * st = get(ta);
    if(st == NULL) {
        lv_ta_set_text(ta, txt);
        return;
    }
    st->len = decode_text(txt, st->text);
    st->caret = st->len;
    st->after_cr = false;
    layout(ta, st);
    commit(ta, st);
}

const char * arabic_ta_get_text(const lv_obj_t * ta)
{
    ar_ta_t * st = get(ta);
    return st ? st->utf8 : lv_ta_get_text(ta);
}

const char * arabic_ta_get_display_text(const lv_obj_t * ta)
{
    return lv_ta_get_text(ta);
}

void arabic_ta_set_dir(lv_obj_t * ta, ar_dir_t dir)
{
    ar_ta_t * st = get(ta);
    if(st == NULL) return;
    st->dir = dir;
    refresh(ta, st);
}

void arabic_ta_set_one_line(lv_obj_t * ta, bool en)
{
    lv_ta_set_one_line(ta, en);
    ar_ta_t * st = get(ta);
    if(st) refresh(ta, st); /*lv_ta_set_one_line() resets the alignment after its own redraw*/
}

uint16_t arabic_ta_get_cursor(const lv_obj_t * ta)
{
    ar_ta_t * st = get(ta);
    if(st == NULL) return lv_ta_get_cursor_pos(ta);
    uint16_t d = lv_ta_get_cursor_pos(ta);
    if(d != st->spot && d <= st->lay.display_len) return st->lay.to_logical[d]; /*moved by a stock function*/
    return st->caret;
}

void arabic_ta_set_cursor(lv_obj_t * ta, uint16_t pos)
{
    ar_ta_t * st = get(ta);
    if(st == NULL) {
        lv_ta_set_cursor_pos(ta, pos);
        return;
    }
    if(pos > st->len) pos = st->len;
    while(pos < st->len && !st->lay.caret_ok[pos]) pos++; /*not between lam and alef*/
    st->caret = pos;
    st->after_cr = false;
    st->spot = st->lay.to_display[pos];
    place_cursor(ta, st, false);
}

void arabic_ta_cursor_left(lv_obj_t * ta)
{
    ar_ta_t * st = get(ta);
    if(st) step(ta, st, -1);
    else lv_ta_cursor_left(ta);
}

void arabic_ta_cursor_right(lv_obj_t * ta)
{
    ar_ta_t * st = get(ta);
    if(st) step(ta, st, +1);
    else lv_ta_cursor_right(ta);
}

void arabic_ta_backspace(lv_obj_t * ta)
{
    ar_ta_t * st = get(ta);
    if(st) send_delete(ta, st, false);
    else lv_ta_del_char(ta);
}

void arabic_ta_delete_forward(lv_obj_t * ta)
{
    ar_ta_t * st = get(ta);
    if(st) send_delete(ta, st, true);
    else lv_ta_del_char_forward(ta);
}

const ar_layout_t * arabic_ta_get_layout(const lv_obj_t * ta)
{
    ar_ta_t * st = get(ta);
    return st ? &st->lay : NULL;
}

/*********************************************************************
 * Label
 *********************************************************************/

void arabic_label_set_text(lv_obj_t * label, const char * txt)
{
    static uint32_t cps[AR_MAX_CHARS]; /*LVGL runs in one task: one scratch layout is enough*/
    static ar_layout_t lay;

    const lv_style_t * style = lv_obj_get_style(label);
    lv_label_long_mode_t mode = lv_label_get_long_mode(label);
    int32_t max_w = 0; /*these modes wrap lines at the label's width: wrap them here, in logical order*/
    if(mode == LV_LABEL_LONG_BREAK || mode == LV_LABEL_LONG_DOT) max_w = wrap_width(lv_obj_get_width(label), style);

    uint16_t n = decode_text(txt, cps);
    ar_layout(cps, n, AR_DIR_AUTO, max_w, glyph_width, (void *)style, &lay);
    lv_obj_set_base_dir(label, LV_BIDI_DIR_LTR); /*already in screen order, see arabic_ta_attach()*/
    lv_label_set_align(label, lay.lines[0].rtl ? LV_LABEL_ALIGN_RIGHT : LV_LABEL_ALIGN_LEFT);
    lv_label_set_text(label, lay.utf8);
}
