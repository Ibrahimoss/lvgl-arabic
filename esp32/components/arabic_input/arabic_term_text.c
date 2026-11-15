/**
 * @file arabic_term_text.c
 * Patch B: shape while typing, store what is shown. See arabic_term_text.h.
 */

#include "arabic_term_text.h"
#include <string.h>

/*********************************************************************
 * Letter forms: the terminal's character table
 *********************************************************************/

/*Joining type of U+0621..U+064A: U does not join, R joins the letter typed
 *before it (2 forms), D joins on both sides (4 forms), C is tatweel (joins
 *both, 1 form), - has no presentation forms and is stored as it is*/
static const char types[] = "URRRRDRDRDDDDDRRRRDDDDDDDD-----CDDDDDDDRRD";

#define FIRST     0x0621
#define LAST      0x064A
#define LAM       0x0644
#define FORMS     0xFE80 /*isolated form of hamza; the other letters follow in order*/
#define FORMS_END 0xFEF4
#define LIG_FIRST 0xFEF5 /*lam-alef ligatures: isolated, final, for 4 alefs*/
#define LIG_LAST  0xFEFC
#define LIG       1      /*base_of() of a ligature*/
#define ALEF_MAKSURA     0x0649 /*joins on both sides; only its isolated and final forms are at U+FEEF-U+FEF0*/
#define ALEF_MAKSURA_INI 0xFBE8
#define ALEF_MAKSURA_MED 0xFBE9

static uint16_t iso_forms[LAST - FIRST + 1];

static uint8_t form_count(char t)
{
    return t == 'U' ? 1 : t == 'R' ? 2 : t == 'D' ? 4 : 0;
}

static void init_forms(void)
{
    if(iso_forms[0]) return;
    uint16_t code = FORMS;
    for(uint32_t k = 0; k <= LAST - FIRST; k++) {
        iso_forms[k] = form_count(types[k]) ? code : 0;
        code += form_count(types[k]);
    }
}

static const uint16_t alefs[4] = {0x0622, 0x0623, 0x0625, 0x0627}; /*in ligature order*/

static int alef_index(uint32_t c)
{
    for(int i = 0; i < 4; i++) {
        if(alefs[i] == c) return i;
    }
    return -1;
}

/*The plain letter of a stored character; LIG (and its alef) for lam-alef*/
static uint32_t base_of(uint32_t c, uint32_t * alef)
{
    if(c >= LIG_FIRST && c <= LIG_LAST) {
        if(alef) *alef = alefs[(c - LIG_FIRST) / 2];
        return LIG;
    }
    if(c == ALEF_MAKSURA_INI || c == ALEF_MAKSURA_MED) return ALEF_MAKSURA;
    if(c >= FORMS && c <= FORMS_END) {
        init_forms();
        for(uint32_t k = 0; k <= LAST - FIRST; k++) {
            if(iso_forms[k] && c >= iso_forms[k] && c < (uint32_t)iso_forms[k] + form_count(types[k])) return FIRST + k;
        }
    }
    return c;
}

static char type_of(uint32_t base)
{
    if(base == LIG) return 'L';
    if(base == ALEF_MAKSURA) return 'D';
    if(base < FIRST || base > LAST) return 'U';
    return types[base - FIRST];
}

/*joins the letter typed after it / before it*/
static bool joins_next(uint32_t base)
{
    char t = type_of(base);
    return t == 'D' || t == 'C';
}

static bool joins_prev(uint32_t base)
{
    char t = type_of(base);
    return t == 'R' || t == 'D' || t == 'C' || t == 'L';
}

/*********************************************************************
 * The stored text
 *********************************************************************/

/*Give the character at k the form its neighbours call for*/
static void reshape(ar_term_t * t, int k)
{
    if(k < 0 || k >= t->n) return;
    uint32_t alef = 0;
    uint32_t base = base_of(t->c[k], &alef);
    char type = type_of(base);
    if(type != 'R' && type != 'D' && type != 'L' && base != FIRST) return; /*not shaped (hamza has one form)*/

    uint32_t before = k + 1 < t->n ? base_of(t->c[k + 1], NULL) : 0; /*typed before it: on its right*/
    uint32_t after = k > 0 ? base_of(t->c[k - 1], NULL) : 0;         /*typed after it: on its left*/
    bool jp = joins_prev(base) && joins_next(before);
    bool jn = joins_next(base) && joins_prev(after);

    if(base == LIG) {
        t->c[k] = LIG_FIRST + 2 * alef_index(alef) + (jp ? 1 : 0);
        return;
    }
    init_forms();
    uint32_t iso = iso_forms[base - FIRST];
    if(base == ALEF_MAKSURA && jn) t->c[k] = jp ? ALEF_MAKSURA_MED : ALEF_MAKSURA_INI;
    else t->c[k] = jp && jn ? iso + 3 : jp ? iso + 1 : jn ? iso + 2 : iso;
}

