#pragma once

/* =======================================================================
 * status_badge.h — Reusable status pill badge component
 *
 * Creates a pill-shaped coloured badge with a text label inside.
 * Used by: Dashboard (GOOD/WARN/ALARM), Logs screen (row status),
 *          and optionally embedded inside circular_gauge.
 *
 * Usage
 *   status_badge_t *b = status_badge_create(parent);
 *   status_badge_set_state(b, BADGE_STATE_GOOD);
 *   // — or override text/colour independently:
 *   status_badge_set_custom(b, "WARMING", lv_color_hex(0x8E24AA));
 * ======================================================================= */

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── State enumeration ──────────────────────────────────────────────────── */

typedef enum {
    BADGE_STATE_GOOD     = 0,   /* green  #43A047 — "GOOD"     */
    BADGE_STATE_MODERATE = 1,   /* yellow #FDD835 — "MODERATE" */
    BADGE_STATE_POOR     = 2,   /* orange #FB8C00 — "POOR"     */
    BADGE_STATE_DANGER   = 3,   /* red    #E53935 — "DANGER"   */
    BADGE_STATE_ERROR    = 4,   /* grey   #9E9E9E — "ERROR"    */
} badge_state_t;

/* ── Opaque handle ──────────────────────────────────────────────────────── */

typedef struct status_badge status_badge_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * Create a status badge as a child of `parent`.
 * Initial state: BADGE_STATE_GOOD.
 * Size is LV_SIZE_CONTENT (shrinks/grows with text).
 * Returns NULL on allocation failure.
 */
status_badge_t *status_badge_create(lv_obj_t *parent);

/**
 * Set badge to a predefined state.
 * Updates background colour and default text automatically.
 */
void status_badge_set_state(status_badge_t *badge, badge_state_t state);

/**
 * Override the badge text without changing the background colour.
 */
void status_badge_set_text(status_badge_t *badge, const char *text);

/**
 * Override both text and background colour (for states not in the enum,
 * e.g. "WARMING" during ENS160 warm-up in Phase 7).
 */
void status_badge_set_custom(status_badge_t *badge, const char *text, lv_color_t bg_color);

/**
 * Return the underlying lv_obj_t for layout purposes (e.g. lv_obj_align_to).
 */
lv_obj_t *status_badge_get_obj(const status_badge_t *badge);

/**
 * Free the component handle.
 * Note: the LVGL objects are NOT deleted here — LVGL parent-child lifecycle
 * handles that.  Call this only if you also delete the LVGL parent.
 */
void status_badge_destroy(status_badge_t *badge);

#ifdef __cplusplus
}
#endif
