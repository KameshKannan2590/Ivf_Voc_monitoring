#include "voc_gauge.h"
#include "ui/ui.h"
#include <stdlib.h>
#include <stdio.h>

/* ── Geometry (matches approved dashboard layout) ────────────────────────── */
#define VG_CONT_W    272    /* container spans full content width              */
#define VG_CONT_H    268    /* height of gauge section; sensor cards start below */
#define VG_ARC_SIZE  210    /* arc diameter in px                              */
#define VG_ARC_W      18    /* arc track width in px                           */
#define VG_ARC_X      31    /* arc top-left x within container (CX - SIZE/2)  */
#define VG_ARC_Y      55    /* arc top-left y within container (CY - SIZE/2)  */
#define VG_CX        136    /* arc centre x within container                   */
#define VG_CY        160    /* arc centre y within container                   */

/* Zone boundary angles — LVGL convention: 0 = 3-o'clock, clockwise */
#define VG_A_START   135    /* gauge start (lower-left)                        */
#define VG_A_G_END   202    /* green zone end  /  yellow zone start            */
#define VG_A_Y_END   270    /* yellow zone end /  orange zone start            */
#define VG_A_O_END   338    /* orange zone end /  red zone start               */
#define VG_A_END      45    /* gauge end (lower-right, = 405° = 45° CW)        */

/* Badge yellow (not in ui.h — only used by the TVOC gauge) */
#define VG_COLOR_MOD   lv_color_hex(0xFDD835)

/* ── Handle ──────────────────────────────────────────────────────────────── */
struct voc_gauge_s {
    lv_obj_t   *container;
    lv_obj_t   *arc_green;
    lv_obj_t   *arc_yellow;
    lv_obj_t   *arc_orange;
    lv_obj_t   *arc_red;
    lv_obj_t   *lbl_value;
    lv_obj_t   *lbl_badge;
    lv_obj_t   *badge_cont;
    int32_t     current_val;
    int32_t     target_val;
    bool        anim_enabled;
};

/* ── Arc construction ────────────────────────────────────────────────────── */

static lv_obj_t *make_zone_arc(lv_obj_t *parent,
                                 uint16_t a_start, uint16_t a_end,
                                 lv_color_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, VG_ARC_SIZE, VG_ARC_SIZE);
    lv_obj_set_pos(arc, VG_ARC_X, VG_ARC_Y);
    lv_arc_set_bg_angles(arc, a_start, a_end);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);

    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, VG_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);

    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, VG_ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    return arc;
}

/* Scale label centred on (cx, cy) within the gauge container */
static void make_scale_label(lv_obj_t *parent,
                              const char *text,
                              lv_coord_t cx, lv_coord_t cy)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_update_layout(lbl);
    lv_coord_t w = lv_obj_get_width(lbl);
    lv_coord_t h = lv_obj_get_height(lbl);
    lv_obj_set_pos(lbl, cx - w / 2, cy - h / 2);
}

/* ── Internal value application ──────────────────────────────────────────── */

static void apply_arcs(struct voc_gauge_s *g, int32_t ppb)
{
    if (ppb < 0)    ppb = 0;
    if (ppb > 1000) ppb = 1000;

    int32_t gf = (ppb <= 250) ? (ppb * 100 / 250)  : 100;
    int32_t yf = (ppb <= 250) ? 0
               : (ppb <= 500) ? ((ppb - 250) * 100 / 250)
               : 100;
    int32_t of = (ppb <= 500) ? 0
               : (ppb <= 750) ? ((ppb - 500) * 100 / 250)
               : 100;
    int32_t rf = (ppb <= 750) ? 0
               : ((ppb - 750) * 100 / 250);

    lv_arc_set_value(g->arc_green,  (int16_t)gf);
    lv_arc_set_value(g->arc_yellow, (int16_t)yf);
    lv_arc_set_value(g->arc_orange, (int16_t)of);
    lv_arc_set_value(g->arc_red,    (int16_t)rf);
}

static void update_badge(struct voc_gauge_s *g, uint16_t ppb)
{
    lv_color_t  bg_color;
    lv_color_t  txt_color = lv_color_white();   /* default: white on coloured bg */
    const char *text;

    if (ppb == VOC_GAUGE_NO_READING) {
        bg_color = lv_color_hex(0x9E9E9E);
        text     = "---";
    } else if (ppb < 250) {
        bg_color = IVF_COLOR_GOOD;
        text     = "GOOD";
    } else if (ppb < 500) {
        bg_color  = VG_COLOR_MOD;
        txt_color = IVF_COLOR_TEXT;              /* dark text on yellow for contrast */
        text      = "MODERATE";
    } else if (ppb < 750) {
        bg_color = IVF_COLOR_WARNING;
        text     = "POOR";
    } else {
        bg_color = IVF_COLOR_DANGER;
        text     = "UNHEALTHY";
    }

    lv_obj_set_style_bg_color(g->badge_cont, bg_color, 0);
    lv_obj_set_style_text_color(g->lbl_badge, txt_color, 0);
    lv_label_set_text(g->lbl_badge, text);
}

/* ── Animation ───────────────────────────────────────────────────────────── */

