#pragma once

/* =======================================================================
 * navigation_drawer.h — Reusable slide-in navigation drawer component
 *
 * Architecture principles
 *   • No screen-specific logic or screen_id_t references.
 *   • Each nav item carries an opaque uint8_t `id`; the caller maps those
 *     IDs to screen IDs in its on_navigate callback.
 *   • The drawer and its floating button live on lv_layer_top() so they
 *     overlay every screen without being rebuilt per screen switch.
 *   • icon_button.h is used internally for the floating [≡] button.
 *
 * Integration with ui.c (Phase 4.2 migration)
 *
 *   static const nav_drawer_item_t APP_ITEMS[] = {
 *       { LV_SYMBOL_HOME,     "Dashboard", SCREEN_DASHBOARD },
 *       { LV_SYMBOL_LIST,     "Chart",     SCREEN_CHART     },
 *       { LV_SYMBOL_FILE,     "Logs",      SCREEN_LOGS      },
 *       { LV_SYMBOL_SETTINGS, "Settings",  SCREEN_SETTINGS  },
 *   };
 *   static void on_nav(uint8_t id, void *ud) { ui_goto_screen(id, true); }
 *
 *   nav_drawer_cfg_t cfg = {
 *       .items       = APP_ITEMS,
 *       .item_count  = 4,
 *       .drawer_width = IVF_DRAWER_W,
 *       .on_navigate = on_nav,
 *       .user_data   = NULL,
 *   };
 *   navigation_drawer_t *d = navigation_drawer_create(&cfg);
 *   // Later, after screen switch:
 *   navigation_drawer_set_active(d, SCREEN_DASHBOARD);
 * ======================================================================= */

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Limits ─────────────────────────────────────────────────────────────── */

#define NAV_DRAWER_MAX_ITEMS  8

/* ── Item descriptor ────────────────────────────────────────────────────── */

typedef struct {
    const char          *symbol;   /* LV_SYMBOL_* fallback (set NULL when img_src is used) */
    const char          *label;    /* display text, e.g. "Dashboard"                        */
    uint8_t              id;       /* opaque ID returned to on_navigate callback             */
    const lv_img_dsc_t  *img_src;  /* PNG asset; takes priority over symbol when non-NULL   */
} nav_drawer_item_t;

/* ── Navigation callback ────────────────────────────────────────────────── */

/**
 * Called on the LVGL task after the user selects a drawer item.
 * The drawer is closed BEFORE this callback fires.
 * `id` is the item's .id field from the descriptor.
 */
typedef void (*nav_drawer_cb_t)(uint8_t id, void *user_data);

/* ── Configuration ──────────────────────────────────────────────────────── */

typedef struct {
    const nav_drawer_item_t *items;          /* pointer to item array          */
    uint8_t                  item_count;     /* number of items                */
    lv_coord_t               drawer_width;  /* panel width; 0 → IVF_DRAWER_W  */
    nav_drawer_cb_t          on_navigate;   /* selection callback (required)  */
    void                    *user_data;     /* forwarded to callback           */
    bool                     create_fab;   /* true = floating [≡] button on lv_layer_top()
                                              false = header owns the open trigger (Phase 4.2.5+) */
    /* Optional top-section content — all three non-NULL activates the app header */
    const char *header_title;   /* e.g. "Environmental Monitor"  */
    const char *header_status;  /* e.g. "Normal" — green pill    */
    const char *footer_version; /* e.g. "Version v1.2.0"         */
} nav_drawer_cfg_t;

/* ── Opaque handle ──────────────────────────────────────────────────────── */

typedef struct navigation_drawer navigation_drawer_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * Create the navigation drawer on lv_layer_top().
 * Creates: floating [≡] button, semi-transparent backdrop, sliding panel.
 * Must be called from within lvgl_port_lock() / lvgl_port_unlock().
 * Returns NULL on allocation failure.
 */
navigation_drawer_t *navigation_drawer_create(const nav_drawer_cfg_t *cfg);

/** Open the drawer (slide in from left). No-op if already open. */
void navigation_drawer_open  (navigation_drawer_t *drawer);

/** Close the drawer (slide out to left). No-op if already closed. */
void navigation_drawer_close (navigation_drawer_t *drawer);

/** Toggle open/close. */
void navigation_drawer_toggle(navigation_drawer_t *drawer);

/**
 * Highlight the item whose .id matches `item_id`.
 * Call from ui_goto_screen() after a screen switch.
 */
void navigation_drawer_set_active(navigation_drawer_t *drawer, uint8_t item_id);

bool navigation_drawer_is_open(const navigation_drawer_t *drawer);

/**
 * Free the component handle.
 * LVGL objects on lv_layer_top() are cleaned up via lv_obj_del().
 */
void navigation_drawer_destroy(navigation_drawer_t *drawer);

#ifdef __cplusplus
}
#endif
