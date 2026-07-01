#pragma once

/* =======================================================================
 * card.h — Reusable card container component
 *
 * A styled container with rounded corners, border, optional shadow,
 * and consistent padding.  The `content` object returned by
 * card_get_content() is the area screens should add their widgets to.
 *
 * Used by: Dashboard sensor tiles, Chart stat cards, Logs/Settings rows.
 *
 * Usage
 *   card_cfg_t cfg = {
 *       .w            = 124,
 *       .h            = 110,
 *       .radius       = IVF_CARD_RADIUS,
 *       .bg_color     = IVF_COLOR_CARD,
 *       .border_color = IVF_COLOR_BORDER,
 *       .border_width = 1,
 *       .pad          = IVF_PAD,
 *       .shadow       = false,
 *   };
 *   card_t *c = card_create(content_area, &cfg);
 *   lv_obj_set_pos(card_get_obj(c), 8, 272);
 *   // Add widgets to: card_get_content(c)
 * ======================================================================= */

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuration ──────────────────────────────────────────────────────── */

typedef struct {
    lv_coord_t  w;             /* card width  (px)                         */
    lv_coord_t  h;             /* card height (px); LV_SIZE_CONTENT = auto */
    uint8_t     radius;        /* corner radius (px)                       */
    lv_color_t  bg_color;
    lv_color_t  border_color;
    uint8_t     border_width;
    uint8_t     pad;           /* inner padding applied to content area    */
    bool        shadow;        /* true → drop shadow (2 px y-offset)       */
    const char *title;         /* optional top-left title text; NULL = none*/
} card_cfg_t;

/* Sensible project defaults — callers can copy-and-modify */
#define CARD_CFG_DEFAULT() {                    \
    .w            = LV_SIZE_CONTENT,            \
    .h            = LV_SIZE_CONTENT,            \
    .radius       = 8,                          \
    .bg_color     = lv_color_hex(0xFFFFFF),     \
    .border_color = lv_color_hex(0xE0E0E0),     \
    .border_width = 1,                          \
    .pad          = 8,                          \
    .shadow       = false,                      \
    .title        = NULL,                       \
}

/* ── Opaque handle ──────────────────────────────────────────────────────── */

typedef struct card card_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * Create the card as a child of `parent`.  Returns NULL on failure.
 */
card_t    *card_create(lv_obj_t *parent, const card_cfg_t *cfg);

/**
 * Returns the padded inner content container.
 * Callers should add their widgets here, NOT to card_get_obj().
 */
lv_obj_t  *card_get_content(const card_t *card);

/**
 * Returns the outer card object (for positioning with lv_obj_set_pos etc.).
 */
lv_obj_t  *card_get_obj(const card_t *card);

/**
 * Update the optional title label text at runtime.
 */
void       card_set_title(card_t *card, const char *title);

void       card_destroy(card_t *card);

#ifdef __cplusplus
}
#endif