static bool insert_at(ar_term_t * t, uint16_t k, uint32_t c)
{
    if(t->n >= AR_MAX_CHARS) return false;
    memmove(&t->c[k + 1], &t->c[k], (t->n - k) * sizeof(uint32_t));
    t->c[k] = c;
    t->n++;
    return true;
}

static void remove_at(ar_term_t * t, uint16_t k)
{
    memmove(&t->c[k], &t->c[k + 1], (t->n - k - 1) * sizeof(uint32_t));
    t->n--;
}

/*Typed left to right inside the right-to-left field (pushed): digits and Latin letters*/
static bool is_ltr(uint32_t c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= 0x0660 && c <= 0x0669) || (c >= 0x06F0 && c <= 0x06F9);
}

/*Right to left: Arabic letters (typed or stored as forms) and the Arabic
 *punctuation that Unicode counts as a letter (؛ ؟). The Arabic comma and
 *the Arabic decimal and thousands separators are neutral, like "." and ","*/
static bool is_rtl(uint32_t c)
{
    if(c >= 0x0600 && c <= 0x06FF) return !is_ltr(c) && c != 0x060C && c != 0x066B && c != 0x066C;
    return (c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFC);
}

/*Spaces and punctuation: they take the direction of the text around them*/
static bool is_neutral(uint32_t c)
{
    return !is_ltr(c) && !is_rtl(c);
}

/*A bracket typed right to left is drawn as its mirror image*/
static uint32_t mirror(uint32_t c)
{
    static const char pairs[] = "()[]{}<>";
    for(int i = 0; pairs[i]; i++) {
        if((uint32_t)pairs[i] == c) return (uint32_t)pairs[i ^ 1];
    }
    return c;
}

/*Start of the left-to-right run ending at s[end - 1]: digits and Latin letters
 *with the spaces and punctuation between them ("3.14", "10:30", "ESP32-S3")*/
static size_t ltr_start(const uint32_t * s, size_t end)
{
    size_t a = end - 1;
    for(;;) {
        size_t b = a;
        while(b > 0 && is_neutral(s[b - 1])) b--;
        if(b > 0 && is_ltr(s[b - 1])) a = b - 1;
        else return a;
    }
}

/*End of the left-to-right run starting at s[start]*/
static size_t ltr_end(const uint32_t * s, size_t n, size_t start)
{
    size_t e = start + 1;
    for(;;) {
        size_t b = e;
        while(b < n && is_neutral(s[b])) b++;
        if(b < n && is_ltr(s[b])) e = b + 1;
        else return e;
    }
}

/*End the push segment: the spaces and punctuation typed at its end go to its
 *left, the first one typed nearest to it (in right-to-left text they come
 *after the Latin text; brackets turn round), and the caret goes left of it all*/
static void end_push(ar_term_t * t)
{
    uint16_t start = t->push_start, end = t->push_end;
    uint16_t first = end;
    while(first > start && is_neutral(t->c[first - 1])) first--;
    for(uint16_t k = first; k < end; k++) {
        uint32_t c = t->c[k];
        remove_at(t, k);
        insert_at(t, start, mirror(c));
    }
    t->push = false;
    t->caret = start;
}

/*Push again into the left-to-right run that holds c[k]: with the caret inside
 *the run it stays where it is; at the run's left edge (where the caret goes
 *when an Arabic letter ends a push) it goes to the run's end, after the
 *character typed last*/
static void resume_push(ar_term_t * t, uint16_t k)
{
    uint16_t start = (uint16_t)ltr_start(t->c, k + 1);
    uint16_t end = (uint16_t)ltr_end(t->c, t->n, k);
    t->push = true;
    if(start < k) {
        t->push_start = start;
        t->push_end = k;
    }
    else {
        t->push_start = k;
        t->push_end = end;
    }
    t->caret = t->push_end;
}

/*********************************************************************
 * Editing
 *********************************************************************/

