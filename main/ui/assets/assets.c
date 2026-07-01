#include "assets.h"
#include "ui.h"        /* IVF_COLOR_* constants */

/* =======================================================================
 * assets.c — Drawn icon implementations
 *
 * All icons are geometric LVGL primitives.  Each fits a 20×20 px bbox
 * unless the docstring says otherwise.
 *
 * Migration path: when real bitmap assets are delivered, replace the body
 * of each function with lv_img_create() + lv_img_set_src() while keeping
 * the function signature identical.  Calling code needs zero changes.
 * ======================================================================= */

/* ── Internal helper: transparent, non-scrollable, non-clickable container */

static lv_obj_t *make_icon_cont(lv_obj_t *parent,
                                  lv_coord_t x, lv_coord_t y,
                                  lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, w, h);
    lv_obj_set_pos(cont, x, y);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    return cont;
}

static void make_filled_rect(lv_obj_t *parent,
                               lv_coord_t x, lv_coord_t y,
                               lv_coord_t w, lv_coord_t h,
                               uint8_t radius,
                               lv_color_t color)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, w, h);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_style_bg_color(r, color, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, radius, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
}

/* ── Leaf icon (12×16 px, within 20×20 container) ────────────────────── */

void assets_draw_leaf(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color)
{
    lv_obj_t *cont = make_icon_cont(parent, x, y, 20, 20);

    /* Oval leaf body */
    make_filled_rect(cont, 4, 2, 12, 16, 6, color);

    /* Midrib — slightly darker */
    lv_color_t rib_col = lv_color_darken(color, LV_OPA_20);
    make_filled_rect(cont, 9, 3, 2, 14, 1, rib_col);
}

/* ── Thermometer icon ─────────────────────────────────────────────────── */

void assets_draw_thermometer(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color)
{
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, &oui_temperature);
    lv_obj_set_pos(img, x, y);
    lv_obj_set_style_img_recolor(img, color, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
}

/* ── Humidity icon ────────────────────────────────────────────────────── */

void assets_draw_humidity(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color)
{
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, &material_symbols_humidity_mid);
    lv_obj_set_pos(img, x, y);
    lv_obj_set_style_img_recolor(img, color, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
}

/* ── WiFi icon (3 concentric arcs + centre dot, 20×16 px) ────────────── */

void assets_draw_wifi(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color)
{
    lv_obj_t *cont = make_icon_cont(parent, x, y, 20, 16);

    static const uint8_t SIZES[] = {8, 14, 20};
    for (int i = 0; i < 3; i++) {
        uint8_t s = SIZES[i];
        lv_obj_t *arc = lv_arc_create(cont);
        lv_obj_set_size(arc, s, s);
        lv_obj_set_pos(arc, (20 - s) / 2, 16 - s / 2 - 2);
        lv_arc_set_bg_angles(arc, 225, 315);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, 100);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
        lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(arc, 2, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(arc, 0, 0);
    }

    /* Centre dot */
    make_filled_rect(cont, 8, 13, 4, 4, 2, color);
}

/* ── SD card icon ─────────────────────────────────────────────────────── */

void assets_draw_sd_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color)
{
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, &glyphs_sd_card_1_bold);
    lv_obj_set_pos(img, x, y);
    lv_obj_set_style_img_recolor(img, color, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
}

/* ── Clock icon (circle with two hands, 20×20 px) ────────────────────── */

void assets_draw_clock(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color)
{
    lv_obj_t *cont = make_icon_cont(parent, x, y, 20, 20);

    /* Clock face: ring (outer filled circle - inner bg square approximation) */
    lv_obj_t *face = lv_obj_create(cont);
    lv_obj_set_size(face, 18, 18);
    lv_obj_set_pos(face, 1, 1);
    lv_obj_set_style_bg_color(face, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(face, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(face, 9, 0);
    lv_obj_set_style_border_color(face, color, 0);
    lv_obj_set_style_border_width(face, 2, 0);
    lv_obj_clear_flag(face, LV_OBJ_FLAG_SCROLLABLE);

    /* Hour hand (short, vertical) */
    make_filled_rect(cont, 9, 5, 2, 6, 1, color);

    /* Minute hand (longer, diagonal approximation — horizontal) */
    make_filled_rect(cont, 9, 9, 5, 2, 1, color);

    /* Centre dot */
    make_filled_rect(cont, 9, 9, 2, 2, 1, color);
}

/* ── Chart / trend icon (3 rising bars, 20×16 px) ────────────────────── */

void assets_draw_chart_icon(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color)
{
    lv_obj_t *cont = make_icon_cont(parent, x, y, 20, 16);

    /* Three bars of increasing height */
    static const lv_coord_t BAR_H[] = {6, 10, 14};
    for (int i = 0; i < 3; i++) {
        lv_coord_t bx = 2 + i * 6;
        lv_coord_t by = 16 - BAR_H[i];
        make_filled_rect(cont, bx, by, 4, BAR_H[i], 1, color);
    }
}

/* ── Shield icon (28×32 px) — rounded arch top tapering to point ─────── */

void assets_draw_shield(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color)
{
    lv_obj_t *cont = make_icon_cont(parent, x, y, 28, 32);

    /* Rounded arch (top body) */
    make_filled_rect(cont,  0,  0, 28, 22, 8, color);

    /* Tapering rows toward point */
    make_filled_rect(cont,  3, 20, 22,  5, 0, color);
    make_filled_rect(cont,  7, 24, 14,  5, 0, color);
    make_filled_rect(cont, 11, 28,  6,  3, 0, color);
    make_filled_rect(cont, 13, 30,  2,  2, 0, color);
}
