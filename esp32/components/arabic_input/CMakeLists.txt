# Design B: shape while typing, store what is shown (terminal style).
#   arabic_term_text.c  the text rules (no LVGL)
#   arabic_term.c       the LVGL glue: the terminal-style field
#   arabic_utf8.c       UTF-8 conversion
#   arabic_kb.c         the Arabic keyboard
idf_component_register(SRCS arabic_term_text.c arabic_term.c arabic_utf8.c arabic_kb.c
                       INCLUDE_DIRS include
                       REQUIRES lvgl)
