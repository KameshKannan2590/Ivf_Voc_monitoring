#pragma once

/*
 * voc_gauge.h — Product-specific TVOC gauge for the IVF EMS
 *
 * Displays a 210 px segmented arc gauge with:
 *   • Four progressive colour zones (green / yellow / orange / red)
 *   • Centre value label and "ppb" unit
 *   • Auto-updating quality badge (GOOD / MODERATE / POOR / UNHEALTHY)
 *   • Scale labels at 0 / 250 / 500 / 750 / 1000 ppb
 *   • 500 ms ease-out animation on value changes
 *
 * This component is designed exclusively for TVOC display.
 * Do not add APIs for other sensor types.
 */

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pass to voc_gauge_set_value() when no sensor reading is available.
 * The gauge shows "--" and resets all arc fills to zero. */
#define VOC_GAUGE_NO_READING  ((uint16_t)0xFFFFu)

typedef struct voc_gauge_s voc_gauge_t;

/**
 * Create the VOC gauge as a child of `parent`.
 * The root container is positioned at (0, 0) in parent and sized 272 × 268 px.
 * Animation is enabled by default.
 * Returns NULL on allocation failure.
 */
voc_gauge_t *voc_gauge_create(lv_obj_t *parent);

/**
 * Update the displayed TVOC value.
 *   • Pass VOC_GAUGE_NO_READING to show "--" with all arcs cleared.
 *   • Values above 1000 are clamped to 1000.
 *   • The badge text and colour update instantly.
 *   • Arc fills and centre value label animate when animation is enabled.
 */
void voc_gauge_set_value(voc_gauge_t *gauge, uint16_t ppb);

/**
 * Enable or disable arc transition animation (default: enabled).
 * Disable during the initial screen render to avoid a "fill-up" effect
 * before the first real sensor reading arrives.
 */
void voc_gauge_set_animation(voc_gauge_t *gauge, bool enable);

#ifdef __cplusplus
}
#endif
