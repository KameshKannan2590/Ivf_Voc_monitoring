#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * IVF VOC Monitor — Light + Dark theme (Phase 6.1)
 * Portrait 272x480  (physical 480x272 rotated 90 by LVGL)
 * ================================================================ */

/* Theme-aware colors — resolve at CALL time via ivf_color_*() (defined in
 * ui.c), based on the theme loaded once at boot in ui_init(). Every screen
 * already sets its colors via lv_obj_set_style_*(obj, IVF_COLOR_X, 0) calls
 * made once when it's built (all screens are created once in ui_init()),
 * so redefining these macros to call a function instead of a literal is
 * enough to make every existing screen — including the frozen ones —
 * theme-aware with zero changes to their own source files. A theme change
 * from Settings takes effect on the next boot (screen_settings.c calls
 * esp_restart() right after saving it) rather than live, since making it
 * live would mean re-styling every already-built widget on every screen —
 * a much larger change than this feature calls for. */
lv_color_t ivf_color_bg(void);
lv_color_t ivf_color_card(void);
lv_color_t ivf_color_border(void);
lv_color_t ivf_color_text(void);
lv_color_t ivf_color_text_muted(void);
lv_color_t ivf_color_nav(void);
lv_color_t ivf_color_nav_active(void);
lv_color_t ivf_color_nav_inactive(void);

#define IVF_COLOR_BG            ivf_color_bg()
#define IVF_COLOR_CARD          ivf_color_card()
#define IVF_COLOR_BORDER        ivf_color_border()
#define IVF_COLOR_TEXT          ivf_color_text()
#define IVF_COLOR_TEXT_MUTED    ivf_color_text_muted()
#define IVF_COLOR_NAV           ivf_color_nav()
#define IVF_COLOR_NAV_ACTIVE    ivf_color_nav_active()
#define IVF_COLOR_NAV_INACTIVE  ivf_color_nav_inactive()

/* Semantic status colors — deliberately identical in both themes (brand /
 * status colors, not surface colors). */
#define IVF_COLOR_PRIMARY       lv_color_hex(0x1A73E8)
#define IVF_COLOR_GOOD          lv_color_hex(0x43A047)
#define IVF_COLOR_WARNING       lv_color_hex(0xFB8C00)
#define IVF_COLOR_DANGER        lv_color_hex(0xE53935)

/* Typography (montserrat sizes already enabled in sdkconfig) */
#define IVF_FONT_TINY    (&lv_font_montserrat_10)
#define IVF_FONT_SMALL   (&lv_font_montserrat_12)
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

/**
 * Is the navigation drawer currently open? Used by lvgl_port.c's touch
 * callback to gate touches that land outside the drawer while it is open
 * (see ui_nav_drawer_close_from_touch) — see Phase 6.4 notes.
 */
bool ui_nav_drawer_is_open(void);

/**
 * Close the navigation drawer as a direct, synchronous reaction to a raw
 * touch point outside its bounds. Does not go through LVGL's normal event
 * dispatch — called from lvgl_touch_read_cb before that press is ever
 * handed to LVGL, so the screen underneath the drawer never sees it.
 */
void ui_nav_drawer_close_from_touch(void);

#ifdef __cplusplus
}
#endif
