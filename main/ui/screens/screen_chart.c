#include "screen_chart.h"
#include "ui/ui.h"
#include "header.h"
#include "card.h"
#include "assets.h"
#include "history_manager.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "chart";

/* ── State ────────────────────────────────────────────────────────────────── */

static lv_obj_t          *s_scr        = NULL;
static header_t          *s_hdr        = NULL;
static lv_obj_t          *s_chart      = NULL;
static lv_chart_series_t *s_ser_avg    = NULL;
static lv_chart_series_t *s_ser_max    = NULL;
static lv_obj_t          *s_lbl_title  = NULL;

/* Stat card widget handles */
static lv_obj_t *s_lbl_avg_val   = NULL;
static lv_obj_t *s_lbl_max_val   = NULL;
static lv_obj_t *s_lbl_min_val   = NULL;
static lv_obj_t *s_lbl_days_val  = NULL;
static lv_obj_t *s_lbl_days_unit = NULL;   /* "Days" / "Hours" — swaps with mode */

/* Simple Today/7-Days dropdown overlay */
static lv_obj_t *s_picker_backdrop  = NULL;

/* ── Chart mode — single source of truth for the Chart screen (Phase 5.4) ───
 * Everything the UI needs to know about "what is currently displayed"
 * lives in these variables. No boolean flags anywhere else. */
typedef enum {
    CHART_MODE_LAST_7_DAYS = 0,
    CHART_MODE_TODAY,
} chart_mode_t;

static chart_mode_t s_mode = CHART_MODE_LAST_7_DAYS;

/* ── Layout constants (content-relative, px) — 8 px screen margin/gap,
 * matching the Dashboard's spacing rhythm ─────────────────────────────────── */
#define MARGIN         8
#define ROW_A_Y        8      /* title + calendar button row                    */
#define ROW_A_H        34
#define CAL_BTN_SIZE   34
#define CAL_ICON_SIZE  24     /* real bitmap (calendar_icon.c) is 24×24, not 16 */

/* Dynamic title sits on the SAME line as the calendar button (vertically
 * centered against its 34 px height), not on its own row below it. */
#define TITLE_Y        (ROW_A_Y + (ROW_A_H - 18) / 2)
#define TITLE_W        (IVF_SCREEN_W - 2 * MARGIN - CAL_BTN_SIZE - 6)

#define LEGEND_Y       (ROW_A_Y + ROW_A_H + 18)  /* legend row, directly below title row  */
#define LEGEND_ICON_Y  (LEGEND_Y + 1)

#define CHART_Y        (LEGEND_Y + 18)
#define CHART_H        180    /* no more reserved chip row — that space goes to the chart */
#define CHART_X        32     /* left offset for Y-axis tick labels           */
#define CHART_W        (IVF_SCREEN_W - CHART_X - MARGIN)
#define CHART_BOTTOM_GAP 14

#define STATS_Y        (CHART_Y + CHART_H + CHART_BOTTOM_GAP)
#define STAT_H         70
#define STAT_GAP       8
#define STAT_W         ((IVF_SCREEN_W - MARGIN * 2 - STAT_GAP) / 2)
#define STAT_ICON_SIZE 16

/* Simple 2-row "Today" / "7 Days" dropdown panel — no calendar grid for now. */
#define PICKER_PANEL_W 140
#define PICKER_ROW_H   40
#define PICKER_ROW_GAP 4
#define PICKER_PAD     4

/* Series colours */
#define COL_AVG   IVF_COLOR_GOOD     /* #43A047 green  */
#define COL_MAX   IVF_COLOR_WARNING  /* #FB8C00 orange */

/* X-axis tick counts. Last-7-Days labels are computed per-mode-switch from
 * real calendar dates (see s_day_axis_labels); Today-mode hour labels are
 * fixed 4-hour boundaries and never change. */
#define DAY_AXIS_TICKS  7
#define HOUR_AXIS_TICKS 7
static char        s_day_axis_labels[DAY_AXIS_TICKS][8];
static const char *HOUR_LABELS[HOUR_AXIS_TICKS] = { "0", "4", "8", "12", "16", "20", "24" };

