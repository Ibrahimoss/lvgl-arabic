/**
 * @file arabic_text.c
 * Arabic shaping, bidi levels, line breaking and caret maps. See arabic_text.h.
 */

#include "arabic_text.h"
#include "arabic_tables.h"
#include <string.h>

/*********************************************************************
 * Shaping
 *********************************************************************/

static const ar_forms_t * find_forms(uint32_t c)
{
    size_t lo = 0;
    size_t hi = sizeof(AR_FORMS) / sizeof(AR_FORMS[0]);
    while(lo < hi) {
        size_t mid = (lo + hi) / 2;
        if(AR_FORMS[mid].cp == c) return &AR_FORMS[mid];
        if(AR_FORMS[mid].cp < c) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

/*Joining_Type (ArabicShaping.txt): ARJ_U, R, L, D, C or T*/
static uint8_t join_type(uint32_t c)
{
    size_t lo = 0;
    size_t hi = sizeof(AR_JOIN_RANGES) / sizeof(AR_JOIN_RANGES[0]);
    while(lo < hi) {
        size_t mid = (lo + hi) / 2;
        if(c < AR_JOIN_RANGES[mid][0]) hi = mid;
        else if(c > AR_JOIN_RANGES[mid][1]) lo = mid + 1;
        else return (uint8_t)AR_JOIN_RANGES[mid][2];
    }
    return ARJ_U;
}

/*The form for a letter joined on the given sides. The table only has the
 *forms the font has, so fall back to the nearest one it does have.*/
static uint32_t pick_form(const ar_forms_t * f, uint32_t c, bool prev, bool next)
{
    if(prev && next && f->med) return f->med;
    if(prev && f->fin) return f->fin;
    if(next && f->ini) return f->ini;
    if(f->iso) return f->iso;
    return c;
}

/*Isolated lam-alef ligature for an alef variant; final form is +1. 0 if not an alef.*/
static uint32_t lam_alef(uint32_t alef)
{
    switch(alef) {
        case 0x0622: return 0xFEF5; /*lam + alef madda*/
        case 0x0623: return 0xFEF7; /*lam + alef hamza above*/
        case 0x0625: return 0xFEF9; /*lam + alef hamza below*/
        case 0x0627: return 0xFEFB; /*lam + alef*/
        default: return 0;
    }
}

size_t ar_shape(const uint32_t * t, size_t n, ar_glyph_t * out)
{
    size_t k = 0;
    for(size_t i = 0; i < n; i++) {
        ar_glyph_t * g = &out[k++];
        g->cp = t[i];
        g->src = (uint16_t)i;
        g->src_len = 1;
        g->level = 0;

        const ar_forms_t * f = find_forms(t[i]);
        if(f == NULL) continue; /*not a shaped letter: drawn as is*/

        uint8_t jt = join_type(t[i]);

        /*Nearest neighbours that are not transparent (marks, format characters)*/
        uint8_t prev_jt = ARJ_U;
        for(size_t p = i; p > 0;) {
            p--;
            uint8_t j = join_type(t[p]);
            if(j != ARJ_T) {
                prev_jt = j;
                break;
            }
        }
        uint8_t next_jt = ARJ_U;
        for(size_t q = i + 1; q < n; q++) {
            uint8_t j = join_type(t[q]);
            if(j != ARJ_T) {
                next_jt = j;
                break;
            }
        }

        /*in logical order: the letter before joins on its left side (D, L, C),
         *this one on its right side (D, R)*/
        bool join_prev = (prev_jt == ARJ_D || prev_jt == ARJ_L || prev_jt == ARJ_C) && (jt == ARJ_D || jt == ARJ_R);

        /*lam directly followed by an alef: one ligature glyph for both*/
        if(t[i] == 0x0644 && i + 1 < n) {
            uint32_t lig = lam_alef(t[i + 1]);
            if(lig) {
                g->cp = join_prev ? lig + 1 : lig;
                g->src_len = 2;
                i++;
                continue;
            }
        }

        bool join_next = (jt == ARJ_D || jt == ARJ_L) && (next_jt == ARJ_D || next_jt == ARJ_R || next_jt == ARJ_C);
        g->cp = pick_form(f, t[i], join_prev, join_next);
    }
    return k;
}

/*********************************************************************
 * Bidi (UBA rules W1-W7, N0-N2, I1-I2, L1-L2, L4; no explicit embeddings)
 *********************************************************************/

enum {
    BC_L = ARB_L, BC_R = ARB_R, BC_AL = ARB_AL, BC_EN = ARB_EN, BC_AN = ARB_AN, BC_ES = ARB_ES, BC_ET = ARB_ET,
    BC_CS = ARB_CS, BC_NSM = ARB_NSM, BC_WS = ARB_WS, BC_ON = ARB_ON, BC_S = ARB_S, BC_B = ARB_B, BC_BN = ARB_BN
};

/*Bidi_Class from the Unicode data (arabic_tables.h). Explicit embeddings,
 *overrides and isolates are not supported and come back as BN, like the
 *zero-width characters: left out before resolving (UBA X9).*/
static uint8_t bidi_class(uint32_t c)
{
    size_t lo = 0;
    size_t hi = sizeof(AR_BIDI_RANGES) / sizeof(AR_BIDI_RANGES[0]);
    while(hi - lo > 1) { /*the last range starting at or before c*/
        size_t mid = (lo + hi) / 2;
        if((AR_BIDI_RANGES[mid] >> 8) <= c) lo = mid;
        else hi = mid;
    }
    return (uint8_t)(AR_BIDI_RANGES[lo] & 0xFF);
}

static bool is_ignorable(uint32_t c)
{
    return bidi_class(c) == BC_BN;
}

/*Explicit formatting characters: ignored here, and not passed on to LVGL,
 *whose own BiDi would act on RLO (U+202E)*/
static bool is_explicit_format(uint32_t c)
{
    return (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069);
}

static bool is_neutral(uint8_t t)
{
    return t == BC_WS || t == BC_ON || t == BC_S || t == BC_B;
}

/*Strong direction for N0 (digits count as right to left), or BC_ON if none*/
static uint8_t strong_dir(uint8_t t)
{
    if(t == BC_L) return BC_L;
    if(t == BC_R || t == BC_EN || t == BC_AN) return BC_R;
    return BC_ON;
}

/*U+2329 and U+232A are canonically equivalent to U+3008 and U+3009 (BD16)*/
static uint32_t bracket_norm(uint32_t c)
{
    return c == 0x2329 ? 0x3008 : c == 0x232A ? 0x3009 : c;
}

/*Bidi_Paired_Bracket of c (normalized), 0 if c is not a paired bracket*/
static uint32_t bracket_pair(uint32_t c, bool * opening)
{
    c = bracket_norm(c);
    size_t lo = 0;
    size_t hi = sizeof(AR_BRACKETS) / sizeof(AR_BRACKETS[0]);
    while(lo < hi) {
        size_t mid = (lo + hi) / 2;
        if(AR_BRACKETS[mid][0] == c) {
            *opening = AR_BRACKETS[mid][2] != 0;
            return bracket_norm(AR_BRACKETS[mid][1]);
        }
        if(AR_BRACKETS[mid][0] < c) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}

/*BD16 + N0: a bracket pair takes the direction of what it encloses, so both
 *brackets end up on the same side of a run of the other direction.
 *t[i] is the type of glyph g[idx[i]].*/
static void resolve_brackets(const ar_glyph_t * g, const uint16_t * idx, size_t n, uint8_t * t, const uint8_t * orig,
                             uint8_t sor)
{
    enum { STACK = 63 }; /*BD16: deeper nesting stops the pairing*/
    uint16_t stack[STACK];
    uint16_t open[AR_MAX_CHARS / 2];
    uint16_t close[AR_MAX_CHARS / 2];
    size_t sp = 0;
    size_t np = 0;

    for(size_t i = 0; i < n; i++) {
        if(t[i] != BC_ON) continue;
        bool opening;
        if(bracket_pair(g[idx[i]].cp, &opening) == 0) continue;
        if(opening) {
            if(sp == STACK) break;
            stack[sp++] = (uint16_t)i;
        }
        else {
            uint32_t c = bracket_norm(g[idx[i]].cp);
            for(size_t k = sp; k > 0; k--) {
                bool o;
                if(bracket_pair(g[idx[stack[k - 1]]].cp, &o) == c) {
                    /*keep the pairs sorted by their opening bracket*/
                    size_t at = np++;
                    while(at > 0 && open[at - 1] > stack[k - 1]) {
                        open[at] = open[at - 1];
                        close[at] = close[at - 1];
                        at--;
                    }
                    open[at] = stack[k - 1];
                    close[at] = (uint16_t)i;
                    sp = k - 1;
                    break;
                }
            }
        }
    }

    for(size_t p = 0; p < np; p++) {
        bool same = false;
        bool opposite = false;
        for(size_t k = open[p] + 1u; k < close[p]; k++) {
            uint8_t d = strong_dir(t[k]);
            if(d == sor) same = true;
            else if(d != BC_ON) opposite = true;
        }
        uint8_t dir;
        if(same) dir = sor;
        else if(opposite) {
            /*only the other direction inside: keep it if the text before the pair has it too*/
            uint8_t before = sor;
            for(size_t k = open[p]; k > 0; k--) {
                uint8_t d = strong_dir(t[k - 1]);
                if(d != BC_ON) {
                    before = d;
                    break;
                }
            }
            dir = before == sor ? sor : before;
        }
        else continue; /*nothing strong inside: N1/N2 decide*/

        uint16_t ends[2] = {open[p], close[p]};
        for(size_t e = 0; e < 2; e++) {
            t[ends[e]] = dir;
            for(size_t k = ends[e] + 1u; k < n && orig[k] == BC_NSM; k++) t[k] = dir; /*marks on a bracket follow it*/
        }
    }
}

bool ar_text_is_rtl(const uint32_t * text, size_t n, bool fallback)
{
    for(size_t i = 0; i < n; i++) {
        uint8_t bc = bidi_class(text[i]);
        if(bc == BC_L) return false;
        if(bc == BC_R || bc == BC_AL) return true;
    }
    return fallback;
}

void ar_bidi_levels(ar_glyph_t * g, size_t count, bool base_rtl)
{
    uint8_t t[AR_MAX_CHARS];
    uint8_t orig[AR_MAX_CHARS];
    uint16_t idx[AR_MAX_CHARS]; /*the glyphs that take part: X9 leaves out the ignorable ones*/
    if(count > AR_MAX_CHARS) count = AR_MAX_CHARS;
    const uint8_t sor = base_rtl ? BC_R : BC_L;

    size_t n = 0;
    for(size_t i = 0; i < count; i++) {
        if(is_ignorable(g[i].cp)) continue;
        idx[n] = (uint16_t)i;
        t[n] = orig[n] = bidi_class(g[i].cp);
        n++;
    }

    /*W1: marks take the type of what they are on*/
    for(size_t i = 0; i < n; i++) {
        if(t[i] == BC_NSM) t[i] = i ? t[i - 1] : sor;
    }
    /*W2: European digits after Arabic letters act as Arabic digits. W3: AL -> R*/
    uint8_t strong = sor;
    for(size_t i = 0; i < n; i++) {
        if(t[i] == BC_L || t[i] == BC_R || t[i] == BC_AL) strong = t[i];
        else if(t[i] == BC_EN && strong == BC_AL) t[i] = BC_AN;
    }
    for(size_t i = 0; i < n; i++) {
        if(t[i] == BC_AL) t[i] = BC_R;
    }
    /*W4: a single separator between two numbers of the same kind joins them*/
    for(size_t i = 1; i + 1 < n; i++) {
        if(t[i] == BC_ES && t[i - 1] == BC_EN && t[i + 1] == BC_EN) t[i] = BC_EN;
        else if(t[i] == BC_CS && t[i - 1] == t[i + 1] && (t[i - 1] == BC_EN || t[i - 1] == BC_AN)) t[i] = t[i - 1];
    }
    /*W5: currency / percent signs next to European digits*/
    for(size_t i = 0; i < n;) {
        if(t[i] != BC_ET) {
            i++;
            continue;
        }
        size_t j = i;
        while(j < n && t[j] == BC_ET) j++;
        bool en = (i > 0 && t[i - 1] == BC_EN) || (j < n && t[j] == BC_EN);
        for(size_t k = i; k < j; k++) t[k] = en ? BC_EN : BC_ET;
        i = j;
    }
    /*W6: remaining separators are neutral*/
    for(size_t i = 0; i < n; i++) {
        if(t[i] == BC_ES || t[i] == BC_ET || t[i] == BC_CS) t[i] = BC_ON;
    }
    /*W7: European digits in a Latin context are Latin*/
    strong = sor;
    for(size_t i = 0; i < n; i++) {
        if(t[i] == BC_L || t[i] == BC_R) strong = t[i];
        else if(t[i] == BC_EN && strong == BC_L) t[i] = BC_L;
    }
    /*N0: bracket pairs*/
    resolve_brackets(g, idx, n, t, orig, sor);
    /*N1/N2: neutrals (spaces, separators, other neutrals) between the same
     *direction take it, otherwise the paragraph's*/
    for(size_t i = 0; i < n;) {
        if(!is_neutral(t[i])) {
            i++;
            continue;
        }
        size_t j = i;
        while(j < n && is_neutral(t[j])) j++;
        uint8_t before = i > 0 ? (t[i - 1] == BC_L ? BC_L : BC_R) : sor;
        uint8_t after = j < n ? (t[j] == BC_L ? BC_L : BC_R) : sor;
        uint8_t dir = before == after ? before : sor;
        for(size_t k = i; k < j; k++) t[k] = dir;
        i = j;
    }
    /*I1/I2: implicit levels*/
    const uint8_t base = base_rtl ? 1 : 0;
    for(size_t i = 0; i < n; i++) {
        uint8_t lv = base;
        if(base == 0) {
            if(t[i] == BC_R) lv = 1;
            else if(t[i] == BC_AN || t[i] == BC_EN) lv = 2;
        }
        else {
            if(t[i] == BC_L || t[i] == BC_AN || t[i] == BC_EN) lv = 2;
        }
        g[idx[i]].level = lv;
    }
    /*the ignored characters take the level of the character before them*/
    uint8_t prev = base;
    for(size_t i = 0, k = 0; i < count; i++) {
        if(k < n && idx[k] == i) {
            prev = g[i].level;
            k++;
        }
        else {
            g[i].level = prev;
        }
    }
}

/*Effective levels of a line and its visual order. L1: segment and paragraph
 *separators (tab), the whitespace before them and the whitespace at the end
 *of the line go to the paragraph level.*/
static void line_levels_order(const ar_glyph_t * g, size_t n, bool base_rtl, uint8_t * lev, uint16_t * order)
{
    for(size_t i = 0; i < n; i++) {
        lev[i] = g[i].level;
        order[i] = (uint16_t)i;
    }
    const uint8_t para = base_rtl ? 1 : 0;
    bool reset = true; /*the end of the line counts like a separator*/
    for(size_t i = n; i > 0; i--) {
        uint8_t c = bidi_class(g[i - 1].cp);
        if(c == BC_S || c == BC_B) {
            lev[i - 1] = para;
            reset = true;
        }
        else if(reset && (c == BC_WS || c == BC_BN)) {
            lev[i - 1] = para;
        }
        else {
            reset = false;
        }
    }

    uint8_t max = 0;
    uint8_t min = 0xFF;
    for(size_t i = 0; i < n; i++) {
        if(lev[i] > max) max = lev[i];
        if(lev[i] < min) min = lev[i];
    }
    if(n == 0) return;
    uint8_t lowest_odd = (min & 1) ? min : (uint8_t)(min + 1);

    /*L2: from the highest level down to the lowest odd one, reverse every run at that level or above*/
    for(int lv = max; lv >= lowest_odd; lv--) {
        for(size_t i = 0; i < n;) {
            if(lev[order[i]] < lv) {
                i++;
                continue;
            }
            size_t j = i;
            while(j < n && lev[order[j]] >= lv) j++;
            for(size_t a = i, b = j - 1; a < b; a++, b--) {
                uint16_t tmp = order[a];
                order[a] = order[b];
                order[b] = tmp;
            }
            i = j;
        }
    }
}

void ar_line_order(const ar_glyph_t * g, size_t n, bool base_rtl, uint16_t * order)
{
    uint8_t lev[AR_MAX_CHARS];
    if(n > AR_MAX_CHARS) n = AR_MAX_CHARS;
    line_levels_order(g, n, base_rtl, lev, order);
}

uint32_t ar_mirror(uint32_t cp)
{
    size_t lo = 0;
    size_t hi = sizeof(AR_MIRRORS) / sizeof(AR_MIRRORS[0]);
    while(lo < hi) {
        size_t mid = (lo + hi) / 2;
        if(AR_MIRRORS[mid][0] == cp) return AR_MIRRORS[mid][1];
        if(AR_MIRRORS[mid][0] < cp) lo = mid + 1;
        else hi = mid;
    }
    return cp;
}

/*********************************************************************
 * UTF-8
 *********************************************************************/

size_t ar_utf8_next(const char * s, uint32_t * cp)
{
    const uint8_t * p = (const uint8_t *)s;
    uint32_t c;
    uint32_t min; /*smallest code point that needs this many bytes*/
    size_t len;
    if(p[0] == 0) return 0;
    if(p[0] < 0x80) {
        c = p[0];
        min = 0;
        len = 1;
    }
    else if((p[0] & 0xE0) == 0xC0) {
        c = p[0] & 0x1F;
        min = 0x80;
        len = 2;
    }
    else if((p[0] & 0xF0) == 0xE0) {
        c = p[0] & 0x0F;
        min = 0x800;
        len = 3;
    }
    else if((p[0] & 0xF8) == 0xF0) {
        c = p[0] & 0x07;
        min = 0x10000;
        len = 4;
    }
    else {
        *cp = 0xFFFD;
        return 1;
    }
    size_t k = 1;
    for(; k < len; k++) {
        if((p[k] & 0xC0) != 0x80) break;
        c = (c << 6) | (p[k] & 0x3F);
    }
    /*truncated, overlong (C0 80 would be a NUL), surrogate or beyond Unicode*/
    if(k < len || c < min || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) c = 0xFFFD;
    *cp = c;
    return k;
}

size_t ar_utf8_decode(const char * s, uint32_t * out, size_t max)
{
    size_t n = 0;
    size_t used;
    while(n < max && (used = ar_utf8_next(s, &out[n])) > 0) {
        s += used;
        n++;
    }
    return n;
}

static size_t utf8_put(uint32_t c, char * o)
{
    if(c < 0x80) {
        o[0] = (char)c;
        return 1;
    }
    if(c < 0x800) {
        o[0] = (char)(0xC0 | (c >> 6));
        o[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if(c < 0x10000) {
        o[0] = (char)(0xE0 | (c >> 12));
        o[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        o[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    o[0] = (char)(0xF0 | (c >> 18));
    o[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    o[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

size_t ar_utf8_encode(const uint32_t * cps, size_t n, char * out)
{
    size_t b = 0;
    for(size_t i = 0; i < n; i++) b += utf8_put(cps[i], out + b);
    out[b] = '\0';
    return b;
}

/*********************************************************************
 * Layout
 *********************************************************************/

/*Glyphs [0, n) that fit max_w; breaks after a space when it can*/
static size_t fit_line(const ar_glyph_t * g, size_t n, int32_t max_w, ar_width_cb_t cb, void * ctx)
{
    int32_t w = 0;
    size_t last_break = 0;
    for(size_t i = 0; i < n; i++) {
        int32_t cw = cb(g[i].cp, i + 1 < n ? g[i + 1].cp : 0, ctx);
        if(w + cw > max_w && i > 0) {
            if(g[i].cp == ' ') return i + 1;   /*the space ends this line*/
            return last_break ? last_break : i; /*a word longer than the line is cut*/
        }
        w += cw;
        if(g[i].cp == ' ') last_break = i + 1;
    }
    return n;
}

static size_t glyph_at(const ar_glyph_t * g, size_t n, size_t src)
{
    for(size_t j = 0; j < n; j++) {
        if(src >= g[j].src && src < (size_t)g[j].src + g[j].src_len) return j;
    }
    return n ? n - 1 : 0;
}

void ar_layout(const uint32_t * text, size_t n, ar_dir_t dir, int32_t max_width,
               ar_width_cb_t width_cb, void * ctx, ar_layout_t * out)
{
    ar_glyph_t g[AR_MAX_CHARS];
    uint16_t order[AR_MAX_CHARS];
    uint16_t vpos[AR_MAX_CHARS];
    uint8_t lev[AR_MAX_CHARS];

    memset(out, 0, sizeof(*out));
    if(n > AR_MAX_CHARS) {
        n = AR_MAX_CHARS;
        out->truncated = true;
    }
    out->logical_len = (uint16_t)n;
    memset(out->caret_ok, 1, sizeof(out->caret_ok));
    memset(out->char_disp, 0xFF, sizeof(out->char_disp)); /*AR_NO_GLYPH until laid out*/

    size_t bytes = 0;
    uint16_t dpos = 0;
    size_t pstart = 0;

    while(1) {
        /*a paragraph ends at a paragraph separator (LF, CR, CRLF, NEL, U+2029...)*/
        size_t pend = pstart;
        while(pend < n && bidi_class(text[pend]) != BC_B) pend++;
        size_t plen = pend - pstart;

        bool rtl = dir == AR_DIR_RTL ||
                   (dir == AR_DIR_AUTO && ar_text_is_rtl(text + pstart, plen, true));
        size_t gn = ar_shape(text + pstart, plen, g);
        ar_bidi_levels(g, gn, rtl);

        size_t a = 0;
        do {
            size_t cnt = gn - a;
            if(max_width > 0 && width_cb && cnt) cnt = fit_line(g + a, cnt, max_width, width_cb, ctx);

            if(out->line_cnt == AR_MAX_LINES) {
                out->truncated = true;
                break;
            }
            if(out->line_cnt > 0) { /*line separator before every line but the first*/
                out->utf8[bytes++] = '\n';
                dpos++;
            }

            ar_line_t * L = &out->lines[out->line_cnt++];
            L->display_start = dpos;
            L->count = (uint16_t)cnt;
            L->rtl = rtl;
            L->src_start = (uint16_t)(pstart + (cnt ? g[a].src : plen));
            L->src_end = (uint16_t)(pstart + (cnt ? g[a + cnt - 1].src + g[a + cnt - 1].src_len : plen));

            const ar_glyph_t * lg = g + a;
            line_levels_order(lg, cnt, rtl, lev, order);
            for(size_t v = 0; v < cnt; v++) vpos[order[v]] = (uint16_t)v;

            for(size_t v = 0; v < cnt; v++) {
                const ar_glyph_t * gl = &lg[order[v]];
                uint32_t cp = (lev[order[v]] & 1) ? ar_mirror(gl->cp) : gl->cp;
                if(is_explicit_format(cp)) cp = 0x200B; /*keep the caret maps, drop the control*/
                bytes += utf8_put(cp, out->utf8 + bytes);
            }
            for(size_t j = 0; j < cnt; j++) {
                for(size_t s = 0; s < lg[j].src_len; s++) {
                    out->char_rtl[pstart + lg[j].src + s] = lev[j] & 1;
                    out->char_disp[pstart + lg[j].src + s] = (uint16_t)(L->display_start + vpos[j]);
                    if(s > 0) out->caret_ok[pstart + lg[j].src + s] = 0; /*between lam and alef*/
                }
            }

            /*Caret maps. Where two directions meet, the caret belongs to the glyph
             *with the higher level (the more deeply embedded run), so every
             *logical position gets its own spot on the line.*/

            /*display caret -> logical caret: the caret touches the right edge of
             *the glyph on its left (A) and the left edge of the one on its right (B)*/
            for(size_t v = 0; v <= cnt; v++) {
                size_t A = v > 0 ? order[v - 1] : SIZE_MAX;
                size_t B = v < cnt ? order[v] : SIZE_MAX;
                bool use_b = A == SIZE_MAX || (B != SIZE_MAX && lev[B] > lev[A]);
                size_t c;
                if(A == SIZE_MAX && B == SIZE_MAX) c = L->src_start - pstart;
                else if(use_b) c = (lev[B] & 1) ? lg[B].src + lg[B].src_len : lg[B].src; /*left edge of B*/
                else c = (lev[A] & 1) ? lg[A].src : lg[A].src + lg[A].src_len;         /*right edge of A*/
                out->to_logical[L->display_start + v] = (uint16_t)(pstart + c);
            }

            /*logical caret -> display caret (a caret on a soft wrap goes to the next line)*/
            for(size_t c = L->src_start; c <= L->src_end; c++) {
                size_t rel = c - pstart;
                size_t v = 0;
                if(cnt) {
                    size_t jb = c > L->src_start ? glyph_at(lg, cnt, rel - 1) : SIZE_MAX; /*glyph before the caret*/
                    size_t ja = c < L->src_end ? glyph_at(lg, cnt, rel) : SIZE_MAX;       /*glyph after it*/
                    bool use_after = jb == SIZE_MAX || (ja != SIZE_MAX && lev[ja] > lev[jb]);
                    if(use_after) v = (lev[ja] & 1) ? (size_t)vpos[ja] + 1 : vpos[ja];     /*before glyph ja*/
                    else v = (lev[jb] & 1) ? vpos[jb] : (size_t)vpos[jb] + 1;              /*after glyph jb*/
                }
                out->to_display[c] = (uint16_t)(L->display_start + v);
            }

            dpos += (uint16_t)cnt;
            a += cnt;
        } while(a < gn);

        if(out->truncated && out->line_cnt == AR_MAX_LINES && a < gn) break;
        if(pend >= n) break;
        if(text[pend] == '\r' && pend + 1 < n && text[pend + 1] == '\n') {
            /*CRLF is one separator: no caret between CR and LF*/
            out->caret_ok[pend + 1] = 0;
            out->to_display[pend + 1] = out->to_display[pend];
            pend++;
        }
        pstart = pend + 1; /*skip the separator*/
    }

    out->utf8[bytes] = '\0';
    out->display_len = dpos;
}

uint16_t ar_layout_line_of(const ar_layout_t * lay, uint16_t spot)
{
    for(uint16_t i = 0; i < lay->line_cnt; i++) {
        const ar_line_t * L = &lay->lines[i];
        if(spot <= L->display_start + L->count) return i;
    }
    return lay->line_cnt ? (uint16_t)(lay->line_cnt - 1) : 0;
}

uint16_t ar_layout_line_edge(const ar_layout_t * lay, uint16_t line, bool end)
{
    if(line >= lay->line_cnt) return 0;
    const ar_line_t * L = &lay->lines[line];
    bool right = L->rtl ? !end : end; /*a right-to-left line starts at its right edge*/
    return right ? (uint16_t)(L->display_start + L->count) : L->display_start;
}

uint16_t ar_layout_step(const ar_layout_t * lay, uint16_t spot, int dir)
{
    if(lay->line_cnt == 0 || dir == 0) return spot;
    uint16_t li = ar_layout_line_of(lay, spot);
    const ar_line_t * L = &lay->lines[li];
    if(spot > L->display_start + L->count) spot = L->display_start + L->count;

    /*spots are glyph edges, so a ligature is crossed in one step*/
    if(dir < 0 && spot > L->display_start) return spot - 1;
    if(dir > 0 && spot < L->display_start + L->count) return spot + 1;

    /*visual edge of the line: continue on the next or previous line. Which
     *one follows from the text's alignment (its first paragraph), not from
     *this line's direction, so lines of mixed direction are passed in order;
     *the new line is entered at the edge the caret moves away from.*/
    bool forward = lay->lines[0].rtl ? dir < 0 : dir > 0;
    int to = forward ? li + 1 : (int)li - 1;
    if(to < 0 || to >= lay->line_cnt) return spot;
    const ar_line_t * N = &lay->lines[to];
    return dir < 0 ? (uint16_t)(N->display_start + N->count) : N->display_start;
}

uint16_t ar_layout_spot_after(const ar_layout_t * lay, uint16_t caret)
{
    if(caret > lay->logical_len) caret = lay->logical_len;
    if(caret == 0 || lay->char_disp[caret - 1] == AR_NO_GLYPH) {
        return ar_layout_line_edge(lay, ar_layout_line_of(lay, lay->to_display[caret]), false);
    }
    /*the trailing edge of that character's glyph: its left edge if it runs right to left*/
    uint16_t d = lay->char_disp[caret - 1];
    return lay->char_rtl[caret - 1] ? d : (uint16_t)(d + 1);
}
