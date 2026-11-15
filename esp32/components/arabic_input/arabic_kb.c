/**
 * @file arabic_kb.c
 * Arabic on-screen keyboard: a stock lv_kb with its own maps and event
 * callback. See arabic_input.h.
 *
 * The Arabic layout follows the standard Arabic (101) PC keyboard: the top
 * row is the Q row (ض to د on the ] key), then the A row (ش to ط on the '
 * key) and the Z row (ئ to ظ). ذ, which a PC keyboard has left of 1, ends the
 * bottom letter row, and the shifted alef forms أ إ آ sit next to the space
 * bar, since there is no shift layer. The keys only send text: shaping and
 * order are the text area's job, so a key types the plain letter (U+06xx).
 */

#include "arabic_kb.h"
#include <string.h>

#define KEY_FLAGS (LV_BTNM_CTRL_NO_REPEAT | LV_BTNM_CTRL_CLICK_TRIG) /*layout and OK keys, as in lv_kb.c*/

/*The stock lv_btnm draws key labels as they are, so a label with more than
 *one Arabic letter has to be shaped and in screen order already*/
#define KEY_AR       "ﻲﺑﺮﻋ" /*"عربي" shaped: to the Arabic layout*/
#define KEY_LAM_ALEF "ﻻ"                   /*the لا ligature: types lam + alef*/

/* clang-format off */
static const char * map_ar[] = {
    "ض", "ص", "ث", "ق", "ف", "غ", "ع", "ه", "خ", "ح", "ج", "د", LV_SYMBOL_BACKSPACE, "\n",
    "ش", "س", "ي", "ب", "ل", "ا", "ت", "ن", "م", "ك", "ط", LV_SYMBOL_NEW_LINE, "\n",
    "ئ", "ء", "ؤ", "ر", KEY_LAM_ALEF, "ى", "ة", "و", "ز", "ظ", "ذ", "\n",
    "EN", "123", "أ", "إ", "آ", " ", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

static const lv_btnm_ctrl_t ctrl_ar[] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    KEY_FLAGS | 3, KEY_FLAGS | 3, 2, 2, 2, 5, 2, 2, KEY_FLAGS | 3};

