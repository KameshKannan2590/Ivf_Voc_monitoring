#pragma once

/* =======================================================================
 * icon_button.h — Floating circular icon button component
 *
 * A circular (or square) button containing a single symbol label.
 * Used as the floating `[≡]` menu button by navigation_drawer, but
 * reusable for any FAB-style action button elsewhere in the UI.
 *
 * Usage
 *   icon_button_cfg_t cfg = {
 *       .size       = 44,
 *       .x          = 8,
 *       .y          = 480 - 44 - 8,
 *       .bg_color   = lv_color_hex(0x1A73E8),
 *       .symbol     = LV_SYMBOL_LIST,
 *       .icon_color = lv_color_white(),
 *       .circular   = true,
 *       .shadow_width = 10,
 *   };
 *   icon_button_t *btn = icon_button_create(lv_layer_top(), &cfg);
 *   icon_button_set_click_cb(btn, my_handler, NULL);
 * ======================================================================= */

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuration ──────────────────────────────────────────────────────── */

typedef struct {
    lv_coord_t          size;          /* button width = height (px)               */
    lv_coord_t          x;             /* absolute x position on parent            */
    lv_coord_t          y;             /* absolute y position on parent            */
    lv_color_t          bg_color;      /* button background fill                   */
    const char         *symbol;        /* LV_SYMBOL_* string or UTF-8 text         */
    const lv_font_t    *font;          /* symbol font; NULL → IVF_FONT_NORMAL      */
    lv_color_t          icon_color;    /* symbol/text colour                       */
    bool                circular;      /* true → radius = size/2 (circle)          */
    uint8_t             shadow_width;  /* 0 = no shadow                            */
} icon_button_cfg_t;

/* ── Callback ────────────────────────────────────────────────────────────── */

typedef void (*icon_button_cb_t)(void *user_data);

/* ── Opaque handle ──────────────────────────────────────────────────────── */

typedef struct icon_button icon_button_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * Create an icon button as a child of `parent` using the given config.
 * Returns NULL on allocation failure.
 */
icon_button_t *icon_button_create(lv_obj_t *parent, const icon_button_cfg_t *cfg);

/**
 * Register a click callback.  `user_data` is forwarded as-is.
 */
void icon_button_set_click_cb(icon_button_t *btn,
                               icon_button_cb_t cb,
                               void *user_data);

/**
 * Change the displayed symbol at runtime (e.g. ≡ ↔ ✕ when drawer opens).
 */
void icon_button_set_symbol(icon_button_t *btn, const char *symbol);

/**
 * Return the underlying lv_obj_t for layout or z-order operations.
 */
lv_obj_t *icon_button_get_obj(const icon_button_t *btn);

void icon_button_destroy(icon_button_t *btn);

#ifdef __cplusplus
}
#endif
