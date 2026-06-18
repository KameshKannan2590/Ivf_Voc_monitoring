#include "screen_dashboard.h"
#include "ui/ui.h"
#include "sensor_manager.h"
#include "lvgl.h"
#include "esp_log.h"
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "dash";

/* ── runtime widget handles ──────────────────────────────────────────────── */
static lv_obj_t *s_scr            = NULL;
static lv_obj_t *s_lbl_tvoc_value = NULL;
static lv_obj_t *s_lbl_temp_value = NULL;
static lv_obj_t *s_lbl_hum_value  = NULL;
static lv_obj_t *s_lbl_level      = NULL;
static lv_obj_t *s_badge          = NULL;   /* badge container — bg_color updated at runtime */
static lv_obj_t *s_chart_temp     = NULL;
static lv_obj_t *s_chart_hum      = NULL;
static lv_obj_t *s_lbl_time       = NULL;
static lv_obj_t *s_lbl_date       = NULL;

/* chart series handles — needed by screen_dashboard_update() */
static lv_chart_series_t *s_ser_temp = NULL;
static lv_chart_series_t *s_ser_hum  = NULL;

/* ── gauge constants ─────────────────────────────────────────────────────── */
#define ARC_SIZE    210
#define ARC_WIDTH    18   /* thicker track matches design */
#define ARC_CX      136   /* arc centre x in content-relative coords */
#define ARC_CY      160   /* arc centre y — shifted down to clear TVOC title */
#define ARC_TOP_X   (ARC_CX - ARC_SIZE / 2)   /* = 31  */
#define ARC_TOP_Y   (ARC_CY - ARC_SIZE / 2)   /* = 55  */
#define R_LABEL     108   /* scale-label orbit radius — reduced to prevent overlap */

/* zone boundary angles (LVGL: 0°=3 o'clock, CW)
 * full sweep 135°→45° = 270°
 * 0→135°, 250→202.5°, 500→270°, 750→337.5°, 1000→45° */
#define A_START   135
#define A_G_END   202
#define A_Y_END   270
#define A_O_END   338
#define A_END      45

/* local yellow (not in ui.h palette) */
#define DASH_COLOR_YELLOW  lv_color_hex(0xFDD835)

/* mock values */
#define MOCK_TVOC   245
#define MOCK_TEMP   28.4f
#define MOCK_HUM    63.0f

/* card layout */
#define CARD_Y   255   /* content-relative top; arc bottom = 265, gap = 7 */
#define CARD_W   124
#define CARD_H   110

/* ── styles (init-once) ──────────────────────────────────────────────────── */
static lv_style_t sty_content;
static lv_style_t sty_card;
static lv_style_t sty_badge;
static lv_style_t sty_chart;
static bool       s_styles_ready = false;

static void build_styles(void)
{
    if (s_styles_ready) return;
    s_styles_ready = true;

    lv_style_init(&sty_content);
    lv_style_set_bg_color(&sty_content, IVF_COLOR_BG);
    lv_style_set_bg_opa(&sty_content, LV_OPA_COVER);
    lv_style_set_border_width(&sty_content, 0);
    lv_style_set_pad_all(&sty_content, 0);
    lv_style_set_radius(&sty_content, 0);

    lv_style_init(&sty_card);
    lv_style_set_bg_color(&sty_card, IVF_COLOR_CARD);
    lv_style_set_bg_opa(&sty_card, LV_OPA_COVER);
    lv_style_set_border_color(&sty_card, IVF_COLOR_BORDER);
    lv_style_set_border_width(&sty_card, 1);
    lv_style_set_radius(&sty_card, IVF_CARD_RADIUS);
    lv_style_set_pad_all(&sty_card, IVF_PAD);

    lv_style_init(&sty_badge);
    lv_style_set_bg_color(&sty_badge, IVF_COLOR_GOOD);
    lv_style_set_bg_opa(&sty_badge, LV_OPA_COVER);
    lv_style_set_radius(&sty_badge, 11);
    lv_style_set_border_width(&sty_badge, 0);
    lv_style_set_pad_hor(&sty_badge, 10);
    lv_style_set_pad_ver(&sty_badge, 3);

    lv_style_init(&sty_chart);
    lv_style_set_bg_opa(&sty_chart, LV_OPA_TRANSP);
    lv_style_set_border_width(&sty_chart, 0);
    lv_style_set_pad_all(&sty_chart, 0);
}

