#pragma once

/* =======================================================================
 * header.h — Reusable screen header component
 *
 * Creates the 272×50 header bar that all content screens share.
 * Unlike the legacy ui_build_header() helper, this component:
 *   • Exposes individual update APIs for every element
 *   • Handles the WiFi signal strength indicator (0–3 bars)
 *   • Handles the SD card presence/error icon
 *   • Is completely screen-agnostic — no screen IDs, no global state
 *
 * Leaf icon, WiFi, and SD card are drawn via assets.h helpers.
 *
 * Usage
 *   header_t *hdr = header_create(s_scr);
 *   header_set_title(hdr, "AIR QUALITY MONITOR");
 *   header_set_wifi_strength(hdr, WIFI_STRENGTH_HIGH);
 *   header_set_time(hdr, "08:25 AM");
 *   header_set_date(hdr, "Jun 27, 2026");
 * ======================================================================= */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Status enumerations ────────────────────────────────────────────────── */

typedef enum {
    WIFI_STRENGTH_NONE = 0,   /* no connection — icon greyed out */
    WIFI_STRENGTH_LOW  = 1,   /* 1 bar  */
    WIFI_STRENGTH_MED  = 2,   /* 2 bars */
    WIFI_STRENGTH_HIGH = 3,   /* 3 bars (full) */
} wifi_strength_t;

typedef enum {
    SD_STATUS_ABSENT  = 0,   /* no card — icon hidden or greyed */
    SD_STATUS_PRESENT = 1,   /* card detected OK                 */
    SD_STATUS_ERROR   = 2,   /* card detected but read/write err */
} sd_status_t;

/* ── Callback type ──────────────────────────────────────────────────────── */

/** Called when the user taps the hamburger [≡] button. */
typedef void (*header_menu_cb_t)(void *user_data);

/* ── Opaque handle ──────────────────────────────────────────────────────── */

typedef struct header header_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * Create the header bar as a child of `parent`.
 * The header is 272 px wide × IVF_HEADER_H tall, aligned to the top of
 * the parent.  It sets up all internal widgets with safe initial values.
 * Returns NULL on allocation failure.
 */
header_t *header_create(lv_obj_t *parent);

/**
 * Set the centre/left title text (e.g. "AIR QUALITY MONITOR").
 */
void header_set_title(header_t *hdr, const char *title);

/**
 * Update the time string displayed in the top-right area (e.g. "08:25 AM").
 */
void header_set_time(header_t *hdr, const char *time_str);

/**
 * Update the date string displayed below the time (e.g. "Jun 27, 2026").
 */
void header_set_date(header_t *hdr, const char *date_str);

/**
 * Update the WiFi signal indicator.
 * Adjusts icon opacity or rebuilds arc count to represent signal bars.
 */
void header_set_wifi_strength(header_t *hdr, wifi_strength_t strength);

/**
 * Update the SD card presence icon.
 * ABSENT → icon hidden; PRESENT → normal colour; ERROR → red tint.
 */
void header_set_sd_status(header_t *hdr, sd_status_t status);

/**
 * Enable the hamburger [≡] menu button on the left of the header.
 *
 * - Hides the decorative leaf icon.
 * - Creates a 44×50 px tappable button at x=0 (full header height).
 * - Shifts the title label right so it does not overlap the button.
 * - Fires cb(user_data) on every tap.
 *
 * Idempotent: calling more than once updates the callback but does not
 * create a second button.  Must be called after header_create().
 * The header does NOT own the navigation drawer — cb is the only coupling.
 */
void header_enable_menu(header_t *hdr, header_menu_cb_t cb, void *user_data);

/**
 * Return the root lv_obj_t (the header bar container).
 */
lv_obj_t *header_get_obj(const header_t *hdr);

void header_destroy(header_t *hdr);

#ifdef __cplusplus
}
#endif
