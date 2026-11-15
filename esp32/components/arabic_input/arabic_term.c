/**
 * @file arabic_term.c
 * Patch B: a terminal-style Arabic field in a plain lv_ta. See arabic_term.h;
 * the text rules are in arabic_term_text.c.
 */

#include "arabic_term.h"
#include "arabic_term_text.h"
#include <string.h>

typedef struct {
    lv_obj_t * ta;
    lv_signal_cb_t signal_orig;
    lv_event_cb_t user_cb;
    uint16_t caret;      /*where this code left the caret: if it moved, the push ended*/
    bool push;           /*the push segment of the last edit (see ar_term_t)*/
    uint16_t push_start;
    uint16_t push_end;
    bool forward_delete; /*the next LV_KEY_DEL insert is the Delete key*/
    bool busy;           /*in store(): our own lv_ta_set_text()*/
    bool parent_off;     /*parent_event is cleared until the next event or signal (term_event())*/
} field_t;

#define FIELDS 4
static field_t fields[FIELDS];
static ar_term_t text; /*the field being edited; LVGL runs in one task*/

static field_t * field_of(const lv_obj_t * ta)
{
    for(int i = 0; i < FIELDS; i++) {
        if(fields[i].ta == ta) return &fields[i];
    }
    return NULL;
}

/*The text area's text and caret, with the push segment if the caret did not move*/
static void load(lv_obj_t * ta, field_t * f, ar_term_t * t)
{
    t->n = (uint16_t)ar_utf8_decode(lv_ta_get_text(ta), t->c, AR_MAX_CHARS);
    t->caret = lv_ta_get_cursor_pos(ta);
    if(t->caret > t->n) t->caret = t->n; /*text set from outside may have been cut*/
    t->push = f->push && t->caret == f->caret; /*an arrow key or a tap ends it*/
    t->push_start = f->push_start;
    t->push_end = f->push_end;
}

/*Put the text and the caret into the text area*/
static void store(lv_obj_t * ta, field_t * f, const ar_term_t * t)
{
    static char utf8[AR_MAX_CHARS * 4 + 1];
    ar_utf8_encode(t->c, t->n, utf8);

    /*lv_ta_set_text() has to just take the text: with a max length or a list
     *of accepted characters it would type it in character by character (both
     *unsupported, so they are cleared); it reads the letter at the old cursor,
     *which may be past the new end; and its VALUE_CHANGED is internal, so it
     *must reach neither the user's callback nor a parent*/
    lv_ta_ext_t * ext = lv_obj_get_ext_attr(ta);
    ext->accapted_chars = NULL;
    ext->max_length = 0;
    ext->cursor.pos = 0;
    uint8_t parent_event = ta->parent_event;
    ta->parent_event = 0;
    f->busy = true;
    lv_ta_set_text(ta, utf8);
    ta->parent_event = parent_event;

    /*Right aligned as Arabic is; a one-line lv_ta only scrolls left-aligned
     *text, so a line wider than the box is left aligned*/
    const lv_style_t * style = lv_obj_get_style(ext->label);
    lv_coord_t w = lv_txt_get_width(utf8, (uint16_t)strlen(utf8), style->text.font, style->text.letter_space,
                                    LV_TXT_FLAG_NONE);
    lv_label_align_t align = w + 2 > lv_page_get_fit_width(ta) ? LV_LABEL_ALIGN_LEFT : LV_LABEL_ALIGN_RIGHT;
    if(lv_label_get_align(ext->label) != align) lv_ta_set_text_align(ta, align);

    ext->cursor.pos = (uint16_t)(t->caret + 1); /*lv_ta_set_cursor_pos() does nothing for an unchanged index*/
    lv_ta_set_cursor_pos(ta, t->caret);
    f->caret = t->caret;
    f->push = t->push;
    f->push_start = t->push_start;
    f->push_end = t->push_end;
    f->busy = false;
}

/*An LV_EVENT_INSERT: apply it to the stored text*/
static void apply_input(lv_obj_t * ta, field_t * f, const char * in)
{
    load(ta, f, &text);
    if(in[0] == LV_KEY_DEL && in[1] == '\0') {
        if(f->forward_delete) ar_term_delete(&text);
        else ar_term_backspace(&text);
    }
    else {
        uint32_t c;
        size_t used;
        while((used = ar_utf8_next(in, &c)) > 0) {
            in += used;
            ar_term_type(&text, c);
        }
    }
    f->forward_delete = false;
    store(ta, f, &text);
    lv_event_send(ta, LV_EVENT_VALUE_CHANGED, NULL);
}

static void parent_on(lv_obj_t * ta, field_t * f)
{
    if(f->parent_off) {
        ta->parent_event = 1;
        f->parent_off = false;
    }
}

