#include "screen_chart.h"
#include "ui/ui.h"

#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "chart";

/* ── State ────────────────────────────────────────────────────────────────── */

static lv_obj_t          *s_scr        = NULL;
static lv_obj_t          *s_chart      = NULL;
static lv_obj_t          *s_period_bar = NULL;
static lv_chart_series_t *s_ser_avg    = NULL;
static lv_chart_series_t *s_ser_max    = NULL;

/* Stat card value labels — updated in screen_chart_refresh() Phase 4C */
static lv_obj_t *s_lbl_avg_val  = NULL;
static lv_obj_t *s_lbl_max_val  = NULL;
static lv_obj_t *s_lbl_min_val  = NULL;
static lv_obj_t *s_lbl_days_val = NULL;

typedef enum { PERIOD_7D = 0, PERIOD_30D = 1, PERIOD_90D = 2 } period_t;
static period_t s_period = PERIOD_90D;

static const uint16_t PERIOD_POINTS[3] = {7, 30, 90};

/* ── Layout constants (content-relative, px) ──────────────────────────────── */
#define PERIOD_BAR_H   40
#define INFO_ROW_Y     43     /* Y-label + legend row, below period bar       */
#define CHART_Y        60     /* chart top                                    */
#define CHART_H        168    /* chart height; X-axis ticks add 14 px below   */
#define CHART_X        40     /* left offset for Y-axis tick labels (38 px)   */
#define CHART_W        228    /* IVF_SCREEN_W - CHART_X - 4                  */
#define STATS_Y        250    /* first stat-card row top (= 60+168+14+8)      */
#define STAT_H         62
#define STAT_W         132    /* (272 - 4 left - 4 gap) / 2                  */
#define STAT_GAP       4

/* Series colours */
#define COL_AVG   IVF_COLOR_GOOD     /* #43A047 green  */
#define COL_MAX   IVF_COLOR_WARNING  /* #FB8C00 orange */

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static void apply_period(period_t p)
{
    s_period     = p;
    uint16_t pts = PERIOD_POINTS[p];
    lv_chart_set_point_count(s_chart, pts);
    for (uint16_t i = 0; i < pts; i++) {
        lv_chart_set_value_by_id(s_chart, s_ser_avg, i, LV_CHART_POINT_NONE);
        lv_chart_set_value_by_id(s_chart, s_ser_max, i, LV_CHART_POINT_NONE);
    }
    lv_chart_refresh(s_chart);
}

static void period_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    uint16_t  idx = lv_btnmatrix_get_selected_btn(obj);
    if (idx == LV_BTNMATRIX_BTN_NONE) return;
    apply_period((period_t)idx);
}

/* X-axis tick label draw event — formats point index as relative day offset */
static void chart_draw_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (!dsc || dsc->type != LV_CHART_DRAW_PART_TICK_LABEL) return;
    if (dsc->id != LV_CHART_AXIS_PRIMARY_X) return;

    uint16_t pts = PERIOD_POINTS[s_period];
    int32_t  ago = (int32_t)(pts - 1) - (int32_t)dsc->value;
    if (ago <= 0) lv_snprintf(dsc->text, dsc->text_length, "Now");
    else          lv_snprintf(dsc->text, dsc->text_length, "-%dD", (int)ago);
}

/* ── Stat card builder ────────────────────────────────────────────────────── */

