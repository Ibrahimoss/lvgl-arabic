/**
 * @file arabic_utf8.c
 * UTF-8 conversion. See arabic_utf8.h.
 */

#include "arabic_utf8.h"

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
