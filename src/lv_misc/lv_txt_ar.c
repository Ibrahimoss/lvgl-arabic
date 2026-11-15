/**
 * @file lv_txt_ar.c
 * Patch C: the form of an Arabic letter from its neighbours. See lv_txt_ar.h.
 */

#include "lv_txt_ar.h"

/*Joining type of U+0621..U+064A: U does not join, R joins the letter before it
 *(2 forms), D joins on both sides (4 forms), C is tatweel (joins both, 1 form),
 *- has no presentation forms in U+FE80-U+FEF4 and is drawn as it is*/
static const char types[] = "URRRRDRDRDDDDDRRRRDDDDDDDD-----CDDDDDDDRRD";

#define FIRST 0x0621
#define LAST  0x064A
#define LAM   0x0644
#define ALEF_MAKSURA 0x0649 /*joins on both sides; only its isolated and final forms are at U+FEEF-U+FEF0*/
#define ALEF_MAKSURA_INI 0xFBE8
#define ALEF_MAKSURA_MED 0xFBE9

/*Isolated form of each letter; final, initial and medial follow it*/
static uint16_t iso_forms[LAST - FIRST + 1];

static char type_of(uint32_t c)
{
    if(c == ALEF_MAKSURA) return 'D';
    if(c < FIRST || c > LAST) return 'U';
    char t = types[c - FIRST];
    return t == '-' ? 'U' : t;
}

static uint16_t iso_form(uint32_t c)
{
    if(iso_forms[0] == 0) { /*first call: the forms are numbered in letter order from U+FE80*/
        uint16_t code = 0xFE80;
        for(uint32_t k = 0; k <= LAST - FIRST; k++) {
            char t = types[k];
            iso_forms[k] = (t == 'U' || t == 'R' || t == 'D') ? code : 0;
            code += t == 'U' ? 1 : t == 'R' ? 2 : t == 'D' ? 4 : 0;
        }
    }
    return iso_forms[c - FIRST];
}

/*Isolated lam-alef ligature for an alef, 0 for any other letter*/
static uint32_t lam_alef(uint32_t alef)
{
    switch(alef) {
        case 0x0622: return 0xFEF5; /*alef with madda*/
        case 0x0623: return 0xFEF7; /*alef with hamza above*/
        case 0x0625: return 0xFEF9; /*alef with hamza below*/
        case 0x0627: return 0xFEFB;
        default: return 0;
    }
}

uint32_t lv_txt_ar_glyph(uint32_t prev, uint32_t letter, uint32_t next)
{
    char t = type_of(letter);
    if(t == 'U' && (letter < FIRST || letter > LAST || iso_form(letter) == 0)) return letter;
    if(t == 'C') return letter;
    if(prev == LAM && lam_alef(letter)) return 0; /*the lam before it draws both*/

    char tp = type_of(prev);
    bool join_prev = (t == 'R' || t == 'D') && (tp == 'D' || tp == 'C');
    if(letter == LAM && lam_alef(next)) return lam_alef(next) + (join_prev ? 1 : 0);

    char tn = type_of(next);
    bool join_next = t == 'D' && (tn == 'R' || tn == 'D' || tn == 'C');

    uint32_t iso = iso_form(letter);
    if(letter == ALEF_MAKSURA && join_next) return join_prev ? ALEF_MAKSURA_MED : ALEF_MAKSURA_INI;
    if(join_prev && join_next) return iso + 3; /*medial*/
    if(join_prev) return iso + 1;              /*final*/
    if(join_next) return iso + 2;              /*initial*/
    return iso;
}

uint32_t lv_txt_ar_glyph_in(uint32_t before, uint32_t letter, uint32_t after, bool visual)
{
    /*In display order a right-to-left run is reversed: the letter typed before
     *this one is drawn after it. Letters only join within such a run.*/
    if(visual && letter >= FIRST && letter <= LAST) return lv_txt_ar_glyph(after, letter, before);
    return lv_txt_ar_glyph(before, letter, after);
}

uint16_t lv_txt_ar_width(const lv_font_t * font, uint32_t before, uint32_t letter, uint32_t after, bool visual)
{
    uint32_t glyph = lv_txt_ar_glyph_in(before, letter, after, visual);
    if(glyph == 0) return 0;
    return lv_font_get_glyph_width(font, glyph, glyph == letter ? after : 0); /*kerning only between unshaped letters*/
}