/* ── helpers ─────────────────────────────────────────────────────────────── */

static lv_obj_t *make_arc_zone(lv_obj_t *parent,
                                uint16_t a_start, uint16_t a_end,
                                lv_color_t color, uint16_t width)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, ARC_SIZE, ARC_SIZE);
    lv_obj_set_pos(arc, ARC_TOP_X, ARC_TOP_Y);
    lv_arc_set_bg_angles(arc, a_start, a_end);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);
    return arc;
}

static lv_obj_t *make_scale_label_abs(
    lv_obj_t *parent,
    const char *text,
    lv_coord_t x,
    lv_coord_t y)
{
    lv_obj_t *lbl = lv_label_create(parent);

    lv_label_set_text(lbl, text);

    lv_obj_set_style_text_font(lbl, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl, IVF_COLOR_TEXT_MUTED, 0);

    lv_obj_update_layout(lbl);

    lv_coord_t w = lv_obj_get_width(lbl);
    lv_coord_t h = lv_obj_get_height(lbl);

    lv_obj_set_pos(lbl,
                   x - (w / 2),
                   y - (h / 2));

    return lbl;
}

/* ── gauge section ───────────────────────────────────────────────────────── */

static void build_gauge(lv_obj_t *content)
{
    /* "TVOC (ppb)" heading — more top padding (y=4) so "500" label doesn't crowd it */
    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text(title, "TVOC (ppb)");
    lv_obj_set_style_text_font(title, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(title, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    /* grey background track */
    make_arc_zone(content, A_START, A_END, lv_color_hex(0xE0E0E0), ARC_WIDTH);

    /* 4 colour zones */
    make_arc_zone(content, A_START, A_G_END, IVF_COLOR_GOOD,    ARC_WIDTH);
    make_arc_zone(content, A_G_END, A_Y_END, DASH_COLOR_YELLOW, ARC_WIDTH);
    make_arc_zone(content, A_Y_END, A_O_END, IVF_COLOR_WARNING, ARC_WIDTH);
    make_arc_zone(content, A_O_END, A_END,   IVF_COLOR_DANGER,  ARC_WIDTH);

    /* scale labels — pixel-exact positions tuned on device */
    make_scale_label_abs(content, "0",    48,  245);
    make_scale_label_abs(content, "250",  20,  125);
    make_scale_label_abs(content, "500",  136,  40);
    make_scale_label_abs(content, "750",  253, 125);
    make_scale_label_abs(content, "1000", 220, 245);

    /* ── centre value stack — transparent flex container ── */
    /* container vertically centred at ARC_CY: pos top = ARC_CY − height/2 */
    lv_obj_t *ctr = lv_obj_create(content);
    lv_obj_set_size(ctr, 130, 115);
    lv_obj_set_pos(ctr, ARC_CX - 65, ARC_CY - 57);  /* (71, 103) */
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

    /* "245" */
    s_lbl_tvoc_value = lv_label_create(ctr);
    lv_label_set_text(s_lbl_tvoc_value, "245");
    lv_obj_set_style_text_font(s_lbl_tvoc_value, IVF_FONT_HUGE, 0);
    lv_obj_set_style_text_color(s_lbl_tvoc_value, IVF_COLOR_TEXT, 0);

    /* "ppb" */
    lv_obj_t *lbl_unit = lv_label_create(ctr);
    lv_label_set_text(lbl_unit, "ppb");
    lv_obj_set_style_text_font(lbl_unit, IVF_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(lbl_unit, IVF_COLOR_TEXT_MUTED, 0);

    /* GOOD badge — pointer kept for runtime colour updates */
    lv_obj_t *badge = lv_obj_create(ctr);
    s_badge = badge;
    lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_style(badge, &sty_badge, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_level = lv_label_create(badge);
    lv_label_set_text(s_lbl_level, "GOOD  " LV_SYMBOL_OK);
    lv_obj_set_style_text_font(s_lbl_level, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_lbl_level, lv_color_white(), 0);
    lv_obj_center(s_lbl_level);
}

/* ── thermometer icon ────────────────────────────────────────────────────── */

static void make_therm_icon(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_set_size(icon, 14, 26);
    lv_obj_set_pos(icon, x, y);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_set_style_pad_all(icon, 0, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *stem = lv_obj_create(icon);
    lv_obj_set_size(stem, 6, 16);
    lv_obj_set_pos(stem, 4, 0);
    lv_obj_set_style_bg_color(stem, IVF_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(stem, 0, 0);
    lv_obj_set_style_radius(stem, 3, 0);
    lv_obj_clear_flag(stem, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bulb = lv_obj_create(icon);
    lv_obj_set_size(bulb, 14, 14);
    lv_obj_set_pos(bulb, 0, 12);
    lv_obj_set_style_bg_color(bulb, IVF_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(bulb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bulb, 0, 0);
    lv_obj_set_style_radius(bulb, 7, 0);
    lv_obj_clear_flag(bulb, LV_OBJ_FLAG_SCROLLABLE);
}

/* ── water-drop icon ─────────────────────────────────────────────────────── */

static void make_drop_icon(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *drop = lv_obj_create(parent);
    lv_obj_set_size(drop, 12, 16);
    lv_obj_set_pos(drop, x, y);
    lv_obj_set_style_bg_color(drop, IVF_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(drop, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(drop, 0, 0);
    lv_obj_set_style_radius(drop, 6, 0);
    lv_obj_clear_flag(drop, LV_OBJ_FLAG_SCROLLABLE);
}

/* ── sensor card ─────────────────────────────────────────────────────────── */

typedef enum { CARD_TEMP, CARD_HUM } card_type_t;

static lv_chart_series_t *build_sensor_card(lv_obj_t *content,
                                             int x, int y,
                                             card_type_t type,
                                             const char *label_str,
                                             const char *value_str,
                                             lv_obj_t **out_val_lbl,
                                             lv_obj_t **out_chart)
{
    lv_obj_t *card = lv_obj_create(content);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_add_style(card, &sty_card, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* icon */
    if (type == CARD_TEMP) {
        make_therm_icon(card, 0, 0);
    } else {
        make_drop_icon(card, 0, 2);
    }

    /* "TEMPERATURE" / "HUMIDITY" */
    lv_obj_t *lbl_name = lv_label_create(card);
    lv_label_set_text(lbl_name, label_str);
    lv_obj_set_style_text_font(lbl_name, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_name, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_name, 18, 2);

    /* value — IVF_FONT_LARGE (24pt) to match design prominence */
    *out_val_lbl = lv_label_create(card);
    lv_label_set_text(*out_val_lbl, value_str);
    lv_obj_set_style_text_font(*out_val_lbl, IVF_FONT_LARGE, 0);
    lv_obj_set_style_text_color(*out_val_lbl, IVF_COLOR_TEXT, 0);
    lv_obj_set_pos(*out_val_lbl, 0, 28);

    /* sparkline — reduced height (36px) to fit in 110px card */
    const int chart_h = 36;
    *out_chart = lv_chart_create(card);
    lv_obj_set_size(*out_chart, CARD_W - IVF_PAD * 2, chart_h);
    lv_obj_align(*out_chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_style(*out_chart, &sty_chart, 0);
    lv_chart_set_type(*out_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(*out_chart, 30);
    lv_chart_set_div_line_count(*out_chart, 0, 0);
    /* remove point dots → smooth polyline */
    lv_obj_set_style_size(*out_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(*out_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_width(*out_chart, 0, LV_PART_MAIN);

    lv_chart_series_t *ser =
        lv_chart_add_series(*out_chart, IVF_COLOR_PRIMARY, LV_CHART_AXIS_PRIMARY_Y);
    return ser;
}

/* ── public API ──────────────────────────────────────────────────────────── */

lv_obj_t *screen_dashboard_create(void)
{
    build_styles();

    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, IVF_SCREEN_W, IVF_SCREEN_H);
    lv_obj_set_style_bg_color(s_scr, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── header ── */
    lv_obj_t *hdr = ui_build_header(s_scr, "AIR QUALITY MONITOR");

    /* re-align title: shrink font + move left so right side has room */
    lv_obj_t *title_lbl = lv_obj_get_child(hdr, 0);
    lv_obj_set_style_text_font(title_lbl, IVF_FONT_SMALL, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 26, 0);

    /* leaf indicator dot — left side */
    lv_obj_t *leaf = lv_obj_create(hdr);
    lv_obj_set_size(leaf, 14, 14);
    lv_obj_set_style_bg_color(leaf, IVF_COLOR_GOOD, 0);
    lv_obj_set_style_bg_opa(leaf, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(leaf, 0, 0);
    lv_obj_set_style_radius(leaf, 7, 0);
    lv_obj_align(leaf, LV_ALIGN_LEFT_MID, 8, 0);

    /* SD card icon — top-right, rightmost */
    lv_obj_t *lbl_sd = lv_label_create(hdr);
    lv_label_set_text(lbl_sd, LV_SYMBOL_SD_CARD);
    lv_obj_set_style_text_font(lbl_sd, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_sd, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_sd, LV_ALIGN_TOP_RIGHT, -8, 4);

    /* WiFi icon — just left of SD card */
    lv_obj_t *lbl_wifi = lv_label_create(hdr);
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(lbl_wifi, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_wifi, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_TOP_RIGHT, -28, 4);

    /* time — below WiFi/SD row */
    s_lbl_time = lv_label_create(hdr);
    lv_label_set_text(s_lbl_time, "08:25 AM");
    lv_obj_set_style_text_font(s_lbl_time, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_lbl_time, IVF_COLOR_TEXT, 0);
    lv_obj_align(s_lbl_time, LV_ALIGN_TOP_RIGHT, -8, 18);

    /* date */
    s_lbl_date = lv_label_create(hdr);
    lv_label_set_text(s_lbl_date, "May 24, 2025");
    lv_obj_set_style_text_font(s_lbl_date, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_lbl_date, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align(s_lbl_date, LV_ALIGN_TOP_RIGHT, -8, 30);

    /* ── content container (y=44, h=386) ── */
    lv_obj_t *content = lv_obj_create(s_scr);
    lv_obj_set_pos(content, 0, IVF_HEADER_H);
    lv_obj_set_size(content, IVF_SCREEN_W, IVF_CONTENT_H);
    lv_obj_add_style(content, &sty_content, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    /* ── multi-zone arc gauge ── */
    build_gauge(content);

    /* ── sensor cards ── */
    /* CARD_Y=272, arc bottom=265, gap=7 */
    /* card bottom=272+110=382, content_h=386, margin=4px */

    /* Initial placeholder data — matches simulation base values so charts look
     * plausible from first render; real values arrive within 1 s via update(). */
    static const int16_t td[] = {
        225,226,224,226,225,224,226,225,224,226,
        225,224,226,225,224,226,225,224,225,225,
        226,225,224,226,225,224,225,225,224,225
    };
    s_ser_temp =
        build_sensor_card(content,
                          IVF_PAD, CARD_Y,
                          CARD_TEMP, "TEMPERATURE",
                          "-- \xc2\xb0""C",
                          &s_lbl_temp_value, &s_chart_temp);
    /* range: temp × 10 units, sim output ~21.7–23.3 °C */
    lv_chart_set_range(s_chart_temp, LV_CHART_AXIS_PRIMARY_Y, 200, 260);
    for (int i = 0; i < 30; i++)
        lv_chart_set_next_value(s_chart_temp, s_ser_temp, td[i]);

    static const int16_t hd[] = {
        48,49,47,50,48,46,49,47,50,49,
        48,47,49,48,47,49,48,47,48,48,
        49,48,47,49,48,47,48,48,47,48
    };
    s_ser_hum =
        build_sensor_card(content,
                          IVF_SCREEN_W / 2 + 4, CARD_Y,
                          CARD_HUM, "HUMIDITY",
                          "-- %",
                          &s_lbl_hum_value, &s_chart_hum);
    /* range: humidity %, sim output ~43–53 % */
    lv_chart_set_range(s_chart_hum, LV_CHART_AXIS_PRIMARY_Y, 40, 60);
    for (int i = 0; i < 30; i++)
        lv_chart_set_next_value(s_chart_hum, s_ser_hum, hd[i]);

    lv_chart_refresh(s_chart_temp);
    lv_chart_refresh(s_chart_hum);

    /* ── tab bar ── */
    ui_build_tab_bar(s_scr, SCREEN_DASHBOARD);

    ESP_LOGD(TAG, "Dashboard Phase 3A created");
    return s_scr;
}

void screen_dashboard_update(void)
{
    sensor_data_t d;
    sensor_manager_get_data(&d);

    char buf[24];

    /* ── TVOC value ── */
    if (s_lbl_tvoc_value) {
        if (d.sensor_ok) {
            snprintf(buf, sizeof(buf), "%.0f", d.voc_ppb);
        } else {
            snprintf(buf, sizeof(buf), "--");
        }
        lv_label_set_text(s_lbl_tvoc_value, buf);
    }

    /* ── Level badge: colour + text driven by VOC threshold ── */
    if (s_badge && s_lbl_level) {
        lv_color_t  badge_color;
        const char *badge_text;

        if (!d.sensor_ok) {
            badge_color = lv_color_hex(0x9E9E9E);
            badge_text  = "ERROR";
        } else {
            switch (sensor_get_voc_level(d.voc_ppb)) {
                case SENSOR_LEVEL_WARNING:
                    badge_color = IVF_COLOR_WARNING;
                    badge_text  = "WARN  " LV_SYMBOL_WARNING;
                    break;
                case SENSOR_LEVEL_DANGER:
                    badge_color = IVF_COLOR_DANGER;
                    badge_text  = "ALARM " LV_SYMBOL_WARNING;
                    break;
                default: /* GOOD */
                    badge_color = IVF_COLOR_GOOD;
                    badge_text  = "GOOD  " LV_SYMBOL_OK;
                    break;
            }
        }
        lv_obj_set_style_bg_color(s_badge, badge_color, 0);
        lv_label_set_text(s_lbl_level, badge_text);
    }

    /* ── Temperature ── */
    if (s_lbl_temp_value) {
        if (d.sensor_ok) {
            snprintf(buf, sizeof(buf), "%.1f \xc2\xb0""C", d.temperature_c);
        } else {
            snprintf(buf, sizeof(buf), "-- \xc2\xb0""C");
        }
        lv_label_set_text(s_lbl_temp_value, buf);
    }

    /* ── Humidity ── */
    if (s_lbl_hum_value) {
        if (d.sensor_ok) {
            snprintf(buf, sizeof(buf), "%.0f %%", d.humidity_pct);
        } else {
            snprintf(buf, sizeof(buf), "-- %%");
        }
        lv_label_set_text(s_lbl_hum_value, buf);
    }

    /* ── Sparklines — push one point per call (1 Hz) ── */
    if (d.sensor_ok) {
        if (s_chart_temp && s_ser_temp) {
            lv_chart_set_next_value(s_chart_temp, s_ser_temp,
                                    (lv_coord_t)(d.temperature_c * 10.0f));
            lv_chart_refresh(s_chart_temp);
        }
        if (s_chart_hum && s_ser_hum) {
            lv_chart_set_next_value(s_chart_hum, s_ser_hum,
                                    (lv_coord_t)d.humidity_pct);
            lv_chart_refresh(s_chart_hum);
        }
    }
}

void dashboard_set_time(const char *time_str)
{
    if (s_lbl_time && time_str)
        lv_label_set_text(s_lbl_time, time_str);
}

void dashboard_set_date(const char *date_str)
{
    if (s_lbl_date && date_str)
        lv_label_set_text(s_lbl_date, date_str);
}
