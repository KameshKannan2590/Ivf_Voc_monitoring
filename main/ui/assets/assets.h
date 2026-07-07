#pragma once

/* =======================================================================
 * assets.h — Central asset registry for IVF VOC Monitor
 *
 * Responsibilities
 *   • Centralise all drawn-icon helper functions (no duplicate drawing
 *     code scattered across screens).
 *   • Provide placeholder LV_IMG_DECLARE stubs so screens can be written
 *     against the final asset interface today; replace stubs with real
 *     C-array bitmaps when graphic assets are delivered.
 *   • Expose the project font table under consistent names.
 *
 * Usage
 *   #include "assets.h"          (ui/assets in INCLUDE_DIRS)
 *   assets_draw_leaf(parent, x, y, IVF_COLOR_GOOD);
 * ======================================================================= */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Drawn icon helpers
 *
 * Each function creates child lv_obj widgets inside `parent` at the given
 * absolute (x, y) position.  All icons fit within a 20×20 px bounding box
 * unless noted.  The `color` parameter tints all shapes in the icon.
 *
 * Phase 4.1: implemented as geometric LVGL primitives.
 * Future: replace body with lv_img_create() + LV_IMG_DECLARE bitmap once
 * real assets are delivered — the calling signature stays the same.
 * ----------------------------------------------------------------------- */

void assets_draw_leaf        (lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color);
void assets_draw_thermometer (lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color);
void assets_draw_humidity    (lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color);
void assets_draw_wifi        (lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color);
void assets_draw_sd_card     (lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color);
void assets_draw_clock       (lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color);
void assets_draw_chart_icon  (lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color);
/* Shield icon (28×32 px) — used in navigation drawer header circle */
void assets_draw_shield      (lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color);

/* Chart screen icons — real bitmaps (calendar_icon.c / chart_average_icon.c /
 * chart_maximum_icon.c / chart_minimum_icon.c / date_range_icon.c), wired up
 * in place of the Phase 5.1/5.2 drawn-primitive placeholders. Calling
 * signature is unchanged, so no caller needed to change. */
void assets_draw_calendar       (lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color); /* 24×24 — period-row calendar button        */
void assets_draw_date_range     (lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color); /* 16×16 — >150ppb Days/Hours stat card      */
void assets_draw_chart_average  (lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color); /* 16×16 — legend + AVERAGE stat card        */
void assets_draw_chart_max      (lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color); /* 16×16 — legend + MAX stat card            */
void assets_draw_chart_min      (lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color); /* 16×16 — MIN stat card                     */

/* -----------------------------------------------------------------------
 * Bitmap image forward declarations (stubs — uncomment when files added)
 *
 * When delivering real PNG/BMP assets:
 *   1. Convert to C array via LVGL online tool or lvgl_image_converter
 *   2. Place generated .c file in main/ui/assets/
 *   3. Uncomment the matching LV_IMG_DECLARE below
 *   4. Replace assets_draw_*() body with lv_img_create() + lv_img_set_src()
 * ----------------------------------------------------------------------- */

LV_IMG_DECLARE(menu_burger_horizontal_bold);
LV_IMG_DECLARE(oui_temperature);
LV_IMG_DECLARE(material_symbols_humidity_mid);
LV_IMG_DECLARE(glyphs_sd_card_1_bold);
LV_IMG_DECLARE(material_symbols_dashboard_rounded);
LV_IMG_DECLARE(solar_chart_bold_duotone);
LV_IMG_DECLARE(icon_park_solid_log);
LV_IMG_DECLARE(ant_design_setting_filled);
LV_IMG_DECLARE(shield);
LV_IMG_DECLARE(Vector);
LV_IMG_DECLARE(lets_icons_date_range_fill);   /* calendar_icon.c        — 24×24 */
LV_IMG_DECLARE(ic_outline_date_range);        /* date_range_icon.c      — 16×16 */
LV_IMG_DECLARE(carbon_chart_average);         /* chart_average_icon.c   — 16×16 */
LV_IMG_DECLARE(tdesign_chart_maximum);        /* chart_maximum_icon.c   — 16×16 */
LV_IMG_DECLARE(tdesign_chart_minimum);        /* chart_minimum_icon.c   — 16×16 */

/* LV_IMG_DECLARE(img_leaf);           */
/* LV_IMG_DECLARE(img_thermometer);    */
/* LV_IMG_DECLARE(img_humidity_drop);  */
/* LV_IMG_DECLARE(img_sd_card);        */
/* LV_IMG_DECLARE(img_clock);          */
/* LV_IMG_DECLARE(img_chart);          */

#ifdef __cplusplus
}
#endif