void ar_term_type(ar_term_t * t, uint32_t c)
{
    if(c < 0x20 || c == 0x7F) return; /*one line; keys come as signals*/

    /*pushed: left to right at the end of the segment; spaces and punctuation
     *typed while pushing stay in it until an Arabic letter comes*/
    if(is_ltr(c) || (t->push && !is_rtl(c))) {
        if(!t->push) {
            t->push = true;
            t->push_start = t->push_end = t->caret;
        }
        if(!insert_at(t, t->push_end, c)) return;
        t->push_end++;
        reshape(t, t->push_start - 1);
        reshape(t, t->push_end);
        t->caret = t->push_end; /*the caret follows the pushed text*/
        return;
    }

    if(t->push) end_push(t);

    /*right to left: in left of the caret, which stays left of it*/
    uint16_t k = t->caret;
    int a = alef_index(c);
    if(a >= 0 && k < t->n && base_of(t->c[k], NULL) == LAM) { /*lam + alef: one ligature*/
        t->c[k] = LIG_FIRST + 2 * a;
    }
    else if(!insert_at(t, k, mirror(c))) {
        return;
    }
    reshape(t, k - 1);
    reshape(t, k);
    reshape(t, k + 1);
}

bool ar_term_backspace(ar_term_t * t)
{
    /*the caret on a number or Latin text (moved there, or left there by an
     *Arabic letter that was then deleted): delete in it as if still pushing*/
    if(!t->push && t->caret < t->n && is_ltr(t->c[t->caret])) resume_push(t, t->caret);

    if(t->push && t->push_end > t->push_start) {
        remove_at(t, --t->push_end);
        reshape(t, t->push_end);
        reshape(t, t->push_start - 1);
        if(t->push_end == t->push_start) t->push = false;
        t->caret = t->push_end;
        return true;
    }
    t->push = false;
    uint16_t k = t->caret;
    if(k >= t->n) return false; /*nothing on its right*/
    if(base_of(t->c[k], NULL) == LIG) t->c[k] = LAM; /*lam-alef: the alef goes*/
    else remove_at(t, k);
    reshape(t, k - 1);
    reshape(t, k);
    /*a number or Latin text typed just before: typing goes on after it*/
    if(k < t->n && is_ltr(t->c[k])) resume_push(t, k);
    return true;
}

bool ar_term_delete(ar_term_t * t)
{
    if(t->push) end_push(t);
    uint16_t k = t->caret;
    if(k == 0) return false; /*nothing on its left*/
    if(is_ltr(t->c[k - 1])) {
        /*next in typing order: inside a number or Latin text the character on
         *the caret's right; at its right edge its first character*/
        uint16_t start = (uint16_t)ltr_start(t->c, k);
        bool inside = k < t->n && ltr_end(t->c, t->n, start) > k;
        uint16_t gone = inside ? k : start;
        remove_at(t, gone);
        if(!inside) t->caret = k - 1;
        reshape(t, gone - 1);
        reshape(t, gone);
        return true;
    }
    uint32_t alef = 0;
    if(base_of(t->c[k - 1], &alef) == LIG) t->c[k - 1] = alef; /*lam-alef: the lam goes*/
    else remove_at(t, --k);
    t->caret = k;
    reshape(t, k - 1);
    reshape(t, k);
    return true;
}

/*********************************************************************
 * Conversions
 *********************************************************************/

void ar_term_from_logical(ar_term_t * t, const uint32_t * in, size_t n)
{
    static uint32_t units[AR_MAX_CHARS]; /*typing order, lam + alef as one ligature*/
    if(n > AR_MAX_CHARS) n = AR_MAX_CHARS;
    size_t m = 0;
    for(size_t k = 0; k < n; k++) {
        int a = k + 1 < n ? alef_index(in[k + 1]) : -1;
        if(in[k] == LAM && a >= 0) {
            units[m++] = LIG_FIRST + 2 * a;
            k++;
        }
        else {
            units[m++] = in[k];
        }
    }

    /*right to left: the last character typed is leftmost, so walk from the end;
     *a run of digits and Latin letters keeps its own order*/
    memset(t, 0, sizeof(*t));
    size_t i = m;
    while(i > 0) {
        if(is_ltr(units[i - 1])) {
            size_t a = ltr_start(units, i);
            for(size_t j = a; j < i; j++) t->c[t->n++] = units[j];
            i = a;
        }
        else {
            t->c[t->n++] = mirror(units[--i]);
        }
    }
    for(int k = 0; k < t->n; k++) reshape(t, k);
}

size_t ar_term_to_logical(const ar_term_t * t, uint32_t * out)
{
    /*display order to typing order: walk from the right; left-to-right runs keep their order*/
    size_t n = 0;
    size_t k = t->n;
    while(k > 0) {
        if(is_ltr(t->c[k - 1])) {
            size_t a = ltr_start(t->c, k);
            for(size_t i = a; i < k; i++) out[n++] = t->c[i];
            k = a;
            continue;
        }
        uint32_t alef = 0;
        uint32_t base = base_of(t->c[--k], &alef);
        if(base == LIG) {
            out[n++] = LAM;
            out[n++] = alef;
        }
        else {
            out[n++] = mirror(base);
        }
    }
    return n;
}