static const char * map_en_lc[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
    "ABC", "z", "x", "c", "v", "b", "n", "m", ",", ".", "\n",
    KEY_AR, "123", " ", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

static const char * map_en_uc[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", ",", ".", "\n",
    KEY_AR, "123", " ", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

static const lv_btnm_ctrl_t ctrl_en[] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 3,
    KEY_FLAGS | 3, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    KEY_FLAGS | 3, KEY_FLAGS | 3, 6, 2, 2, KEY_FLAGS | 3};

static const char * map_num[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "١", "٢", "٣", "٤", "٥", "٦", "٧", "٨", "٩", "٠", LV_SYMBOL_NEW_LINE, "\n",
    "،", "؛", "؟", ".", ":", "-", "(", ")", "!", "\"", "\n",
    KEY_AR, "EN", " ", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

static const lv_btnm_ctrl_t ctrl_num[] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    KEY_FLAGS | 3, KEY_FLAGS | 3, 6, 2, 2, KEY_FLAGS | 3};
/* clang-format on */

/*The key of the new layout that leads back to the old one*/
static const char * back_key(const char ** old_map, const char ** new_map)
{
    if(old_map == map_ar) return KEY_AR;
    if(old_map == map_num) return "123";
    if(old_map == map_en_uc) return new_map == map_en_lc ? "ABC" : "EN";
    return new_map == map_en_uc ? "abc" : "EN"; /*from lower case English*/
}

static void set_layout(lv_obj_t * kb, const char * map[], const lv_btnm_ctrl_t ctrl[])
{
    lv_btnm_ext_t * ext = lv_obj_get_ext_attr(kb);
    const char ** old_map = ext->map_p;
    lv_btnm_set_map(kb, map);
    lv_btnm_set_ctrl_map(kb, ctrl);

    /*lv_btnm_set_map() keeps the pressed and active key indexes, which may be
     *past the end of the new map or on an unrelated key. With a keypad the
     *selection then stays on the key that switches back.*/
    uint16_t focus = 0;
    const char * back = back_key(old_map, map);
    for(uint16_t i = 0, b = 0; map[i][0] != '\0'; i++) {
        if(strcmp(map[i], "\n") == 0) continue;
        if(strcmp(map[i], back) == 0) focus = b;
        b++;
    }
    if(ext->btn_id_pr != LV_BTNM_BTN_NONE) ext->btn_id_pr = focus;
    if(ext->btn_id_act != LV_BTNM_BTN_NONE) ext->btn_id_act = focus;
}

/*The key under a point, or LV_BTNM_BTN_NONE (lv_btnm.c get_button_from_point())*/
static uint16_t key_at(lv_obj_t * kb, const lv_point_t * p)
{
    lv_btnm_ext_t * ext = lv_obj_get_ext_attr(kb);
    lv_area_t kb_area;
    lv_obj_get_coords(kb, &kb_area);
    for(uint16_t i = 0; i < ext->btn_cnt; i++) {
        lv_area_t a = ext->button_areas[i];
        a.x1 += kb_area.x1;
        a.x2 += kb_area.x1;
        a.y1 += kb_area.y1;
        a.y2 += kb_area.y1;
        if(lv_area_is_point_on(&a, p)) return i;
    }
    return LV_BTNM_BTN_NONE;
}

static void send_key(lv_obj_t * ta, uint32_t key)
{
    lv_signal_send(ta, LV_SIGNAL_CONTROL, &key);
}

static void kb_event(lv_obj_t * kb, lv_event_t e)
{
    if(e != LV_EVENT_VALUE_CHANGED) return;

    uint16_t id = lv_btnm_get_active_btn(kb);
    if(id == LV_BTNM_BTN_NONE) return;
    if(lv_btnm_get_btn_ctrl(kb, id, LV_BTNM_CTRL_HIDDEN | LV_BTNM_CTRL_INACTIVE)) return;

    /*When a finger slides onto another key, lv_btnm sends VALUE_CHANGED for
     *the key it left (lv_btnm.c, LV_SIGNAL_PRESSING), even for keys that only
     *act on release: that would type the letter twice or switch the layout
     *under the finger. While pressed, only the key under the finger counts.*/
    lv_indev_t * indev = lv_indev_get_act();
    if(indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER && indev->proc.state == LV_INDEV_STATE_PR) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        if(key_at(kb, &p) != id) return;
    }

    const char * txt = lv_btnm_get_active_btn_text(kb);
    if(txt == NULL) return;

    if(strcmp(txt, KEY_AR) == 0) {
        set_layout(kb, map_ar, ctrl_ar);
        return;
    }
    else if(strcmp(txt, "EN") == 0 || strcmp(txt, "abc") == 0) {
        set_layout(kb, map_en_lc, ctrl_en);
        return;
    }
    else if(strcmp(txt, "ABC") == 0) {
        set_layout(kb, map_en_uc, ctrl_en);
        return;
    }
    else if(strcmp(txt, "123") == 0) {
        set_layout(kb, map_num, ctrl_num);
        return;
    }

    lv_obj_t * ta = lv_kb_get_ta(kb);
    if(ta == NULL) return;

    /*the cursor keys and Backspace go in the way a keypad sends them, so each
     *kind of text area handles them its own way*/
    if(strcmp(txt, LV_SYMBOL_OK) == 0)
        lv_event_send(ta, LV_EVENT_APPLY, NULL);
    else if(strcmp(txt, LV_SYMBOL_LEFT) == 0)
        send_key(ta, LV_KEY_LEFT);
    else if(strcmp(txt, LV_SYMBOL_RIGHT) == 0)
        send_key(ta, LV_KEY_RIGHT);
    else if(strcmp(txt, LV_SYMBOL_BACKSPACE) == 0)
        send_key(ta, LV_KEY_BACKSPACE);
    else if(strcmp(txt, LV_SYMBOL_NEW_LINE) == 0)
        lv_ta_add_char(ta, '\n'); /*dropped by one-line text areas*/
    else if(strcmp(txt, KEY_LAM_ALEF) == 0)
        lv_ta_add_text(ta, "لا");
    else
        lv_ta_add_text(ta, txt);
}

lv_obj_t * arabic_kb_create(lv_obj_t * parent, const lv_font_t * font)
{
    static const lv_kb_style_t types[] = {LV_KB_STYLE_BTN_REL, LV_KB_STYLE_BTN_PR, LV_KB_STYLE_BTN_TGL_REL,
                                          LV_KB_STYLE_BTN_TGL_PR, LV_KB_STYLE_BTN_INA};
    static lv_style_t styles[sizeof(types) / sizeof(types[0])];

    lv_obj_t * kb = lv_kb_create(parent, NULL);
    if(kb == NULL) return NULL;

    /*the stock key styles with the Arabic font*/
    for(size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        lv_style_copy(&styles[i], lv_kb_get_style(kb, types[i]));
        styles[i].text.font = font;
        lv_kb_set_style(kb, types[i], &styles[i]);
    }

    lv_obj_set_event_cb(kb, kb_event);
    set_layout(kb, map_ar, ctrl_ar);
    return kb;
}