static const char *MONTH_ABBR[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* ── Calendar math — dependency-free, no <time.h> ────────────────────────────
 * This project has no RTC/SNTP yet (Phase 7), so we cannot trust the C
 * library's time.h to reflect real wall-clock time or even to be fully
 * configured on this target. Rather than depend on mktime()/gmtime()'s
 * timezone machinery, every calendar computation below is done with plain
 * integer arithmetic — Howard Hinnant's public-domain days_from_civil() /
 * civil_from_days() algorithm — which is correct for any proleptic
 * Gregorian date and has zero platform dependency.
 *
 * A single fixed reference date anchors "today" to boot time (see
 * calendar_init_reference()). Everything the user sees (day picker, title,
 * X-axis) is a real, correctly-computed calendar date built on this anchor.
 * Only the anchor itself is a placeholder — once RTC/SNTP exists, replacing
 * calendar_init_reference()'s fixed reference with a real time read is the
 * only change needed; every date computation downstream is already correct.
 * ======================================================================= */

static int64_t days_from_civil(int32_t y, uint32_t m, uint32_t d)
{
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);                          /* [0, 399]     */
    uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;      /* [0, 365]     */
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;               /* [0, 146096]  */
    return era * 146097 + (int64_t)doe - 719468;                       /* days since 1970-01-01 */
}

static void civil_from_days(int64_t z, uint16_t *y, uint8_t *m, uint8_t *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);                       /* [0, 146096]  */
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0, 399]   */
    int64_t  yr  = (int64_t)yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);             /* [0, 365]     */
    uint32_t mp  = (5 * doy + 2) / 153;                                 /* [0, 11]      */
    *d = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);                       /* [1, 31]      */
    *m = (uint8_t)(mp + (mp < 10 ? 3 : -9));                            /* [1, 12]      */
    yr += (*m <= 2);
    *y = (uint16_t)yr;
}

/* boot-relative seconds (the same time base history_manager stores) <-> real
 * calendar date, via the fixed reference anchor computed once at startup. */
static int64_t s_calendar_epoch_offset_s = 0;   /* unix_seconds_at(boot_ts=0) */

static void calendar_init_reference(void)
{
    /* Placeholder "today" until RTC/SNTP exists (Phase 7) — matches the
     * approximate date this phase was implemented. Only this line needs to
     * change once a real time source is available. */
    #define CHART_REF_YEAR  2026
    #define CHART_REF_MONTH 7
    #define CHART_REF_DAY   6

    int64_t ref_unix_s = days_from_civil(CHART_REF_YEAR, CHART_REF_MONTH, CHART_REF_DAY) * 86400;
    uint32_t boot_now_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    s_calendar_epoch_offset_s = ref_unix_s - (int64_t)boot_now_s;
}

static void boot_ts_to_calendar_date(uint32_t boot_ts, lv_calendar_date_t *out)
{
    int64_t unix_s = (int64_t)boot_ts + s_calendar_epoch_offset_s;
    int64_t days   = unix_s >= 0 ? unix_s / 86400 : (unix_s - 86399) / 86400; /* floor div */
    uint16_t y; uint8_t m, d;
    civil_from_days(days, &y, &m, &d);
    out->year = y;
    out->month = (int8_t)m;
    out->day = (int8_t)d;
}

static uint32_t calendar_date_to_boot_ts(const lv_calendar_date_t *d)
{
    int64_t days   = days_from_civil(d->year, (uint32_t)d->month, (uint32_t)d->day);
    int64_t unix_s = days * 86400;
    int64_t boot_s = unix_s - s_calendar_epoch_offset_s;
    return boot_s > 0 ? (uint32_t)boot_s : 0;
}

/* ── Helpers ──────────────────────────────────────────────────────────────── */

/* Chart draw-part hook: formats X-axis tick labels (mode-dependent — real
 * calendar day labels or fixed 4-hour boundaries), and — since LVGL 8.4's
 * lv_chart has no built-in per-series area fill for LINE-type charts —
 * paints a light fill polygon under each segment of the average series
 * only (max series is left as a clean line, per Figma; unchanged since
 * Phase 5.1/5.2). */
