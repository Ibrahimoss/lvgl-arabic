/**
 * @file arabic_utf8.h
 * UTF-8 conversion for design B, and the longest text a field holds.
 */

#ifndef ARABIC_UTF8_H
#define ARABIC_UTF8_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest text a field holds, in code points. */
#ifndef AR_MAX_CHARS
#define AR_MAX_CHARS 128
#endif

/** UTF-8 to code points; stops at `max`. Malformed sequences become U+FFFD. @return number of code points */
size_t ar_utf8_decode(const char * s, uint32_t * out, size_t max);

/** One code point of UTF-8 (U+FFFD if malformed). @return bytes used, 0 at the end of the string */
size_t ar_utf8_next(const char * s, uint32_t * cp);

/** Code points to UTF-8; `out` needs 4*n+1 bytes. @return bytes written (without the '\0') */
size_t ar_utf8_encode(const uint32_t * cps, size_t n, char * out);

#ifdef __cplusplus
}
#endif

#endif /*ARABIC_UTF8_H*/
