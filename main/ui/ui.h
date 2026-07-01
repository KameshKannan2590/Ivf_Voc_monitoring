#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * IVF VOC Monitor — Light Consumer Theme
 * Portrait 272x480  (physical 480x272 rotated 90 by LVGL)
 * ================================================================ */

/* Color palette */
#define IVF_COLOR_BG            lv_color_hex(0xFFFFFF)
#define IVF_COLOR_CARD          lv_color_hex(0xFFFFFF)
#define IVF_COLOR_BORDER        lv_color_hex(0xE0E0E0)
#define IVF_COLOR_PRIMARY       lv_color_hex(0x1A73E8)
#define IVF_COLOR_TEXT          lv_color_hex(0x212121)
#define IVF_COLOR_TEXT_MUTED    lv_color_hex(0x757575)
#define IVF_COLOR_GOOD          lv_color_hex(0x43A047)
#define IVF_COLOR_WARNING       lv_color_hex(0xFB8C00)
#define IVF_COLOR_DANGER        lv_color_hex(0xE53935)
#define IVF_COLOR_NAV           lv_color_hex(0xF8F9FA)
#define IVF_COLOR_NAV_ACTIVE    lv_color_hex(0xE8F0FE)
#define IVF_COLOR_NAV_INACTIVE  lv_color_hex(0x9E9E9E)

/* Typography (montserrat sizes already enabled in sdkconfig) */
#define IVF_FONT_SMALL   (&lv_font_montserrat_12)
#define IVF_FONT_NORMAL  (&lv_font_montserrat_16)
#define IVF_FONT_MEDIUM  (&lv_font_montserrat_20)
#define IVF_FONT_LARGE   (&lv_font_montserrat_24)
#define IVF_FONT_XLARGE  (&lv_font_montserrat_32)
#define IVF_FONT_HUGE    (&lv_font_montserrat_48)

/* Portrait layout constants */
#define IVF_SCREEN_W      272
#define IVF_SCREEN_H      480
#define IVF_HEADER_H       50
#define IVF_CONTENT_H     (IVF_SCREEN_H - IVF_HEADER_H)  /* 430 */
#define IVF_DRAWER_W      200
#define IVF_NAV_BTN_SIZE   44
#define IVF_CARD_RADIUS     8
#define IVF_PAD             8

/* Screen IDs */
typedef enum {
    SCREEN_SPLASH    = 0,
    SCREEN_DASHBOARD = 1,
    SCREEN_CHART     = 2,
    SCREEN_LOGS      = 3,
    SCREEN_SETTINGS  = 4,
    SCREEN_COUNT
} screen_id_t;

/* Shared builder — call from each screen's _create() */
lv_obj_t *ui_build_header(lv_obj_t *parent, const char *title);

/**
 * Initialize the UI (call after lvgl_port_init).
 * Creates all screens and loads the splash screen.
 */
void ui_init(void);

/**
 * Navigate to a screen with a fade animation.
 * @param target   Destination screen ID.
 * @param forward  Kept for API compatibility with screen_splash; ignored internally.
 */
void ui_goto_screen(screen_id_t target, bool forward);

/**
 * Toggle the navigation drawer open/closed.
 * Called from the header menu button callback — screens never touch the
 * navigation_drawer_t handle directly; all navigation state lives in ui.c.
 */
void ui_nav_drawer_toggle(void);

#ifdef __cplusplus
}
#endif