static void chart_draw_part_cb(lv_event_t *e)
{
    lv_obj_t                *chart = lv_event_get_target(e);
    lv_obj_draw_part_dsc_t   *dsc  = lv_event_get_draw_part_dsc(e);
    if (!dsc) return;

    if (dsc->type == LV_CHART_DRAW_PART_TICK_LABEL && dsc->id == LV_CHART_AXIS_PRIMARY_X) {
        if (s_mode == CHART_MODE_LAST_7_DAYS) {
            if (dsc->value >= 0 && dsc->value < DAY_AXIS_TICKS)
                lv_snprintf(dsc->text, dsc->text_length, "%s", s_day_axis_labels[dsc->value]);
        } else {
            if (dsc->value >= 0 && dsc->value < HOUR_AXIS_TICKS)
                lv_snprintf(dsc->text, dsc->text_length, "%s", HOUR_LABELS[dsc->value]);
        }
        return;
    }

    if (dsc->type == LV_CHART_DRAW_PART_LINE_AND_POINT &&
        dsc->part == LV_PART_ITEMS &&
        dsc->sub_part_ptr == s_ser_avg &&
        dsc->p1 && dsc->p2) {

        lv_area_t content_area;
        lv_obj_get_content_coords(chart, &content_area);

        lv_point_t poly[4] = {
            { dsc->p1->x, dsc->p1->y },
            { dsc->p2->x, dsc->p2->y },
            { dsc->p2->x, content_area.y2 },
            { dsc->p1->x, content_area.y2 },
        };

        lv_draw_rect_dsc_t fill_dsc;
        lv_draw_rect_dsc_init(&fill_dsc);
        fill_dsc.bg_color = COL_AVG;
        fill_dsc.bg_opa   = LV_OPA_20;

        lv_draw_polygon(dsc->draw_ctx, &fill_dsc, poly, 4);
    }
}

/* ── Menu button callback ────────────────────────────────────────────────── */

static void on_menu_btn(void *user_data)
{
    (void)user_data;
    ui_nav_drawer_toggle();
}

/* ── Central mode controller (Phase 5.4) ─────────────────────────────────────
 * The ONLY place that updates the Chart UI when the mode changes. Coordinates
 * the title, statistics labels/values, X-axis, and chart dataset, in that
 * order, from one History Manager query. The legend ("Average"/"Maximum") is
 * mode-invariant since Phase 5.4 and is not touched here. */
static void apply_chart_mode(chart_mode_t mode)
{
    s_mode = mode;

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    history_record_t buf[24];
    uint16_t n;

    if (mode == CHART_MODE_LAST_7_DAYS) {
        uint32_t to_ts   = now;
        uint32_t from_ts = (to_ts > 7 * 86400u) ? to_ts - 7 * 86400u : 0;
        n = history_manager_get_daily_aggregates(from_ts, to_ts, buf, 7);
        lv_chart_set_axis_tick(s_chart, LV_CHART_AXIS_PRIMARY_X, 2, 0, DAY_AXIS_TICKS, 1, true, 14);

        for (uint16_t i = 0; i < DAY_AXIS_TICKS; i++) s_day_axis_labels[i][0] = '\0';
        for (uint16_t i = 0; i < n; i++) {
            lv_calendar_date_t d;
            boot_ts_to_calendar_date(buf[i].timestamp_s, &d);
            snprintf(s_day_axis_labels[i], sizeof(s_day_axis_labels[i]), "%d %s",
                     (int)d.day, MONTH_ABBR[d.month - 1]);
        }
    } else {
        lv_calendar_date_t today;
        boot_ts_to_calendar_date(now, &today);
        uint32_t day_start = calendar_date_to_boot_ts(&today);
        uint32_t day_end   = day_start + 86400u;
        n = history_manager_get_range(day_start, day_end, buf, 24);
        lv_chart_set_axis_tick(s_chart, LV_CHART_AXIS_PRIMARY_X, 2, 0, HOUR_AXIS_TICKS, 1, true, 14);
    }

    /* Chart dataset */
    lv_chart_set_point_count(s_chart, n > 0 ? n : 1);
    if (n == 0) {
        lv_chart_set_value_by_id(s_chart, s_ser_avg, 0, LV_CHART_POINT_NONE);
        lv_chart_set_value_by_id(s_chart, s_ser_max, 0, LV_CHART_POINT_NONE);
    } else {
        for (uint16_t i = 0; i < n; i++) {
            lv_chart_set_value_by_id(s_chart, s_ser_avg, i, (lv_coord_t)buf[i].avg_voc_ppb);
            lv_chart_set_value_by_id(s_chart, s_ser_max, i, (lv_coord_t)buf[i].max_voc_ppb);
        }
    }
    lv_chart_refresh(s_chart);

    /* Statistics labels + values — always computed from the exact dataset
     * just plotted, never a separately-fetched or stale set. */
    history_stats_t stats;
    history_manager_compute_stats(buf, n, VOC_WARNING_THRESHOLD_PPB, &stats);

    char vbuf[16];
    if (n == 0) {
        lv_label_set_text(s_lbl_avg_val,  "--");
        lv_label_set_text(s_lbl_max_val,  "--");
        lv_label_set_text(s_lbl_min_val,  "--");
        lv_label_set_text(s_lbl_days_val, "--");
    } else {
        /* Standard snprintf, not lv_snprintf: LV_SPRINTF_USE_FLOAT is off in
         * this project's sdkconfig, so LVGL's own printf can't format %f. */
        snprintf(vbuf, sizeof(vbuf), "%.0f", (double)stats.avg_voc_ppb);
        lv_label_set_text(s_lbl_avg_val, vbuf);
        snprintf(vbuf, sizeof(vbuf), "%.0f", (double)stats.max_voc_ppb);
        lv_label_set_text(s_lbl_max_val, vbuf);
        snprintf(vbuf, sizeof(vbuf), "%.0f", (double)stats.min_voc_ppb);
        lv_label_set_text(s_lbl_min_val, vbuf);
        lv_snprintf(vbuf, sizeof(vbuf), "%u", stats.over_threshold_count);
        lv_label_set_text(s_lbl_days_val, vbuf);
    }
    lv_label_set_text(s_lbl_days_unit, mode == CHART_MODE_LAST_7_DAYS ? "Days" : "Hours");

    /* Dynamic title */
    lv_label_set_text(s_lbl_title, mode == CHART_MODE_LAST_7_DAYS
                                        ? "TVOC Trend - Last 7 Days"
                                        : "TVOC Trend - Today");
}

