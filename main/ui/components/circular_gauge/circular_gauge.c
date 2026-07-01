#include "circular_gauge.h"
#include "status_badge.h"
#include "ui/ui.h"
#include <stdlib.h>
#include <stdio.h>

/* =======================================================================
 * circular_gauge.c — Progressive segmented arc gauge implementation
 *
 * Each zone arc covers its angular slice of the sweep.
 *   LV_PART_MAIN  → transparent (grey background track shows through)
 *   LV_PART_INDICATOR → zone colour, fill [0..100]
 *
 * Animation is done on a scaled integer (value × 10) so lv_anim's
 * int32_t transport gives 0.1 ppb resolution.
 * ======================================================================= */

/* ── Handle definition ──────────────────────────────────────────────────── */

struct circular_gauge {
    circular_gauge_cfg_t  cfg;
    lv_obj_t             *container;
    lv_obj_t             *bg_arc;
    lv_obj_t             *zone_arcs[CIRCULAR_GAUGE_MAX_ZONES];
    lv_obj_t             *value_label;
    lv_obj_t             *unit_label;
    status_badge_t       *badge;         /* external, NOT owned */
    int32_t               current_x10;  /* animated display value × 10 */
    int32_t               target_x10;   /* destination value × 10      */
};

/* ── Internal helpers ───────────────────────────────────────────────────── */

/* Compute total sweep span (0..360) handling the start > end wraparound */
static uint16_t total_span(uint16_t start, uint16_t end)
{
    return (end >= start) ? (uint16_t)(end - start)
                          : (uint16_t)(360u - start + end);
}

/* Apply zone fill percentages for a given value */
static void gauge_apply_zones(struct circular_gauge *g, float value)
{
    float min = g->cfg.min;
    float max = g->cfg.max;

    /* Clamp */
    if (value < min) value = min;
    if (value > max) value = max;

    for (int i = 0; i < g->cfg.zone_count; i++) {
        const gauge_zone_cfg_t *z = &g->cfg.zones[i];
        lv_obj_t *arc = g->zone_arcs[i];
        if (!arc) continue;

        int32_t fill;
        if (value >= z->range_max) {
            fill = 100;
        } else if (value <= z->range_min) {
            fill = 0;
        } else {
            fill = (int32_t)(((value - z->range_min) /
                              (z->range_max - z->range_min)) * 100.0f);
        }
        lv_arc_set_value(arc, (int16_t)fill);
    }

    /* Update centre value label */
    if (g->value_label) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d", (int)value);
        lv_label_set_text(g->value_label, buf);
    }
}

/* Animation exec callback — called every frame */
static void gauge_anim_exec_cb(void *var, int32_t val)
{
    struct circular_gauge *g = (struct circular_gauge *)var;
    g->current_x10 = val;
    gauge_apply_zones(g, (float)val / 10.0f);
}

/* ── Arc construction ───────────────────────────────────────────────────── */

static lv_obj_t *make_bg_arc(lv_obj_t *parent, const circular_gauge_cfg_t *cfg)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, cfg->arc_size, cfg->arc_size);
    lv_obj_set_pos(arc, 0, 0);
    lv_arc_set_bg_angles(arc, cfg->start_angle, cfg->end_angle);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 100);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);

    /* MAIN = grey track, always visible */
    lv_obj_set_style_arc_color(arc, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, cfg->arc_width, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);

    /* INDICATOR = transparent (grey track is the background) */
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);
    return arc;
}

static lv_obj_t *make_zone_arc(lv_obj_t *parent,
                                 const circular_gauge_cfg_t *cfg,
                                 uint16_t zone_start_deg,
                                 uint16_t zone_end_deg,
                                 lv_color_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, cfg->arc_size, cfg->arc_size);
    lv_obj_set_pos(arc, 0, 0);
    lv_arc_set_bg_angles(arc, zone_start_deg, zone_end_deg);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);

    /* MAIN = transparent (grey background shows through) */
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);

    /* INDICATOR = coloured fill */
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, cfg->arc_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);
    return arc;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

