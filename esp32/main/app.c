/**
 * @file app.c
 * Arabic input test scene for LVGL v6.1.2. As released, v6.1.2 has no Arabic
 * support: no letter shaping (added in v7.0), and its BiDi only treats Hebrew
 * as right-to-left (lv_bidi.c, lv_bidi_letter_is_rtl), so Arabic comes out
 * unjoined and left to right.
 *
 * Plain LVGL code: nothing here knows about QEMU or the display hardware, so
 * this file runs unchanged on a real board.
 *
 * Top to bottom:
 *   1. "target": the sample sentence hand-shaped into presentation forms and
 *      stored in display order, i.e. what correct output looks like
 *   2. "label":  the same sentence converted for the field as a host had to for
 *      an Arabic terminal (arabic_term_label_set_text())
 *   3. the textarea under test (focused at start: typing goes here)
 *   4. the code points the textarea stores (last few; the serial console
 *      prints all of them on every change)
 *   5. the Arabic keyboard (arabic_kb.c; Tab focuses it, arrows pick a key,
 *      Enter presses it, same path as a touch tap)
 *
 * Design B, shape while typing, store what is shown (arabic_term.h): row 4
 * shows what the text area stores (shaped, display order); the console
 * prints it ("[stored]") and the text converted back ("[logical]").
 */

#include "app.h"
#include <stdio.h>
#include <string.h>
#include "arabic_kb.h"
#include "arabic_term.h"
#include "arabic_utf8.h" /*AR_MAX_CHARS*/

#define SAMPLE_TEXT "مرحبا بك في ESP32"

/*SAMPLE_TEXT shaped (initial/medial/final/isolated forms) and
 *laid out left to right in the order the glyphs appear on screen*/
#define TARGET_TEXT "ESP32 ﻲﻓ ﻚﺑ ﺎﺒﺣﺮﻣ"

#define CODES_SHOWN 10 /*code points shown on screen; the console gets all of them*/

LV_FONT_DECLARE(lv_font_arabic_16) /*main/font_arabic_16.c, DejaVu Sans via lv_font_conv*/

static lv_obj_t * ta;
static lv_obj_t * codes_label;

static lv_style_t style_ar;
static lv_style_t style_ta;
static lv_style_t style_caption;

/* Minimal UTF-8 decoder: returns the code point at s[*i] and advances *i. */
static uint32_t utf8_next(const char * s, uint32_t * i)
{
    const uint8_t * p = (const uint8_t *)s + *i;
    uint32_t cp;
    uint32_t len;

    if(p[0] < 0x80) {
        cp = p[0];
        len = 1;
    }
    else if((p[0] & 0xE0) == 0xC0) {
        cp = p[0] & 0x1F;
        len = 2;
    }
    else if((p[0] & 0xF0) == 0xE0) {
        cp = p[0] & 0x0F;
        len = 3;
    }
    else {
        cp = p[0] & 0x07;
        len = 4;
    }

    for(uint32_t k = 1; k < len; k++) {
        if((p[k] & 0xC0) != 0x80) { /*truncated sequence*/
            *i += k;
            return 0xFFFD;
        }
        cp = (cp << 6) | (p[k] & 0x3F);
    }

    *i += len;
    return cp;
}

/* Prints "<tag> cursor=N chars=N | XXXX XXXX ...  "text"" (no cursor if
 * `cursor` is negative). If `line` is not NULL, the last CODES_SHOWN code
 * points go there too (for the screen).
 * Typed Arabic letters are U+06xx; shaped presentation forms are U+FBxx-U+FExx. */
static void print_codes(const char * tag, const char * txt, int cursor, char * line, size_t size)
{
    uint32_t total = 0;
    for(uint32_t i = 0; txt[i] != '\0'; total++) utf8_next(txt, &i);

    size_t pos = 0;
    if(line) line[0] = '\0';

    if(cursor >= 0) printf("%s cursor=%d chars=%u |", tag, cursor, (unsigned)total);
    else printf("%s chars=%u |", tag, (unsigned)total);

    uint32_t n = 0;
    for(uint32_t i = 0; txt[i] != '\0'; n++) {
        uint32_t cp = utf8_next(txt, &i);
        printf(" %04X", (unsigned)cp);

        if(line && n + CODES_SHOWN >= total && pos < size) {
            const char * sep = pos ? " " : (total > CODES_SHOWN ? "... " : "");
            pos += (size_t)snprintf(line + pos, size - pos, "%s%04X", sep, (unsigned)cp);
        }
    }
    printf("  \"%s\"\n", txt);

    if(line && total == 0) snprintf(line, size, "(empty)");
}

static char typed[AR_MAX_CHARS * 4 + 1];

/* Logs what the text area stores (shaped, in display order) and the text
 * converted back to typing order. The screen shows the stored one. */
