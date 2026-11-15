# LVGL as an ESP-IDF component. v6.1.2 predates LVGL's own ESP-IDF support, so
# the component is declared here. The sources are this repository's src/
# (three levels up); ./lv_conf.h configures them.
get_filename_component(LVGL_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)
file(GLOB_RECURSE LVGL_SOURCES ${LVGL_ROOT}/src/*.c)

idf_component_register(SRCS ${LVGL_SOURCES}
                       INCLUDE_DIRS . ../../..
                       REQUIRES esp_timer)

# lv_conf.h and lvgl.h are found through the include path
target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)

# 2019-era code: compile as C11, and keep newer GCC warnings from failing the build
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu11 -Wno-error=all -Wno-error)