circular_gauge_t *circular_gauge_create(lv_obj_t *parent,
                                         const circular_gauge_cfg_t *cfg)
{
    if (!cfg || cfg->zone_count == 0) return NULL;

    circular_gauge_t *g = (circular_gauge_t *)malloc(sizeof(*g));
    if (!g) return NULL;

    g->cfg          = *cfg;              /* shallow copy */
    g->badge        = NULL;
    g->current_x10  = (int32_t)(cfg->min * 10.0f);
    g->target_x10   = g->current_x10;

    /* Root container — transparent, exact arc size */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, cfg->arc_size, cfg->arc_size);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    g->container = cont;

    /* Grey background arc */
    g->bg_arc = make_bg_arc(cont, cfg);

    /* Zone arcs */
    uint16_t span = total_span(cfg->start_angle, cfg->end_angle);
    float range   = cfg->max - cfg->min;

    for (int i = 0; i < cfg->zone_count && i < CIRCULAR_GAUGE_MAX_ZONES; i++) {
        const gauge_zone_cfg_t *z = &cfg->zones[i];
        float zone_frac_start = (z->range_min - cfg->min) / range;
        float zone_frac_end   = (z->range_max - cfg->min) / range;
        uint16_t z_start = (uint16_t)((cfg->start_angle +
                                        (uint16_t)(zone_frac_start * span)) % 360);
        uint16_t z_end   = (uint16_t)((cfg->start_angle +
                                        (uint16_t)(zone_frac_end   * span)) % 360);
        g->zone_arcs[i] = make_zone_arc(cont, cfg, z_start, z_end, z->color);
    }
    for (int i = cfg->zone_count; i < CIRCULAR_GAUGE_MAX_ZONES; i++) {
        g->zone_arcs[i] = NULL;
    }

    /* Centre value label */
    lv_coord_t cx = (lv_coord_t)(cfg->arc_size / 2);
    lv_coord_t cy = (lv_coord_t)(cfg->arc_size / 2);

    lv_obj_t *val_lbl = lv_label_create(cont);
    lv_obj_set_style_text_font(val_lbl, IVF_FONT_HUGE, 0);
    lv_obj_set_style_text_color(val_lbl, lv_color_hex(0x212121), 0);
    lv_label_set_text(val_lbl, "0");
    lv_obj_align(val_lbl, LV_ALIGN_CENTER, 0, -12);
    g->value_label = val_lbl;

    /* Unit label */
    if (cfg->unit) {
        lv_obj_t *unit_lbl = lv_label_create(cont);
        lv_obj_set_style_text_font(unit_lbl, IVF_FONT_NORMAL, 0);
        lv_obj_set_style_text_color(unit_lbl, lv_color_hex(0x757575), 0);
        lv_label_set_text(unit_lbl, cfg->unit);
        lv_obj_align(unit_lbl, LV_ALIGN_CENTER, 0, 28);
        g->unit_label = unit_lbl;
    } else {
        g->unit_label = NULL;
    }

    /* Scale tick labels */
    for (int i = 0; i < cfg->label_count && i < CIRCULAR_GAUGE_MAX_LABELS; i++) {
        const gauge_label_cfg_t *lc = &cfg->labels[i];
        lv_obj_t *tick = lv_label_create(cont);
        lv_obj_set_style_text_font(tick, IVF_FONT_SMALL, 0);
        lv_obj_set_style_text_color(tick, lv_color_hex(0x757575), 0);
        lv_label_set_text(tick, lc->text);
        lv_obj_set_pos(tick, lc->x, lc->y);
        (void)cx; (void)cy;
    }

    return g;
}

void circular_gauge_set_value(circular_gauge_t *gauge, float value)
{
    if (!gauge) return;
    float min = gauge->cfg.min, max = gauge->cfg.max;
    if (value < min) value = min;
    if (value > max) value = max;

    /* Cancel any running animation */
    lv_anim_del(gauge, gauge_anim_exec_cb);

    gauge->current_x10 = (int32_t)(value * 10.0f);
    gauge->target_x10  = gauge->current_x10;
    gauge_apply_zones(gauge, value);
}

void circular_gauge_set_value_animated(circular_gauge_t *gauge,
                                        float value,
                                        uint32_t anim_ms)
{
    if (!gauge) return;
    float min = gauge->cfg.min, max = gauge->cfg.max;
    if (value < min) value = min;
    if (value > max) value = max;

    int32_t new_x10 = (int32_t)(value * 10.0f);
    if (new_x10 == gauge->target_x10) return;

    gauge->target_x10 = new_x10;

    lv_anim_del(gauge, gauge_anim_exec_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, gauge);
    lv_anim_set_exec_cb(&a, gauge_anim_exec_cb);
    lv_anim_set_values(&a, gauge->current_x10, new_x10);
    lv_anim_set_time(&a, anim_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

float circular_gauge_get_value(const circular_gauge_t *gauge)
{
    return gauge ? (float)gauge->target_x10 / 10.0f : 0.0f;
}

lv_obj_t *circular_gauge_get_obj(const circular_gauge_t *gauge)
{
    return gauge ? gauge->container : NULL;
}

void circular_gauge_attach_badge(circular_gauge_t *gauge,
                                  status_badge_t  *badge)
{
    if (!gauge) return;
    gauge->badge = badge;   /* caller owns the badge */
}

void circular_gauge_set_center_text(circular_gauge_t *gauge, const char *text)
{
    if (!gauge || !gauge->value_label) return;
    lv_label_set_text(gauge->value_label, text);
}

void circular_gauge_destroy(circular_gauge_t *gauge)
{
    if (!gauge) return;
    lv_anim_del(gauge, gauge_anim_exec_cb);
    free(gauge);
}
