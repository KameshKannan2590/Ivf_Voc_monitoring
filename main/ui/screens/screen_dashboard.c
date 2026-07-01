#include "screen_dashboard.h"
#include "ui/ui.h"
#include "header.h"
#include "card.h"
#include "assets.h"
#include "voc_gauge.h"
#include "sensor_manager.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "dash";

/* ── Runtime widget handles ──────────────────────────────────────────────── */
static lv_obj_t    *s_scr            = NULL;
static header_t    *s_hdr            = NULL;
static voc_gauge_t *s_gauge          = NULL;
static lv_obj_t    *s_lbl_temp_value = NULL;
static lv_obj_t    *s_lbl_hum_value  = NULL;

/* ── Card layout ─────────────────────────────────────────────────────────── */
#define CARD_Y   281
#define CARD_W   126
#define CARD_H    77   /* icon + label + value; no sparkline */

/* ── Styles ──────────────────────────────────────────────────────────────── */
static lv_style_t sty_content;
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
}

/* ── Menu button callback ────────────────────────────────────────────────── */

static void on_menu_btn(void *user_data)
{
    (void)user_data;
    ui_nav_drawer_toggle();
}

/* ── Sensor card (card_t + assets) ──────────────────────────────────────── */

typedef enum { CARD_TEMP, CARD_HUM } card_type_t;

static void build_sensor_card(lv_obj_t *content,
                               int x, int y,
                               card_type_t type,
                               const char *label_str,
                               const char *value_str,
                               lv_obj_t **out_val_lbl)
{
    card_cfg_t ccfg = {
        .w            = CARD_W,
        .h            = CARD_H,
        .radius       = IVF_CARD_RADIUS,
        .bg_color     = IVF_COLOR_CARD,
        .border_color = IVF_COLOR_BORDER,
        .border_width = 1,
        .pad          = IVF_PAD,
        .shadow       = false,
        .title        = NULL,
    };
    card_t   *c    = card_create(content, &ccfg);
    lv_obj_t *card = card_get_obj(c);
    lv_obj_set_pos(card, x, y);

    if (type == CARD_TEMP) {
        assets_draw_thermometer(card, 100, 2, IVF_COLOR_PRIMARY);
    } else {
        assets_draw_humidity(card, 100, 2, IVF_COLOR_PRIMARY);
    }

    lv_obj_t *lbl_name = lv_label_create(card);
    lv_label_set_text(lbl_name, label_str);
    lv_obj_set_style_text_font(lbl_name, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_name, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_name, 0, 2);

    *out_val_lbl = lv_label_create(card);
    lv_label_set_text(*out_val_lbl, value_str);
    lv_obj_set_style_text_font(*out_val_lbl, IVF_FONT_LARGE, 0);
    lv_obj_set_style_text_color(*out_val_lbl, IVF_COLOR_TEXT, 0);
    lv_obj_set_pos(*out_val_lbl, 0, 28);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

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

    /* ── Header ── */
    s_hdr = header_create(s_scr);
    header_set_title(s_hdr, "DASHBOARD");
    header_set_wifi_strength(s_hdr, WIFI_STRENGTH_HIGH);
    header_set_sd_status(s_hdr, SD_STATUS_ABSENT);
    header_set_time(s_hdr, "08:25 AM");
    header_set_date(s_hdr, "May 24, 2026");
    header_enable_menu(s_hdr, on_menu_btn, NULL);

    /* ── Content container (y=50, h=430) ── */
    lv_obj_t *content = lv_obj_create(s_scr);
    lv_obj_set_pos(content, 0, IVF_HEADER_H);
    lv_obj_set_size(content, IVF_SCREEN_W, IVF_CONTENT_H);
    lv_obj_add_style(content, &sty_content, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    /* ── VOC Gauge ── */
    s_gauge = voc_gauge_create(content);
    voc_gauge_set_value(s_gauge, VOC_GAUGE_NO_READING);   /* grey "---" badge until first sensor read */

    /* ── Temperature sensor card ── */
    build_sensor_card(content, IVF_PAD, CARD_Y,
                      CARD_TEMP, "TEMPERATURE",
                      "-- \xc2\xb0""C",
                      &s_lbl_temp_value);

    /* ── Humidity sensor card ── */
    build_sensor_card(content, IVF_SCREEN_W / 2 + 4, CARD_Y,
                      CARD_HUM, "HUMIDITY",
                      "-- %",
                      &s_lbl_hum_value);

    ESP_LOGD(TAG, "Dashboard created");
    return s_scr;
}

void screen_dashboard_update(void)
{
    sensor_data_t d;
    sensor_manager_get_data(&d);

    char buf[24];

    /* VOC gauge — one call handles arcs, value label, badge, and animation */
    if (d.sensor_ok)
        voc_gauge_set_value(s_gauge, (uint16_t)d.voc_ppb);
    else
        voc_gauge_set_value(s_gauge, VOC_GAUGE_NO_READING);

    /* Temperature */
    if (s_lbl_temp_value) {
        if (d.sensor_ok)
            snprintf(buf, sizeof(buf), "%.1f \xc2\xb0""C", d.temperature_c);
        else
            snprintf(buf, sizeof(buf), "-- \xc2\xb0""C");
        lv_label_set_text(s_lbl_temp_value, buf);
    }

    /* Humidity */
    if (s_lbl_hum_value) {
        if (d.sensor_ok)
            snprintf(buf, sizeof(buf), "%.0f %%", d.humidity_pct);
        else
            snprintf(buf, sizeof(buf), "-- %%");
        lv_label_set_text(s_lbl_hum_value, buf);
    }
}

void dashboard_set_time(const char *time_str)
{
    if (s_hdr && time_str)
        header_set_time(s_hdr, time_str);
}

void dashboard_set_date(const char *date_str)
{
    if (s_hdr && date_str)
        header_set_date(s_hdr, date_str);
}
