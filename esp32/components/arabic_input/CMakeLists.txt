# Design A: Arabic input for LVGL v6 with a double buffer.
#   arabic_text.c   the text engine: shaping, bidi, layout (no LVGL)
#   arabic_ta.c     the LVGL glue: the double-buffered text area and labels
#   arabic_kb.c     the Arabic keyboard
idf_component_register(SRCS arabic_text.c arabic_ta.c arabic_kb.c
                       INCLUDE_DIRS include
                       REQUIRES lvgl)