static void term_event(lv_obj_t * ta, lv_event_t e)
{
    field_t * f = field_of(ta);
    if(f == NULL) return;
    parent_on(ta, f);

    if(e == LV_EVENT_INSERT) {
        if(f->busy) return;
        const char * in = lv_event_get_data();
        /*the stock lv_ta does not insert it: this code does*/
        lv_ta_set_insert_replace(ta, "");
        if(f->user_cb) f->user_cb(ta, e);
        if(field_of(ta) != f) return; /*the callback deleted the text area*/
        /*LVGL passes the event on to a parent (parent_event) after this
         *function, before the stock lv_ta_add_text() looks at the cancel; the
         *parent may type into another text area, which clears it. So the parent
         *gets it here, before the edit, and LVGL does not pass it on again.*/
        bool to_parent = ta->parent_event && ta->par;
        if(to_parent) {
            lv_event_send(ta->par, e, in);
            if(field_of(ta) != f) return;
        }
        lv_ta_set_insert_replace(ta, "");
        apply_input(ta, f, in);
        if(field_of(ta) != f) return;
        /*the cancel is one variable for all text areas: a VALUE_CHANGED handler
         *that typed into another one cleared it*/
        lv_ta_set_insert_replace(ta, "");
        if(to_parent) {
            ta->parent_event = 0; /*back on with the next event or signal*/
            f->parent_off = true;
        }
        return;
    }
    if(e == LV_EVENT_VALUE_CHANGED && f->busy) return; /*our own lv_ta_set_text()*/
    if(f->user_cb) f->user_cb(ta, e);
}

/*Backspace and Delete announce themselves like lv_ta_del_char(); nothing to delete: no event*/
static void send_delete(lv_obj_t * ta, field_t * f, bool forward)
{
    static const char del[2] = {LV_KEY_DEL, '\0'};
    load(ta, f, &text);
    bool something = forward ? text.caret > 0 || text.push
                             : text.caret < text.n || (text.push && text.push_end > text.push_start);
    if(!something) return;
    f->forward_delete = forward;
    lv_event_send(ta, LV_EVENT_INSERT, del);
}

static lv_res_t term_signal(lv_obj_t * ta, lv_signal_t sign, void * param)
{
    field_t * f = field_of(ta);
    lv_signal_cb_t orig = f->signal_orig;
    parent_on(ta, f);
    if(sign == LV_SIGNAL_GET_TYPE) return orig(ta, sign, param); /*first: getters ask for it (see arabic_ta.c)*/

    if(sign == LV_SIGNAL_CONTROL) {
        uint32_t key = *(uint32_t *)param;
        if(key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
            send_delete(ta, f, key == LV_KEY_DEL);
            return LV_RES_OK;
        }
        /*the field is right to left: its start is the right end*/
        if(key == LV_KEY_HOME || key == LV_KEY_END) {
            lv_ta_set_cursor_pos(ta, key == LV_KEY_HOME ? LV_TA_CURSOR_LAST : 0);
            return LV_RES_OK;
        }
    }

    lv_res_t res = orig(ta, sign, param);
    if(res == LV_RES_OK && sign == LV_SIGNAL_CLEANUP) {
        ta->signal_cb = orig; /*LVGL asks for the type once more*/
        f->ta = NULL;
    }
    return res;
}

void arabic_term_attach(lv_obj_t * ta, lv_event_cb_t user_cb)
{
    field_t * f = field_of(NULL);
    if(f == NULL) {
        LV_LOG_WARN("arabic_term_attach: no free field");
        return;
    }
    memset(f, 0, sizeof(*f));
    f->ta = ta;
    f->user_cb = user_cb;
    f->signal_orig = ta->signal_cb;
    lv_obj_set_signal_cb(ta, term_signal);
    lv_obj_set_event_cb(ta, term_event);

    /*features that would edit the stored text behind our back*/
    lv_ta_set_pwd_mode(ta, false);
    lv_ta_set_accepted_chars(ta, NULL);
    lv_ta_set_max_length(ta, 0);
    lv_ta_set_text_sel(ta, false);

    /*the text already there is typed text*/
    static uint32_t typed[AR_MAX_CHARS];
    size_t n = ar_utf8_decode(lv_ta_get_text(ta), typed, AR_MAX_CHARS);
    ar_term_from_logical(&text, typed, n);
    store(ta, f, &text);
}

void arabic_term_backspace(lv_obj_t * ta)
{
    field_t * f = field_of(ta);
    if(f) send_delete(ta, f, false);
    else lv_ta_del_char(ta);
}

const char * arabic_term_get_text(lv_obj_t * ta, char * buf, size_t size)
{
    static uint32_t out[2 * AR_MAX_CHARS];
    ar_term_t * t = &text;
    t->n = (uint16_t)ar_utf8_decode(lv_ta_get_text(ta), t->c, AR_MAX_CHARS);
    size_t n = ar_term_to_logical(t, out);

    size_t b = 0; /*UTF-8 that fits in size*/
    for(size_t i = 0; i < n; i++) {
        char tmp[5];
        size_t len = ar_utf8_encode(&out[i], 1, tmp);
        if(b + len + 1 > size) break;
        memcpy(buf + b, tmp, len);
        b += len;
    }
    if(size) buf[b] = '\0';
    return buf;
}

void arabic_term_to_display(const char * txt, char * out)
{
    static uint32_t typed[AR_MAX_CHARS];
    size_t n = ar_utf8_decode(txt, typed, AR_MAX_CHARS);
    ar_term_from_logical(&text, typed, n);
    ar_utf8_encode(text.c, text.n, out);
}

void arabic_term_label_set_text(lv_obj_t * label, const char * txt)
{
    static char out[AR_MAX_CHARS * 4 + 1];
    arabic_term_to_display(txt, out);
    lv_label_set_text(label, out);
}