static void show_text_codes(void)
{
    char line[CODES_SHOWN * 7 + 8];
    print_codes("[stored]", lv_ta_get_text(ta), lv_ta_get_cursor_pos(ta), line, sizeof(line));
    print_codes("[logical]", arabic_term_get_text(ta, typed, sizeof(typed)), -1, NULL, 0);
    lv_label_set_text(codes_label, line);
}

static const char * ta_text(void)
{
    return arabic_term_get_text(ta, typed, sizeof(typed));
}

static void ta_event_cb(lv_obj_t * obj, lv_event_t event)
{
    (void)obj;

    if(event == LV_EVENT_INSERT) {
        /*sent before the text goes in: this is the raw input*/
        const char * in = lv_event_get_data();
        printf("[insert]");
        for(uint32_t i = 0; in[i] != '\0';) printf(" %04X", (unsigned)utf8_next(in, &i));
        printf("  \"%s\"\n", in);
    }
    else if(event == LV_EVENT_VALUE_CHANGED) {
        show_text_codes();
    }
    else if(event == LV_EVENT_APPLY) { /*the keyboard's OK key*/
        printf("[ready] \"%s\"\n", ta_text());
    }
}

static lv_obj_t * caption(lv_obj_t * parent, const char * txt, lv_coord_t y)
{
    lv_obj_t * l = lv_label_create(parent, NULL);
    lv_label_set_style(l, LV_LABEL_STYLE_MAIN, &style_caption);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, 4, y);
    return l;
}

/* `logical`: txt is typed text (converted to what the field would store);
 * otherwise txt is already shaped and in display order. */
static lv_obj_t * arabic_label(lv_obj_t * parent, const char * txt, lv_coord_t y, bool logical)
{
    lv_obj_t * l = lv_label_create(parent, NULL);
    lv_label_set_style(l, LV_LABEL_STYLE_MAIN, &style_ar);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CROP);
    lv_label_set_align(l, LV_LABEL_ALIGN_RIGHT);
    lv_obj_set_size(l, 262, 24);
    lv_obj_set_pos(l, 54, y);
    if(logical) {
        arabic_term_label_set_text(l, txt); /*converted as a host had to for an Arabic terminal*/
        return l;
    }
    lv_label_set_text(l, txt);
    return l;
}

static lv_group_t * app_group;
static bool show_target = true;

/*(Re)build the screen; the text area starts empty*/
static void build(void)
{
    lv_obj_t * scr = lv_scr_act();
    lv_obj_clean(scr); /*deleted objects also leave the group*/

    lv_coord_t y = 2;
    if(show_target) {
        caption(scr, "target", y + 6);
        arabic_label(scr, TARGET_TEXT, y, false);
        y += 26;
    }
    caption(scr, "label", y + 6);
    arabic_label(scr, SAMPLE_TEXT, y, true);
    y += 28;

    ta = lv_ta_create(scr, NULL);
    lv_style_copy(&style_ta, lv_ta_get_style(ta, LV_TA_STYLE_BG));
    style_ta.text.font = &lv_font_arabic_16;
    style_ta.body.padding.top = 4;
    style_ta.body.padding.bottom = 4;
    lv_ta_set_style(ta, LV_TA_STYLE_BG, &style_ta);
    lv_ta_set_one_line(ta, true);
    lv_ta_set_text(ta, "");
    lv_ta_set_placeholder_text(ta, "type here");
    lv_obj_set_width(ta, 312);
    lv_obj_set_pos(ta, 4, y);
    arabic_term_attach(ta, ta_event_cb);

    codes_label = lv_label_create(scr, NULL);
    lv_label_set_style(codes_label, LV_LABEL_STYLE_MAIN, &style_caption);
    lv_label_set_long_mode(codes_label, LV_LABEL_LONG_CROP);
    lv_obj_set_size(codes_label, 312, 14);
    lv_obj_align(codes_label, ta, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    lv_obj_t * kb = arabic_kb_create(scr, &lv_font_arabic_16);
    lv_obj_set_size(kb, 320, 240 - lv_obj_get_y(codes_label) - 18);
    lv_obj_align(kb, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
    lv_kb_set_ta(kb, ta);
    lv_kb_set_cursor_manage(kb, true);

    lv_group_add_obj(app_group, ta);
    lv_group_add_obj(app_group, kb);
    lv_group_focus_obj(ta);

    show_text_codes();
}

void app_create(lv_group_t * group)
{
    app_group = group;

    lv_style_copy(&style_ar, &lv_style_plain);
    style_ar.text.font = &lv_font_arabic_16;

    lv_style_copy(&style_caption, &lv_style_plain);
    style_caption.text.font = &lv_font_roboto_12;
    style_caption.text.color = LV_COLOR_GRAY;

    build();
}

void app_toggle_target(void)
{
    show_target = !show_target;
    printf("[app] target row %s\n", show_target ? "shown" : "hidden");
    build();
}
