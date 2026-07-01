#pragma once

/* =======================================================================
 * circular_gauge.h — Reusable progressive segmented arc gauge
 *
 * Renders a multi-zone arc gauge where colour zones fill progressively
 * as the value increases — only completed zones and the active zone are
 * visible at any given time.  An animated transition plays whenever the
 * displayed value changes.
 *
 * Design invariants
 *   • A grey background track covers the full sweep at all times.
 *   • Each colour zone uses LV_PART_INDICATOR for its fill; LV_PART_MAIN
 *     is transparent so the grey track shows through unfilled portions.
 *   • Zone fill is in LVGL arc range [0..100], computed from the ratio
 *     of (current_value − zone_min) / (zone_max − zone_min).
 *   • Only the last zone whose range_min < value has a partial fill;
 *     all previous zones are full (100) and following zones are empty (0).
 *
 * Usage (IVF TVOC example)
 *   static const gauge_zone_cfg_t zones[] = {
 *       { 0,   250,  lv_color_hex(0x43A047) },  // green
 *       { 250, 500,  lv_color_hex(0xFDD835) },  // yellow
 *       { 500, 750,  lv_color_hex(0xFB8C00) },  // orange
 *       { 750, 1000, lv_color_hex(0xE53935) },  // red
 *   };
 *   static const gauge_label_cfg_t labels[] = {
 *       {"0",    48, 245}, {"250",  20, 125}, {"500", 136, 40},
 *       {"750", 253, 125}, {"1000", 220, 245},
 *   };
 *   circular_gauge_cfg_t cfg = {
 *       .min         = 0,   .max         = 1000,
 *       .start_angle = 135, .end_angle   = 45,
 *       .arc_size    = 210, .arc_width   = 18,
 *       .unit        = "ppb",
 *       .zone_count  = 4,   .zones       = zones,
 *       .label_count = 5,   .labels      = labels,
 *   };
 *   circular_gauge_t *g = circular_gauge_create(content, &cfg);
 *   circular_gauge_set_value_animated(g, 245.0f, 500);
 * ======================================================================= */

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Limits ─────────────────────────────────────────────────────────────── */

#define CIRCULAR_GAUGE_MAX_ZONES   4
#define CIRCULAR_GAUGE_MAX_LABELS  8

/* ── Configuration types ────────────────────────────────────────────────── */

/** One colour band on the gauge arc */
typedef struct {
    float       range_min;   /* value at which this zone starts             */
    float       range_max;   /* value at which this zone ends               */
    lv_color_t  color;       /* arc indicator colour for this zone          */
} gauge_zone_cfg_t;

/** A scale tick label at a pixel-exact position (content-relative) */
typedef struct {
    const char  *text;
    lv_coord_t   x;          /* centre-x of label in content coordinates    */
    lv_coord_t   y;          /* centre-y of label in content coordinates    */
} gauge_label_cfg_t;

/** Full gauge configuration — typically declared as a static const */
typedef struct {
    float                     min;          /* global minimum value        */
    float                     max;          /* global maximum value        */
    uint16_t                  start_angle;  /* LVGL arc start (degrees)   */
    uint16_t                  end_angle;    /* LVGL arc end   (degrees)   */
    uint16_t                  arc_size;     /* arc diameter   (px)        */
    uint16_t                  arc_width;    /* track width    (px)        */
    const char               *unit;         /* centre unit label text     */

    uint8_t                   zone_count;
    const gauge_zone_cfg_t   *zones;        /* array of zone_count zones  */

    uint8_t                   label_count;
    const gauge_label_cfg_t  *labels;       /* array of scale labels      */
} circular_gauge_cfg_t;

/* ── Opaque handle ──────────────────────────────────────────────────────── */

typedef struct circular_gauge circular_gauge_t;

/* ── Forward reference (status_badge is an optional attachment) ─────────── */
typedef struct status_badge status_badge_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * Create the gauge as a child of `parent`.
 * The gauge occupies a square of arc_size × arc_size, positioned at (0,0)
 * relative to parent.  Use lv_obj_set_pos() on the returned object's
 * container after creation if you need to offset it.
 * Returns NULL on allocation failure.
 */
circular_gauge_t *circular_gauge_create(lv_obj_t *parent,
                                         const circular_gauge_cfg_t *cfg);

/**
 * Instantly set the displayed value (no animation).
 * Value is clamped to [cfg.min, cfg.max].
 */
void circular_gauge_set_value(circular_gauge_t *gauge, float value);

/**
 * Animate the value from the current displayed position to `value`
 * over `anim_ms` milliseconds using an ease-out curve.
 * Any in-progress animation is cancelled before starting the new one.
 */
void circular_gauge_set_value_animated(circular_gauge_t *gauge,
                                        float value,
                                        uint32_t anim_ms);

/** Returns the current target value (post-animation destination). */
float circular_gauge_get_value(const circular_gauge_t *gauge);

/**
 * Returns the root lv_obj_t container.
 * Use this for positioning, parenting, or z-order operations.
 */
lv_obj_t *circular_gauge_get_obj(const circular_gauge_t *gauge);

/**
 * Optionally attach a status_badge_t that will be embedded in the gauge
 * centre stack.  The gauge does NOT own the badge; the caller manages its
 * lifetime.  Pass NULL to detach.
 */
void circular_gauge_attach_badge(circular_gauge_t *gauge,
                                  status_badge_t  *badge);

/**
 * Update the centre value label text directly (overrides the auto-formatted
 * number).  Useful for "error" or "--" states.
 */
void circular_gauge_set_center_text(circular_gauge_t *gauge, const char *text);

void circular_gauge_destroy(circular_gauge_t *gauge);

#ifdef __cplusplus
}
#endif