static void make_stat_card(lv_obj_t *parent,
                            lv_coord_t x, lv_coord_t y,
                            const char *title, const char *unit,
                            lv_color_t val_color, const char *sym,
                            lv_obj_t **out_val_lbl)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, STAT_W, STAT_H);
    lv_obj_set_style_bg_color(card, IVF_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, IVF_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_t = lv_label_create(card);
    lv_label_set_text(lbl_t, title);
    lv_obj_set_style_text_font(lbl_t, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_t, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_s = lv_label_create(card);
    lv_label_set_text(lbl_s, sym);
    lv_obj_set_style_text_font(lbl_s, IVF_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(lbl_s, val_color, 0);
    lv_obj_align(lbl_s, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *lbl_v = lv_label_create(card);
    lv_label_set_text(lbl_v, "--");
    lv_obj_set_style_text_font(lbl_v, IVF_FONT_LARGE, 0);  /* montserrat_24 */
    lv_obj_set_style_text_color(lbl_v, val_color, 0);
    lv_obj_align(lbl_v, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    if (out_val_lbl) *out_val_lbl = lbl_v;

    lv_obj_t *lbl_u = lv_label_create(card);
    lv_label_set_text(lbl_u, unit);
    lv_obj_set_style_text_font(lbl_u, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_u, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align_to(lbl_u, lbl_v, LV_ALIGN_OUT_RIGHT_BOTTOM, 3, -2);
}

/* ── Public API ────────────────────────────────────────────────────────────── */

lv_obj_t *screen_chart_create(void)
{
    /* ── Screen root ──────────────────────────────────────────────────────── */
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, IVF_SCREEN_W, IVF_SCREEN_H);
    lv_obj_set_style_bg_color(s_scr, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Header + calendar icon ────────────────────────────────────────────── */
    lv_obj_t *hdr = ui_build_header(s_scr, "TVOC HISTORY");
    lv_obj_t *cal = lv_label_create(hdr);
    lv_label_set_text(cal, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(cal, IVF_COLOR_PRIMARY, 0);
    lv_obj_set_style_text_font(cal, IVF_FONT_NORMAL, 0);
    lv_obj_align(cal, LV_ALIGN_RIGHT_MID, -8, 0);

    /* ── Content container ─────────────────────────────────────────────────── */
    lv_obj_t *content = lv_obj_create(s_scr);
    lv_obj_set_size(content, IVF_SCREEN_W, IVF_CONTENT_H);
    lv_obj_set_pos(content, 0, IVF_HEADER_H);
    lv_obj_set_style_bg_color(content, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_CLICKABLE);

    /* ── Period toggle bar ─────────────────────────────────────────────────── */
    static const char *period_map[] = {"7D", "30D", "90D", ""};

    s_period_bar = lv_btnmatrix_create(content);
    lv_obj_set_size(s_period_bar, IVF_SCREEN_W, PERIOD_BAR_H);
    lv_obj_set_pos(s_period_bar, 0, 0);
    lv_btnmatrix_set_map(s_period_bar, period_map);
    lv_btnmatrix_set_btn_ctrl_all(s_period_bar, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(s_period_bar, true);
    lv_btnmatrix_set_btn_ctrl(s_period_bar, 2, LV_BTNMATRIX_CTRL_CHECKED);
    lv_obj_add_event_cb(s_period_bar, period_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_set_style_bg_color(s_period_bar, IVF_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_period_bar, IVF_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_period_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(s_period_bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_radius(s_period_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_period_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_period_bar, IVF_COLOR_BG, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_period_bar, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_period_bar, IVF_COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_period_bar, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_period_bar, 6, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_period_bar, IVF_COLOR_PRIMARY,
                               LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(s_period_bar, LV_OPA_COVER,
                            LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_period_bar, lv_color_white(),
                                LV_PART_ITEMS | LV_STATE_CHECKED);

    /* ── Y-axis label + legend row ─────────────────────────────────────────── */
    lv_obj_t *lbl_y = lv_label_create(content);
    lv_label_set_text(lbl_y, "TVOC (ppb)");
    lv_obj_set_style_text_font(lbl_y, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_y, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_y, 2, INFO_ROW_Y);

    /* Legend: colored text labels (entire label in series color) */
    lv_obj_t *leg_avg = lv_label_create(content);
    lv_label_set_text(leg_avg, LV_SYMBOL_MINUS " Daily Avg");
    lv_obj_set_style_text_font(leg_avg, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(leg_avg, COL_AVG, 0);
    lv_obj_set_pos(leg_avg, 120, INFO_ROW_Y);

    lv_obj_t *leg_max = lv_label_create(content);
    lv_label_set_text(leg_max, LV_SYMBOL_MINUS " Max");
    lv_obj_set_style_text_font(leg_max, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(leg_max, COL_MAX, 0);
    lv_obj_set_pos(leg_max, 218, INFO_ROW_Y);

    /* ── TVOC chart ─────────────────────────────────────────────────────────── */
    s_chart = lv_chart_create(content);
    lv_obj_set_size(s_chart, CHART_W, CHART_H);
    lv_obj_set_pos(s_chart, CHART_X, CHART_Y);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    lv_chart_set_point_count(s_chart, PERIOD_POINTS[PERIOD_90D]);
    lv_chart_set_div_line_count(s_chart, 4, 0);  /* horiz lines at 250/500/750 */

    /* Y-axis: 5 major ticks (0,250,500,750,1000), labels 38 px to the left */
    lv_chart_set_axis_tick(s_chart, LV_CHART_AXIS_PRIMARY_Y, 6, 3, 5, 4, true, 38);
    /* X-axis: 5 major ticks, draw event formats labels as relative day offsets */
    lv_chart_set_axis_tick(s_chart, LV_CHART_AXIS_PRIMARY_X, 4, 2, 5, 4, true, 14);
    lv_obj_add_event_cb(s_chart, chart_draw_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    lv_obj_clear_flag(s_chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_chart, LV_OBJ_FLAG_CLICKABLE);

    /* Chart base styles */
    lv_obj_set_style_bg_color(s_chart, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_chart, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_chart, 1, 0);
    lv_obj_set_style_pad_all(s_chart, 4, 0);

    /* Axis tick/label styles */
    lv_obj_set_style_text_font(s_chart, IVF_FONT_SMALL, LV_PART_TICKS);
    lv_obj_set_style_text_color(s_chart, IVF_COLOR_TEXT_MUTED, LV_PART_TICKS);
    lv_obj_set_style_line_color(s_chart, IVF_COLOR_BORDER, LV_PART_TICKS);
    lv_obj_set_style_line_width(s_chart, 1, LV_PART_TICKS);

    /* Series line width + area fill under line */
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_20, LV_PART_ITEMS);

    /* Point markers — small dots on all series */
    lv_obj_set_style_size(s_chart, 3, LV_PART_INDICATOR);

    /* Series: avg (green) first → renders behind max (orange) */
    s_ser_avg = lv_chart_add_series(s_chart, COL_AVG, LV_CHART_AXIS_PRIMARY_Y);
    s_ser_max = lv_chart_add_series(s_chart, COL_MAX, LV_CHART_AXIS_PRIMARY_Y);

    /* ── Stats cards (2 × 2 grid) ──────────────────────────────────────────── */
    make_stat_card(content,
                   4,                     STATS_Y,
                   "AVERAGE", "ppb", COL_AVG, LV_SYMBOL_UP, &s_lbl_avg_val);

    make_stat_card(content,
                   4 + STAT_W + STAT_GAP, STATS_Y,
                   "MAX", "ppb", COL_MAX, LV_SYMBOL_UP, &s_lbl_max_val);

    lv_coord_t sy2 = STATS_Y + STAT_H + STAT_GAP;

    make_stat_card(content,
                   4,                     sy2,
                   "MIN", "ppb", IVF_COLOR_PRIMARY, LV_SYMBOL_DOWN, &s_lbl_min_val);

    make_stat_card(content,
                   4 + STAT_W + STAT_GAP, sy2,
                   ">150 ppb", "Days", lv_color_hex(0x7B1FA2),
                   LV_SYMBOL_LIST, &s_lbl_days_val);

    /* Init both series with POINT_NONE (no data yet) */
    apply_period(PERIOD_90D);

    ESP_LOGD(TAG, "Chart screen created (Phase 4A visual complete)");
    return s_scr;
}

void screen_chart_refresh(void)
{
    /* Phase 4C: query history_manager, update s_ser_avg/s_ser_max and
     * set lv_label_set_text on s_lbl_avg_val / _max_val / _min_val / _days_val */
}
