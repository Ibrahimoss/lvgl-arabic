# Design C shapes Arabic inside LVGL itself (src/ of this repository); this
# component only adds the Arabic keyboard.
idf_component_register(SRCS arabic_kb.c
                       INCLUDE_DIRS include
                       REQUIRES lvgl)