static void anim_exec_cb(void *var, int32_t val)
{
    struct voc_gauge_s *g = (struct voc_gauge_s *)var;
    g->current_val = val;
    apply_arcs(g, val);

    if (g->lbl_value) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", (int)val);
        lv_label_set_text(g->lbl_value, buf);
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

voc_gauge_t *voc_gauge_create(lv_obj_t *parent)
{
    voc_gauge_t *g = (voc_gauge_t *)malloc(sizeof(*g));
    if (!g) return NULL;

    g->current_val  = 0;
    g->target_val   = 0;
    g->anim_enabled = true;

    /* ── Root container ── */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, VG_CONT_W, VG_CONT_H);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    g->container = cont;

    /* ── Title ── */
    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, "TVOC (ppb)");
    lv_obj_set_style_text_font(title, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(title, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    /* ── Grey background track (full sweep) ── */
    lv_obj_t *bg = lv_arc_create(cont);
    lv_obj_set_size(bg, VG_ARC_SIZE, VG_ARC_SIZE);
    lv_obj_set_pos(bg, VG_ARC_X, VG_ARC_Y);
    lv_arc_set_bg_angles(bg, VG_A_START, VG_A_END);
    lv_arc_set_range(bg, 0, 100);
    lv_arc_set_value(bg, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(bg, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_color(bg, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(bg, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(bg, VG_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(bg, false, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(bg, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bg, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bg, 0, 0);

    /* ── Four progressive zone arcs ── */
    g->arc_green  = make_zone_arc(cont, VG_A_START, VG_A_G_END, IVF_COLOR_GOOD);
    g->arc_yellow = make_zone_arc(cont, VG_A_G_END, VG_A_Y_END, VG_COLOR_MOD);
    g->arc_orange = make_zone_arc(cont, VG_A_Y_END, VG_A_O_END, IVF_COLOR_WARNING);
    g->arc_red    = make_zone_arc(cont, VG_A_O_END, VG_A_END,   IVF_COLOR_DANGER);

    /* ── Scale labels — pixel-exact centre positions ── */
    make_scale_label(cont, "0",    48, 245);
    make_scale_label(cont, "250",  20, 125);
    make_scale_label(cont, "500",  136, 40);
    make_scale_label(cont, "750",  253, 125);
    make_scale_label(cont, "1000", 220, 245);

    /* ── Centre value stack ── */
    lv_obj_t *ctr = lv_obj_create(cont);
    lv_obj_set_size(ctr, 130, 115);
    lv_obj_set_pos(ctr, VG_CX - 65, VG_CY - 57);
    lv_obj_set_style_bg_opa(ctr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctr, 0, 0);
    lv_obj_set_style_pad_all(ctr, 0, 0);
    lv_obj_clear_flag(ctr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(ctr, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ctr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctr,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ctr, 2, 0);

    g->lbl_value = lv_label_create(ctr);
    lv_label_set_text(g->lbl_value, "--");
    lv_obj_set_style_text_font(g->lbl_value, IVF_FONT_HUGE, 0);
    lv_obj_set_style_text_color(g->lbl_value, IVF_COLOR_TEXT, 0);

    lv_obj_t *lbl_unit = lv_label_create(ctr);
    lv_label_set_text(lbl_unit, "ppb");
    lv_obj_set_style_text_font(lbl_unit, IVF_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(lbl_unit, IVF_COLOR_TEXT_MUTED, 0);

    /* ── Quality badge ── */
    g->badge_cont = lv_obj_create(ctr);
    lv_obj_set_size(g->badge_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(g->badge_cont, IVF_COLOR_GOOD, 0);
    lv_obj_set_style_bg_opa(g->badge_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g->badge_cont, 11, 0);
    lv_obj_set_style_border_width(g->badge_cont, 0, 0);
    lv_obj_set_style_pad_hor(g->badge_cont, 10, 0);
    lv_obj_set_style_pad_ver(g->badge_cont, 3, 0);
    lv_obj_clear_flag(g->badge_cont, LV_OBJ_FLAG_SCROLLABLE);

    g->lbl_badge = lv_label_create(g->badge_cont);
    lv_label_set_text(g->lbl_badge, "GOOD");
    lv_obj_set_style_text_font(g->lbl_badge, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(g->lbl_badge, lv_color_white(), 0);
    lv_obj_center(g->lbl_badge);

    return g;
}

void voc_gauge_set_value(voc_gauge_t *gauge, uint16_t ppb)
{
    if (!gauge) return;

    /* Badge updates instantly — no animation */
    update_badge(gauge, ppb);

    if (ppb == VOC_GAUGE_NO_READING) {
        lv_anim_del(gauge, anim_exec_cb);
        gauge->current_val = 0;
        gauge->target_val  = 0;
        apply_arcs(gauge, 0);
        lv_label_set_text(gauge->lbl_value, "--");
        return;
    }

    int32_t new_val = (int32_t)ppb;
    if (new_val > 1000) new_val = 1000;

    if (new_val == gauge->target_val) return;
    gauge->target_val = new_val;

    if (!gauge->anim_enabled) {
        lv_anim_del(gauge, anim_exec_cb);
        gauge->current_val = new_val;
        apply_arcs(gauge, new_val);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", (int)new_val);
        lv_label_set_text(gauge->lbl_value, buf);
        return;
    }

    lv_anim_del(gauge, anim_exec_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, gauge);
    lv_anim_set_exec_cb(&a, anim_exec_cb);
    lv_anim_set_values(&a, gauge->current_val, new_val);
    lv_anim_set_time(&a, 500);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void voc_gauge_set_animation(voc_gauge_t *gauge, bool enable)
{
    if (!gauge) return;
    gauge->anim_enabled = enable;
}