/* ── Simple Today / 7-Days dropdown (Phase 5.6) ──────────────────────────────
 * Deliberately simple for now: a 2-row picker, no calendar grid. Revisit once
 * arbitrary-date selection is actually needed. */

static void on_picker_today_click(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_picker_backdrop, LV_OBJ_FLAG_HIDDEN);
    apply_chart_mode(CHART_MODE_TODAY);
}

static void on_picker_7days_click(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_picker_backdrop, LV_OBJ_FLAG_HIDDEN);
    apply_chart_mode(CHART_MODE_LAST_7_DAYS);
}

static void on_picker_backdrop_click(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_picker_backdrop, LV_OBJ_FLAG_HIDDEN);
}

static void on_calendar_btn_click(lv_event_t *e)
{
    (void)e;
    lv_obj_clear_flag(s_picker_backdrop, LV_OBJ_FLAG_HIDDEN);
}

/* ── Stat card builder (shared card_t + assets icons) ─────────────────────── */

typedef void (*stat_icon_fn_t)(lv_obj_t *, lv_coord_t, lv_coord_t, lv_color_t);

static void make_stat_card(lv_obj_t *parent,
                            lv_coord_t x, lv_coord_t y,
                            const char *title, const char *unit, const char *initial_value,
                            lv_color_t color, stat_icon_fn_t icon_fn, lv_coord_t icon_w,
                            lv_obj_t **out_val_lbl, lv_obj_t **out_unit_lbl)
{
    /* Same card recipe as Dashboard's sensor tiles (build_sensor_card() in
     * screen_dashboard.c) — radius, border, and shadow=false all match. */
    card_cfg_t ccfg = {
        .w            = STAT_W,
        .h            = STAT_H,
        .radius       = IVF_CARD_RADIUS,
        .bg_color     = IVF_COLOR_CARD,
        .border_color = IVF_COLOR_BORDER,
        .border_width = 1,
        .pad          = IVF_PAD,
        .shadow       = false,
        .title        = NULL,
    };
    card_t   *c       = card_create(parent, &ccfg);
    lv_obj_t *card    = card_get_obj(c);
    lv_obj_t *content = card_get_content(c);
    lv_obj_set_pos(card, x, y);

    lv_obj_t *lbl_t = lv_label_create(content);
    lv_label_set_text(lbl_t, title);
    lv_obj_set_style_text_font(lbl_t, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_t, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_t, 0, 0);

    lv_coord_t content_w = STAT_W - 2 * IVF_PAD;
    icon_fn(content, content_w - icon_w, 0, color);

    lv_obj_t *lbl_v = lv_label_create(content);
    lv_label_set_text(lbl_v, initial_value);
    lv_obj_set_style_text_font(lbl_v, IVF_FONT_LARGE, 0);  /* montserrat_24 */
    lv_obj_set_style_text_color(lbl_v, color, 0);
    lv_obj_set_pos(lbl_v, 0, 30);
    if (out_val_lbl) *out_val_lbl = lbl_v;

    lv_obj_t *lbl_u = lv_label_create(content);
    lv_label_set_text(lbl_u, unit);
    lv_obj_set_style_text_font(lbl_u, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_u, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align_to(lbl_u, lbl_v, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -2);  /* "245  ppb", not "245ppb" */
    if (out_unit_lbl) *out_unit_lbl = lbl_u;
}

/* ── Public API ────────────────────────────────────────────────────────────── */

lv_obj_t *screen_chart_create(void)
{
    calendar_init_reference();

    /* ── Screen root ──────────────────────────────────────────────────────── */
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, IVF_SCREEN_W, IVF_SCREEN_H);
    lv_obj_set_style_bg_color(s_scr, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Header — shared component, pixel identical to Dashboard, FROZEN ───── */
    s_hdr = header_create(s_scr);
    header_set_title(s_hdr, "CHART");
    header_set_wifi_strength(s_hdr, WIFI_STRENGTH_HIGH);
    header_set_sd_status(s_hdr, SD_STATUS_ABSENT);
    header_set_time(s_hdr, "08:25 AM");
    header_set_date(s_hdr, "May 24, 2026");
    header_enable_menu(s_hdr, on_menu_btn, NULL);

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

    /* ── Row A: dynamic title + calendar btn, same line ──────────────────────── */
    lv_obj_t *cal_btn = lv_obj_create(content);
    lv_obj_set_size(cal_btn, CAL_BTN_SIZE, CAL_BTN_SIZE);
    lv_obj_set_pos(cal_btn, IVF_SCREEN_W - MARGIN - CAL_BTN_SIZE, ROW_A_Y);
    lv_obj_set_style_bg_color(cal_btn, IVF_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(cal_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cal_btn, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(cal_btn, 1, 0);
    lv_obj_set_style_radius(cal_btn, IVF_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(cal_btn, 0, 0);
    lv_obj_clear_flag(cal_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(cal_btn, on_calendar_btn_click, LV_EVENT_CLICKED, NULL);
    /* IVF_COLOR_PRIMARY (was muted grey) signals the icon is now interactive */
    assets_draw_calendar(cal_btn, (CAL_BTN_SIZE - CAL_ICON_SIZE) / 2, (CAL_BTN_SIZE - CAL_ICON_SIZE) / 2, IVF_COLOR_PRIMARY);

    s_lbl_title = lv_label_create(content);
    lv_label_set_text(s_lbl_title, "TVOC Trend - Last 7 Days");
    lv_obj_set_style_text_font(s_lbl_title, IVF_FONT_NORMAL, 0);   /* same font as Dashboard title */
    lv_obj_set_style_text_color(s_lbl_title, IVF_COLOR_TEXT, 0);
    lv_obj_set_width(s_lbl_title, TITLE_W);   /* stops short of cal_btn — no overlap */
    lv_label_set_long_mode(s_lbl_title, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(s_lbl_title, MARGIN, TITLE_Y);

    /* ── Legend (own row) — "Average" is mode-invariant since Phase 5.4 ─────── */
    assets_draw_chart_average(content, MARGIN, LEGEND_ICON_Y, COL_AVG);
    lv_obj_t *leg_avg = lv_label_create(content);
    lv_label_set_text(leg_avg, "Average");
    lv_obj_set_style_text_font(leg_avg, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(leg_avg, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(leg_avg, MARGIN + STAT_ICON_SIZE + 3, LEGEND_Y);

    lv_coord_t legend_max_x = MARGIN + STAT_ICON_SIZE + 3 + 60; /* "Average" width + gap, hand-tuned */
    assets_draw_chart_max(content, legend_max_x, LEGEND_ICON_Y, COL_MAX);
    lv_obj_t *leg_max = lv_label_create(content);
    lv_label_set_text(leg_max, "Maximum");
    lv_obj_set_style_text_font(leg_max, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(leg_max, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(leg_max, legend_max_x + STAT_ICON_SIZE + 3, LEGEND_Y);

    /* ── TVOC chart ─────────────────────────────────────────────────────────── */
    s_chart = lv_chart_create(content);
    lv_obj_set_size(s_chart, CHART_W, CHART_H);
    lv_obj_set_pos(s_chart, CHART_X, CHART_Y);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    lv_chart_set_point_count(s_chart, 7);
    lv_chart_set_div_line_count(s_chart, 4, 6);  /* light grid: 4 horiz / 6 vert */

    /* Y-axis: 5 major ticks (1000,750,500,250,0), no minor sub-ticks.
     * draw_size (32) matches CHART_X so the label gutter is fully redrawn.
     * X-axis tick config is (re)applied per mode inside apply_chart_mode(). */
    lv_chart_set_axis_tick(s_chart, LV_CHART_AXIS_PRIMARY_Y, 2, 0, 5, 1, true, 32);
    lv_chart_set_axis_tick(s_chart, LV_CHART_AXIS_PRIMARY_X, 2, 0, DAY_AXIS_TICKS, 1, true, 14);
    lv_obj_add_event_cb(s_chart, chart_draw_part_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    lv_obj_clear_flag(s_chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_chart, LV_OBJ_FLAG_CLICKABLE);

    /* Chart base styles — light grey card background, rounded border */
    lv_obj_set_style_bg_color(s_chart, IVF_COLOR_NAV, 0);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_chart, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_chart, 1, 0);
    lv_obj_set_style_radius(s_chart, IVF_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(s_chart, 6, 0);
    /* NOTE: deliberately no clip_corner here — LVGL draws the X/Y axis tick
     * labels outside the chart's own coordinate box (via the ext-draw-size
     * mechanism), and clip_corner scopes its rounded-rect mask to exactly
     * that box, which silently hides those labels entirely. */

    /* Softer grid lines — thin, partly transparent against the grey background */
    lv_obj_set_style_line_color(s_chart, IVF_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_opa(s_chart, LV_OPA_60, LV_PART_MAIN);

    /* Axis tick/label styles — same font/colour as the Dashboard gauge's
     * scale labels (voc_gauge.c: IVF_FONT_SMALL + IVF_COLOR_TEXT_MUTED) */
    lv_obj_set_style_text_font(s_chart, IVF_FONT_SMALL, LV_PART_TICKS);
    lv_obj_set_style_text_color(s_chart, IVF_COLOR_TEXT_MUTED, LV_PART_TICKS);
    lv_obj_set_style_line_color(s_chart, IVF_COLOR_BORDER, LV_PART_TICKS);
    lv_obj_set_style_line_width(s_chart, 1, LV_PART_TICKS);
    lv_obj_set_style_pad_left(s_chart, 2, LV_PART_TICKS);
    lv_obj_set_style_pad_right(s_chart, 2, LV_PART_TICKS);

    /* Series lines — smooth (anti-aliased), rounded caps/joins */
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(s_chart, true, LV_PART_ITEMS);

    /* Point markers — small circular dots */
    lv_obj_set_style_size(s_chart, 4, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_chart, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, LV_PART_INDICATOR);

    /* Series: avg (green) first → renders behind max (orange) */
    s_ser_avg = lv_chart_add_series(s_chart, COL_AVG, LV_CHART_AXIS_PRIMARY_Y);
    s_ser_max = lv_chart_add_series(s_chart, COL_MAX, LV_CHART_AXIS_PRIMARY_Y);

    /* ── Stats cards (2 × 2 grid) ──────────────────────────────────────────── */
    char days_card_title[16];
    snprintf(days_card_title, sizeof(days_card_title), ">%.0f ppb", (double)VOC_WARNING_THRESHOLD_PPB);

    make_stat_card(content, MARGIN, STATS_Y,
                   "AVERAGE", "ppb", "--", COL_AVG,
                   assets_draw_chart_average, STAT_ICON_SIZE, &s_lbl_avg_val, NULL);

    make_stat_card(content, MARGIN + STAT_W + STAT_GAP, STATS_Y,
                   "MAX", "ppb", "--", COL_MAX,
                   assets_draw_chart_max, STAT_ICON_SIZE, &s_lbl_max_val, NULL);

    lv_coord_t sy2 = STATS_Y + STAT_H + STAT_GAP;

    make_stat_card(content, MARGIN, sy2,
                   "MIN", "ppb", "--", IVF_COLOR_PRIMARY,
                   assets_draw_chart_min, STAT_ICON_SIZE, &s_lbl_min_val, NULL);

    make_stat_card(content, MARGIN + STAT_W + STAT_GAP, sy2,
                   days_card_title, "Days", "--", IVF_COLOR_TEXT,
                   assets_draw_date_range, STAT_ICON_SIZE, &s_lbl_days_val, &s_lbl_days_unit);

    /* ── Today/7-Days dropdown overlay (hidden until the calendar icon is tapped) */
    s_picker_backdrop = lv_obj_create(s_scr);
    lv_obj_set_size(s_picker_backdrop, IVF_SCREEN_W, IVF_SCREEN_H);
    lv_obj_set_pos(s_picker_backdrop, 0, 0);
    lv_obj_set_style_bg_color(s_picker_backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_picker_backdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_picker_backdrop, 0, 0);
    lv_obj_set_style_radius(s_picker_backdrop, 0, 0);
    lv_obj_set_style_pad_all(s_picker_backdrop, 0, 0);
    lv_obj_clear_flag(s_picker_backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_picker_backdrop, on_picker_backdrop_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_picker_backdrop, LV_OBJ_FLAG_HIDDEN);

    lv_coord_t picker_panel_h = 2 * PICKER_ROW_H + PICKER_ROW_GAP + 2 * PICKER_PAD;
    lv_obj_t *picker_panel = lv_obj_create(s_picker_backdrop);
    lv_obj_set_size(picker_panel, PICKER_PANEL_W, picker_panel_h);
    lv_obj_align(picker_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(picker_panel, IVF_COLOR_CARD, 0);
    lv_obj_set_style_radius(picker_panel, IVF_CARD_RADIUS, 0);
    lv_obj_set_style_border_color(picker_panel, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(picker_panel, 1, 0);
    lv_obj_set_style_pad_all(picker_panel, PICKER_PAD, 0);
    lv_obj_clear_flag(picker_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t row_w = PICKER_PANEL_W - 2 * PICKER_PAD;

    lv_obj_t *row_today = lv_btn_create(picker_panel);
    lv_obj_set_size(row_today, row_w, PICKER_ROW_H);
    lv_obj_set_pos(row_today, 0, 0);
    lv_obj_set_style_bg_color(row_today, IVF_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(row_today, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row_today, 0, 0);
    lv_obj_set_style_radius(row_today, IVF_CARD_RADIUS, 0);
    lv_obj_add_event_cb(row_today, on_picker_today_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *row_today_lbl = lv_label_create(row_today);
    lv_label_set_text(row_today_lbl, "Today");
    lv_obj_set_style_text_font(row_today_lbl, IVF_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(row_today_lbl, IVF_COLOR_TEXT, 0);
    lv_obj_center(row_today_lbl);

    lv_obj_t *row_7days = lv_btn_create(picker_panel);
    lv_obj_set_size(row_7days, row_w, PICKER_ROW_H);
    lv_obj_set_pos(row_7days, 0, PICKER_ROW_H + PICKER_ROW_GAP);
    lv_obj_set_style_bg_color(row_7days, IVF_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(row_7days, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row_7days, 0, 0);
    lv_obj_set_style_radius(row_7days, IVF_CARD_RADIUS, 0);
    lv_obj_add_event_cb(row_7days, on_picker_7days_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *row_7days_lbl = lv_label_create(row_7days);
    lv_label_set_text(row_7days_lbl, "7 Days");
    lv_obj_set_style_text_font(row_7days_lbl, IVF_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(row_7days_lbl, IVF_COLOR_TEXT, 0);
    lv_obj_center(row_7days_lbl);

    /* Populate the chart with real history_manager data (Last 7 Days default) */
    apply_chart_mode(CHART_MODE_LAST_7_DAYS);

    ESP_LOGD(TAG, "Chart screen created (Phase 5.6 simplified Today/7-Days picker)");
    return s_scr;
}

void screen_chart_refresh(void)
{
    /* Requirement: the Chart screen always opens in Last-7-Days mode,
     * regardless of what was last displayed before the user navigated away. */
    apply_chart_mode(CHART_MODE_LAST_7_DAYS);
}
