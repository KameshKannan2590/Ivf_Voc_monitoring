/**
 * lv_conf.h — LVGL 8.3 reference configuration for CrowPanel ESP32-S3 4.3"
 *
 * NOTE: This file is NOT active at build time.
 * The LVGL managed component (lvgl/lvgl >=8.3) defaults to CONFIG_LV_CONF_SKIP=y,
 * which causes lv_conf_internal.h to bypass this file entirely and use Kconfig
 * values from sdkconfig instead.
 *
 * Active configuration is set in sdkconfig.defaults via CONFIG_LV_* keys.
 * This file is kept as a reference for which options were chosen and why.
 * Tick source is handled by lv_tick_inc() in lvgl_port/lvgl_port.c.
 */

#if 1  /* Set to 1 to enable */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*-------------------------
 * Color settings
 *-------------------------*/
#define LV_COLOR_DEPTH 16
/*
 * LV_COLOR_16_SWAP=0 for native esp_lcd_panel_rgb (parallel DMA — native LE order).
 * The CrowPanel Arduino GFX example used swap=1 because Arduino GFX draws BE bitmaps;
 * esp_lcd_panel_draw_bitmap passes memory directly to DMA in little-endian format.
 */
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_SCREEN_TRANSP 0
#define LV_COLOR_MIX_ROUND_OFS 0

/*-------------------------
 * Memory
 *-------------------------*/
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM
    /* Use esp_heap_caps to keep LVGL allocations in internal byte-accessible SRAM
     * (MALLOC_CAP_8BIT). Matches CrowPanel official lv_conf.h allocation strategy. */
    #define LV_MEM_CUSTOM_INCLUDE "esp_heap_caps.h"
    #define LV_MEM_CUSTOM_ALLOC(size)          heap_caps_malloc(size, MALLOC_CAP_8BIT)
    #define LV_MEM_CUSTOM_FREE(ptr)            heap_caps_free(ptr)
    #define LV_MEM_CUSTOM_REALLOC(ptr, size)   heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT)
#endif

/*-------------------------
 * HAL settings
 *-------------------------*/
#define LV_DISP_DEF_REFR_PERIOD  30   /* matches CrowPanel official lv_conf.h */
#define LV_INDEV_DEF_READ_PERIOD 30   /* touch polling interval ms */

/* Tick provided by esp_timer (see lvgl_port.c) */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE  "esp_timer.h"
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR ((uint32_t)(esp_timer_get_time() / 1000LL))
#endif

#define LV_DPI_DEF 130

/*-------------------------
 * Drawing
 *-------------------------*/
#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_CNT 4
#define LV_IMG_CACHE_DEF_SIZE 0
#define LV_GRADIENT_MAX_STOPS 2
#define LV_GRAD_CACHE_DEF_SIZE 0
#define LV_DITHER_GRADIENT 0
#define LV_DISP_ROT_MAX_BUF (10*1024)

/*-------------------------
 * GPU
 *-------------------------*/
#define LV_USE_GPU_ESP_DMA2D 0

/*-------------------------
 * Logging
 *-------------------------*/
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 1
    #define LV_LOG_TRACE_MEM 0
    #define LV_LOG_TRACE_TIMER 0
    #define LV_LOG_TRACE_INDEV 0
    #define LV_LOG_TRACE_DISP_REFR 0
    #define LV_LOG_TRACE_EVENT 0
    #define LV_LOG_TRACE_OBJ_CREATE 0
    #define LV_LOG_TRACE_LAYOUT 0
    #define LV_LOG_TRACE_ANIM 0
#endif

/*-------------------------
 * Asserts
 *-------------------------*/
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0
#define LV_ASSERT_HANDLER_INCLUDE   <stdint.h>
#define LV_ASSERT_HANDLER           while(1);

/*-------------------------
 * Printf
 *-------------------------*/
#define LV_SPRINTF_CUSTOM 0
#define LV_SPRINTF_USE_FLOAT 0

/*-------------------------
 * Object types
 *-------------------------*/
#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BTN          1
#define LV_USE_BTNMATRIX    1
#define LV_USE_CANVAS       1
#define LV_USE_CHECKBOX     1
#define LV_USE_DROPDOWN     1
#define LV_USE_IMG          1
#define LV_USE_LABEL        1
#define LV_USE_LINE         1
#define LV_USE_LIST         1
#define LV_USE_MENU         1
#define LV_USE_METER        1
#define LV_USE_MSGBOX       1
#define LV_USE_ROLLER       1
#define LV_USE_SLIDER       1
#define LV_USE_SPAN         1
#define LV_USE_SPINBOX      1
#define LV_USE_SPINNER      1
#define LV_USE_SWITCH       1
#define LV_USE_TABLE        1
#define LV_USE_TABVIEW      1
#define LV_USE_TILEVIEW     1
#define LV_USE_WIN          1

/*-------------------------
 * Themes
 *-------------------------*/
#define LV_USE_THEME_DEFAULT    1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 1
    #define LV_THEME_DEFAULT_GROW 0
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif
#define LV_USE_THEME_BASIC  1
#define LV_USE_THEME_MONO   0

/*-------------------------
 * Layouts
 *-------------------------*/
#define LV_USE_FLEX  1
#define LV_USE_GRID  1

/*-------------------------
 * Fonts
 *-------------------------*/
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_16

/* Declare unscii-8 for fallback */
#define LV_FONT_UNSCII_8  0
#define LV_FONT_UNSCII_16 0

/*-------------------------
 * Text
 *-------------------------*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_COLOR_CMD "#"
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*-------------------------
 * Widgets extras
 *-------------------------*/
#define LV_USE_ANIMIMG    0
#define LV_USE_CHART      1
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     0
#define LV_USE_KEYBOARD   0
#define LV_USE_LED        1
#define LV_USE_LOTTIE     0

/*-------------------------
 * FS interface
 *-------------------------*/
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0

/*-------------------------
 * PNG/BMP/JPG decoders
 *-------------------------*/
#define LV_USE_PNG 0
#define LV_USE_BMP 0
#define LV_USE_SJPG 0
#define LV_USE_GIF 0
#define LV_USE_QR_CODE 0
#define LV_USE_BARCODE 0
#define LV_USE_FRAGMENT 0

/*-------------------------
 * OS & Tick
 *-------------------------*/
#define LV_USE_OS LV_OS_FREERTOS

/*-------------------------
 * Profiling
 *-------------------------*/
#define LV_USE_PROFILER 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_REFR_DEBUG 0

#define LV_BUILD_TEST 0

#endif /* LV_CONF_H */
#endif /* End enable content */
