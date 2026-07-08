#include "screen_settings.h"
#include "ui/ui.h"
#include "header.h"
#include "card.h"
#include "data/config_manager.h"
#include "sensors/sensor_manager.h"
#include "data/alarm_manager.h"
#include "display/display_driver.h"
#include "display/display_power.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

static const char *TAG = "settings";

/* ── State ────────────────────────────────────────────────────────────────── */

static lv_obj_t *s_scr  = NULL;
static header_t  *s_hdr = NULL;

static lv_obj_t *s_lbl_brightness_pct = NULL;
#if 0 /* Brightness slider — replaced by a dropdown (BRIGHTNESS_OPTIONS) per request.
       * Kept, not deleted, in case a slider is wanted again later. */
static lv_obj_t *s_slider_brightness  = NULL;
#endif

static lv_obj_t *s_lbl_theme_val      = NULL;
static lv_obj_t *s_lbl_timeout_val    = NULL;
static lv_obj_t *s_lbl_disp_thr_val   = NULL;
static lv_obj_t *s_lbl_disp_max_val   = NULL;
static lv_obj_t *s_lbl_voc_warn_val   = NULL;
static lv_obj_t *s_lbl_voc_alarm_val  = NULL;

static lv_obj_t *s_alert_content   = NULL;
static lv_obj_t *s_alert_chevron   = NULL;
static bool      s_alert_expanded  = true;

/* Generic value-picker overlay (Today/7-Days-style backdrop from Chart,
 * generalized to N labelled options) — reused for every dropdown-style
 * field on this screen instead of duplicating the overlay per field. */
static lv_obj_t *s_picker_backdrop = NULL;
static lv_obj_t *s_picker_title    = NULL;
static lv_obj_t *s_picker_list     = NULL;

typedef void (*picker_apply_cb_t)(int32_t value);
static picker_apply_cb_t s_picker_cb = NULL;

typedef struct {
    const char *label;
    int32_t     value;
} picker_option_t;

/* The one option set every ppb-style dropdown on this screen uses. */
static const picker_option_t PPB_OPTIONS[] = {
    { "0 ppb",    0    },
    { "250 ppb",  250  },
    { "500 ppb",  500  },
    { "750 ppb",  750  },
    { "1000 ppb", 1000 },
};
#define PPB_OPTIONS_COUNT (sizeof(PPB_OPTIONS) / sizeof(PPB_OPTIONS[0]))

static const picker_option_t BRIGHTNESS_OPTIONS[] = {
    { "15%",  15  },   /* matches CONFIG_DIM_BRIGHTNESS_PCT — the visibility floor */
    { "25%",  25  },
    { "50%",  50  },
    { "75%",  75  },
    { "100%", 100 },
};
#define BRIGHTNESS_OPTIONS_COUNT (sizeof(BRIGHTNESS_OPTIONS) / sizeof(BRIGHTNESS_OPTIONS[0]))

static const picker_option_t THEME_OPTIONS[] = {
    { "Light", 0 },
    { "Dark",  1 },
};
#define THEME_OPTIONS_COUNT (sizeof(THEME_OPTIONS) / sizeof(THEME_OPTIONS[0]))

static const picker_option_t TIMEOUT_OPTIONS[] = {
    { "15 sec", CONFIG_TIMEOUT_15S },
    { "30 sec", CONFIG_TIMEOUT_30S },
    { "45 sec", CONFIG_TIMEOUT_45S },
    { "1 min",  CONFIG_TIMEOUT_60S },
    { "None",   CONFIG_TIMEOUT_NONE },
};
#define TIMEOUT_OPTIONS_COUNT (sizeof(TIMEOUT_OPTIONS) / sizeof(TIMEOUT_OPTIONS[0]))

/* ── Layout constants ─────────────────────────────────────────────────────── */
#define MARGIN          8
#define NAV_ROW_H       30   /* Theme / Screen Timeout / Threshold (ppb) / TVOC High Threshold */
#define ALERT_HEADER_H  44   /* "ALERT SETTINGS" collapsible header bar — unaffected by NAV_ROW_H */
#define ALERT_ROW_H     56
#define VALUE_RIGHT_OFS -12

#define PICKER_PANEL_W   180
#define PICKER_ROW_H     36

/* ── Menu button callback ────────────────────────────────────────────────── */

static void on_menu_btn(void *user_data)
{
    (void)user_data;
    ui_nav_drawer_toggle();
}

/* ── Generic value picker ─────────────────────────────────────────────────── */

static void on_picker_backdrop_click(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_picker_backdrop, LV_OBJ_FLAG_HIDDEN);
}

static void on_picker_option_click(lv_event_t *e)
{
    int32_t value = (int32_t)(intptr_t)lv_event_get_user_data(e);
    lv_obj_add_flag(s_picker_backdrop, LV_OBJ_FLAG_HIDDEN);
    if (s_picker_cb) s_picker_cb(value);
}

static void open_picker(const char *title, const picker_option_t *options, size_t count,
                         int32_t current_value, picker_apply_cb_t cb)
{
    s_picker_cb = cb;
    lv_label_set_text(s_picker_title, title);
    lv_obj_clean(s_picker_list);

    for (size_t i = 0; i < count; i++) {
        lv_obj_t *row = lv_btn_create(s_picker_list);
        lv_obj_set_size(row, LV_PCT(100), PICKER_ROW_H);
        bool selected = (options[i].value == current_value);
        lv_obj_set_style_bg_color(row, selected ? IVF_COLOR_PRIMARY : IVF_COLOR_CARD, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, IVF_CARD_RADIUS, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_add_event_cb(row, on_picker_option_click, LV_EVENT_CLICKED,
                             (void *)(intptr_t)options[i].value);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, options[i].label);
        lv_obj_set_style_text_font(lbl, IVF_FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl, selected ? lv_color_white() : IVF_COLOR_TEXT, 0);
        lv_obj_center(lbl);
    }

    lv_obj_clear_flag(s_picker_backdrop, LV_OBJ_FLAG_HIDDEN);
}

/* ── Field apply callbacks — persist, live-reload the relevant subsystem,
 * update the row's displayed value ──────────────────────────────────────── */

/* Theme can't be applied live (see ui.h) — every screen's colors are set
 * once, when it's built. Reboot immediately so the new theme is in effect
 * by the time the user is looking at the screen again. */
static void on_theme_selected(int32_t dark)
{
    config_manager_set_dark_mode(dark != 0);
    ESP_LOGI(TAG, "Theme changed to %s, restarting to apply", dark ? "Dark" : "Light");
    esp_restart();
}

static void on_timeout_selected(int32_t sec)
{
    config_manager_set_timeout_sec((uint16_t)sec);
    display_power_reload_settings();
    const char *txt = "None";
    for (size_t i = 0; i < TIMEOUT_OPTIONS_COUNT; i++) {
        if (TIMEOUT_OPTIONS[i].value == sec) { txt = TIMEOUT_OPTIONS[i].label; break; }
    }
    lv_label_set_text_fmt(s_lbl_timeout_val, "%s  %s", txt, LV_SYMBOL_RIGHT);
}

static void on_disp_threshold_selected(int32_t ppb)
{
    config_manager_set_display_threshold_ppb(ppb);
    lv_label_set_text_fmt(s_lbl_disp_thr_val, "%d ppb  %s", (int)ppb, LV_SYMBOL_RIGHT);
}

static void on_disp_max_selected(int32_t ppb)
{
    config_manager_set_display_max_ppb(ppb);
    lv_label_set_text_fmt(s_lbl_disp_max_val, "%d ppb  %s", (int)ppb, LV_SYMBOL_RIGHT);
}

static void on_voc_warn_selected(int32_t ppb)
{
    config_manager_set_voc_warn_ppb(ppb);
    sensor_manager_reload_thresholds();
    lv_label_set_text_fmt(s_lbl_voc_warn_val, "%d ppb  %s", (int)ppb, LV_SYMBOL_RIGHT);
}

static void on_voc_alarm_selected(int32_t ppb)
{
    config_manager_set_voc_alarm_ppb(ppb);
    sensor_manager_reload_thresholds();
    alarm_manager_reload_thresholds();
    lv_label_set_text_fmt(s_lbl_voc_alarm_val, "%d ppb  %s", (int)ppb, LV_SYMBOL_RIGHT);
}

/* ── Row click handlers — each just opens the picker with the right
 * options/current-value/callback ────────────────────────────────────────── */

static void on_theme_row_click(lv_event_t *e)
{
    (void)e;
    open_picker("Theme", THEME_OPTIONS, THEME_OPTIONS_COUNT,
                config_manager_get_dark_mode() ? 1 : 0, on_theme_selected);
}

static void on_timeout_row_click(lv_event_t *e)
{
    (void)e;
    open_picker("Screen Timeout", TIMEOUT_OPTIONS, TIMEOUT_OPTIONS_COUNT,
                config_manager_get_timeout_sec(), on_timeout_selected);
}

static void on_disp_threshold_row_click(lv_event_t *e)
{
    (void)e;
    open_picker("Threshold (ppb)", PPB_OPTIONS, PPB_OPTIONS_COUNT,
                config_manager_get_display_threshold_ppb(), on_disp_threshold_selected);
}

static void on_disp_max_row_click(lv_event_t *e)
{
    (void)e;
    open_picker("TVOC High Threshold", PPB_OPTIONS, PPB_OPTIONS_COUNT,
                config_manager_get_display_max_ppb(), on_disp_max_selected);
}

static void on_voc_warn_row_click(lv_event_t *e)
{
    (void)e;
    open_picker("TVOC Alert Threshold", PPB_OPTIONS, PPB_OPTIONS_COUNT,
                config_manager_get_voc_warn_ppb(), on_voc_warn_selected);
}

static void on_voc_alarm_row_click(lv_event_t *e)
{
    (void)e;
    open_picker("High Alert Threshold", PPB_OPTIONS, PPB_OPTIONS_COUNT,
                config_manager_get_voc_alarm_ppb(), on_voc_alarm_selected);
}

/* ── Brightness — dropdown (was a slider; kept below, commented, not
 * deleted, in case it's wanted again) ───────────────────────────────────── */

#if 0
static void on_brightness_changed(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t pct = lv_slider_get_value(slider);
    /* Live preview: apply immediately so dragging the slider is visibly
     * responsive, without waiting for release. */
    display_set_brightness((uint8_t)pct);
    lv_label_set_text_fmt(s_lbl_brightness_pct, "%d%%", (int)pct);
}

static void on_brightness_released(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t pct = lv_slider_get_value(slider);
    /* Persist (and update display_power's cached awake-brightness) only on
     * release, not on every drag tick — NVS writes are not free. */
    config_manager_set_brightness_pct((uint8_t)pct);
    display_power_reload_settings();
}
#endif

static void on_brightness_selected(int32_t pct)
{
    config_manager_set_brightness_pct((uint8_t)pct);
    display_power_reload_settings();
    lv_label_set_text_fmt(s_lbl_brightness_pct, "%d%%  %s", (int)pct, LV_SYMBOL_RIGHT);
}

static void on_brightness_row_click(lv_event_t *e)
{
    (void)e;
    open_picker("Brightness", BRIGHTNESS_OPTIONS, BRIGHTNESS_OPTIONS_COUNT,
                config_manager_get_brightness_pct(), on_brightness_selected);
}

/* ── Alert Settings collapse/expand ───────────────────────────────────────── */

static void on_alert_header_click(lv_event_t *e)
{
    (void)e;
    s_alert_expanded = !s_alert_expanded;
    if (s_alert_expanded) {
        lv_obj_clear_flag(s_alert_content, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_alert_chevron, LV_SYMBOL_UP);
    } else {
        lv_obj_add_flag(s_alert_content, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_alert_chevron, LV_SYMBOL_DOWN);
    }
}

/* ── Row builders ─────────────────────────────────────────────────────────── */

/* A single "label ... value >" row, divider on the bottom, tappable. */
static lv_obj_t *make_nav_row(lv_obj_t *parent, const char *label, const char *value,
                               lv_event_cb_t on_click, lv_obj_t **out_value_lbl)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), NAV_ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    if (on_click) lv_obj_add_event_cb(row, on_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_name = lv_label_create(row);
    lv_label_set_text(lbl_name, label);
    lv_obj_set_style_text_font(lbl_name, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_name, IVF_COLOR_TEXT, 0);
    lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, MARGIN, 0);

    lv_obj_t *lbl_val = lv_label_create(row);
    lv_label_set_text(lbl_val, value);
    lv_obj_set_style_text_font(lbl_val, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_val, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_val, LV_ALIGN_RIGHT_MID, VALUE_RIGHT_OFS, 0);
    if (out_value_lbl) *out_value_lbl = lbl_val;

    return row;
}

/* An alert-settings row: same top line as make_nav_row(), plus a muted
 * subtitle line underneath ("Set warning threshold for TVOC level"). */
static lv_obj_t *make_alert_row(lv_obj_t *parent, const char *label, const char *subtitle,
                                 const char *value, lv_event_cb_t on_click,
                                 lv_obj_t **out_value_lbl)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), ALERT_ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    if (on_click) lv_obj_add_event_cb(row, on_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_name = lv_label_create(row);
    lv_label_set_text(lbl_name, label);
    lv_obj_set_style_text_font(lbl_name, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_name, IVF_COLOR_TEXT, 0);
    lv_obj_set_pos(lbl_name, MARGIN, 8);

    lv_obj_t *lbl_val = lv_label_create(row);
    lv_label_set_text(lbl_val, value);
    lv_obj_set_style_text_font(lbl_val, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_val, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_val, LV_ALIGN_TOP_RIGHT, VALUE_RIGHT_OFS, 8);
    if (out_value_lbl) *out_value_lbl = lbl_val;

    lv_obj_t *lbl_sub = lv_label_create(row);
    lv_label_set_text(lbl_sub, subtitle);
    lv_obj_set_style_text_font(lbl_sub, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_sub, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_sub, MARGIN, 28);

    return row;
}

/* ── Public API ────────────────────────────────────────────────────────────── */

lv_obj_t *screen_settings_create(void)
{
    /* ── Screen root ──────────────────────────────────────────────────────── */
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, IVF_SCREEN_W, IVF_SCREEN_H);
    lv_obj_set_style_bg_color(s_scr, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Header — shared component, same as every other screen ─────────────── */
    s_hdr = header_create(s_scr);
    header_set_title(s_hdr, "SETTINGS");
    header_set_wifi_strength(s_hdr, WIFI_STRENGTH_HIGH);
    header_set_sd_status(s_hdr, SD_STATUS_ABSENT);
    header_set_time(s_hdr, "08:25 AM");
    header_set_date(s_hdr, "May 24, 2026");
    header_enable_menu(s_hdr, on_menu_btn, NULL);

    /* ── Content — unlike Dashboard/Chart/Logs, this screen scrolls: there
     * are more rows here than fit in 430 px, especially with Alert Settings
     * expanded (its default state). Flex-column so the collapsible Alert
     * Settings section reflows everything below it automatically instead
     * of needing manual y-offset bookkeeping per row. ────────────────────── */
    lv_obj_t *content = lv_obj_create(s_scr);
    lv_obj_set_size(content, IVF_SCREEN_W, IVF_CONTENT_H);
    lv_obj_set_pos(content, 0, IVF_HEADER_H);
    lv_obj_set_style_bg_color(content, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, MARGIN, 0);
    lv_obj_set_style_pad_row(content, MARGIN, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* ── Brightness — dropdown row, same visual pattern as Theme/Timeout/
     * thresholds below (was a slider; see the #if 0 block above for that
     * implementation, kept rather than deleted). ──────────────────────────── */
    card_cfg_t bright_cfg = {
        .w = LV_PCT(100), .h = LV_SIZE_CONTENT, .radius = IVF_CARD_RADIUS,
        .bg_color = IVF_COLOR_CARD, .border_color = IVF_COLOR_BORDER,
        .border_width = 1, .pad = 0, .shadow = false, .title = NULL,
    };
    card_t   *bright_card = card_create(content, &bright_cfg);
    lv_obj_t *bright_body = card_get_content(bright_card);

    char bright_buf[16];
    snprintf(bright_buf, sizeof(bright_buf), "%d%%  %s",
             (int)config_manager_get_brightness_pct(), LV_SYMBOL_RIGHT);
    make_nav_row(bright_body, "Brightness", bright_buf, on_brightness_row_click, &s_lbl_brightness_pct);

#if 0
    /* ── Brightness (slider version) ─────────────────────────────────────── */
    lv_obj_t *lbl_bright_name = lv_label_create(bright_body);
    lv_label_set_text(lbl_bright_name, "Brightness");
    lv_obj_set_style_text_font(lbl_bright_name, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_bright_name, IVF_COLOR_TEXT, 0);
    lv_obj_set_pos(lbl_bright_name, 0, 0);

    uint8_t bright_pct = config_manager_get_brightness_pct();
    s_lbl_brightness_pct = lv_label_create(bright_body);
    lv_label_set_text_fmt(s_lbl_brightness_pct, "%d%%", (int)bright_pct);
    lv_obj_set_style_text_font(s_lbl_brightness_pct, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_lbl_brightness_pct, IVF_COLOR_TEXT, 0);
    lv_obj_align(s_lbl_brightness_pct, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_slider_brightness = lv_slider_create(bright_body);
    /* The object's own box must be tall enough to fully contain the round
     * knob (LV_PART_KNOB) — the knob is bigger than the track and, unlike
     * the track, doesn't get its size from this box. A too-short box (was
     * 8 px, knob ~16-20 px) meant the top/bottom of the knob overflowed the
     * slider's own bounds and got clipped by the card wrapping it (auto-
     * height containers don't reserve room for a child's overflow — the
     * same class of bug as the Chart axis-label clipping, Phase 5.6). */
    lv_obj_set_size(s_slider_brightness, LV_PCT(100), 20);
    lv_obj_set_pos(s_slider_brightness, 0, 30);
    /* Floor at CONFIG_DIM_BRIGHTNESS_PCT, not 0 — a manual setting should
     * never be able to go dimmer than the auto-dim level, or the user could
     * set the screen effectively invisible with no way to see it to fix it. */
    lv_slider_set_range(s_slider_brightness, CONFIG_DIM_BRIGHTNESS_PCT, 100);
    lv_slider_set_value(s_slider_brightness, bright_pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider_brightness, IVF_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider_brightness, IVF_COLOR_PRIMARY, LV_PART_KNOB);
    /* Explicit knob size/shape rather than relying on the default theme —
     * 16 px comfortably fits inside the 20 px box above with no overflow. */
    lv_obj_set_style_radius(s_slider_brightness, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_size(s_slider_brightness, 16, LV_PART_KNOB);
    lv_obj_add_event_cb(s_slider_brightness, on_brightness_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_slider_brightness, on_brightness_released, LV_EVENT_RELEASED, NULL);
#endif

    /* ── Theme / Screen Timeout / display-range rows ───────────────────────── */
    card_cfg_t rows_cfg = {
        .w = LV_PCT(100), .h = LV_SIZE_CONTENT, .radius = IVF_CARD_RADIUS,
        .bg_color = IVF_COLOR_CARD, .border_color = IVF_COLOR_BORDER,
        .border_width = 1, .pad = 0, .shadow = false, .title = NULL,
    };
    card_t   *rows_card = card_create(content, &rows_cfg);
    lv_obj_t *rows_body = card_get_content(rows_card);
    /* Column layout: each make_nav_row() call becomes the next flex item,
     * stacking top-to-bottom — without this they'd all land at (0,0) and
     * overlap (same bug already caught once in screen_logs.c). */
    lv_obj_set_flex_flow(rows_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rows_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    char timeout_buf[24];
    {
        const char *txt = "None";
        uint16_t sec = config_manager_get_timeout_sec();
        for (size_t i = 0; i < TIMEOUT_OPTIONS_COUNT; i++) {
            if (TIMEOUT_OPTIONS[i].value == sec) { txt = TIMEOUT_OPTIONS[i].label; break; }
        }
        snprintf(timeout_buf, sizeof(timeout_buf), "%s  %s", txt, LV_SYMBOL_RIGHT);
    }
    char disp_thr_buf[24], disp_max_buf[24];
    snprintf(disp_thr_buf, sizeof(disp_thr_buf), "%d ppb  %s",
             (int)config_manager_get_display_threshold_ppb(), LV_SYMBOL_RIGHT);
    snprintf(disp_max_buf, sizeof(disp_max_buf), "%d ppb  %s",
             (int)config_manager_get_display_max_ppb(), LV_SYMBOL_RIGHT);

    char theme_buf[16];
    snprintf(theme_buf, sizeof(theme_buf), "%s  %s",
             config_manager_get_dark_mode() ? "Dark" : "Light", LV_SYMBOL_RIGHT);

    make_nav_row(rows_body, "Theme", theme_buf, on_theme_row_click, &s_lbl_theme_val);
    make_nav_row(rows_body, "Screen Timeout", timeout_buf, on_timeout_row_click, &s_lbl_timeout_val);
    make_nav_row(rows_body, "Threshold (ppb)", disp_thr_buf, on_disp_threshold_row_click, &s_lbl_disp_thr_val);
    make_nav_row(rows_body, "TVOC High Threshold", disp_max_buf, on_disp_max_row_click, &s_lbl_disp_max_val);

    /* ── Alert Settings (collapsible, default expanded) ──────────────────────
     * TVOC Alert Threshold drives only sensor_manager's UI-level color
     * classification (warn tier has no independent alarm_manager concept —
     * see alarm_manager.h). High Alert Threshold is the real critical alarm
     * trigger — it updates both sensor_manager AND alarm_manager. */
    lv_obj_t *alert_card = lv_obj_create(content);
    lv_obj_set_size(alert_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(alert_card, IVF_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(alert_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(alert_card, IVF_CARD_RADIUS, 0);
    lv_obj_set_style_border_color(alert_card, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(alert_card, 1, 0);
    lv_obj_set_style_pad_all(alert_card, 0, 0);
    lv_obj_clear_flag(alert_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *alert_header = lv_obj_create(alert_card);
    lv_obj_set_size(alert_header, LV_PCT(100), ALERT_HEADER_H);
    lv_obj_set_pos(alert_header, 0, 0);
    lv_obj_set_style_bg_color(alert_header, lv_color_hex(0xFDECEA), 0);
    lv_obj_set_style_bg_opa(alert_header, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(alert_header, IVF_CARD_RADIUS, 0);
    lv_obj_set_style_border_width(alert_header, 0, 0);
    lv_obj_set_style_pad_all(alert_header, 0, 0);
    lv_obj_clear_flag(alert_header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(alert_header, on_alert_header_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bell = lv_label_create(alert_header);
    lv_label_set_text(bell, LV_SYMBOL_BELL);
    lv_obj_set_style_text_font(bell, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(bell, IVF_COLOR_DANGER, 0);
    lv_obj_align(bell, LV_ALIGN_LEFT_MID, MARGIN, 0);

    lv_obj_t *alert_title = lv_label_create(alert_header);
    lv_label_set_text(alert_title, "ALERT SETTINGS");
    lv_obj_set_style_text_font(alert_title, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(alert_title, IVF_COLOR_DANGER, 0);
    lv_obj_align(alert_title, LV_ALIGN_LEFT_MID, MARGIN + 20, 0);

    s_alert_chevron = lv_label_create(alert_header);
    lv_label_set_text(s_alert_chevron, LV_SYMBOL_UP);
    lv_obj_set_style_text_font(s_alert_chevron, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_alert_chevron, IVF_COLOR_DANGER, 0);
    lv_obj_align(s_alert_chevron, LV_ALIGN_RIGHT_MID, VALUE_RIGHT_OFS, 0);

    s_alert_content = lv_obj_create(alert_card);
    lv_obj_set_size(s_alert_content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_pos(s_alert_content, 0, ALERT_HEADER_H);
    lv_obj_set_style_bg_opa(s_alert_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_alert_content, 0, 0);
    lv_obj_set_style_pad_all(s_alert_content, 0, 0);
    lv_obj_set_style_radius(s_alert_content, 0, 0);
    lv_obj_clear_flag(s_alert_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_alert_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_alert_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    char voc_warn_buf[24], voc_alarm_buf[24];
    snprintf(voc_warn_buf, sizeof(voc_warn_buf), "%d ppb  %s",
             (int)config_manager_get_voc_warn_ppb(), LV_SYMBOL_RIGHT);
    snprintf(voc_alarm_buf, sizeof(voc_alarm_buf), "%d ppb  %s",
             (int)config_manager_get_voc_alarm_ppb(), LV_SYMBOL_RIGHT);

    make_alert_row(s_alert_content, "TVOC Alert Threshold", "Set warning threshold for TVOC level",
                   voc_warn_buf, on_voc_warn_row_click, &s_lbl_voc_warn_val);
    make_alert_row(s_alert_content, "High Alert Threshold", "Set critical threshold for TVOC level",
                   voc_alarm_buf, on_voc_alarm_row_click, &s_lbl_voc_alarm_val);

    /* alert_card is LV_SIZE_CONTENT-height, sized from its children's
     * bounding box (header + s_alert_content) — expanded by default to
     * match the design mockup. */

    /* ── Generic value-picker overlay (hidden until a row is tapped) ────────── */
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

    lv_obj_t *picker_panel = lv_obj_create(s_picker_backdrop);
    lv_obj_set_size(picker_panel, PICKER_PANEL_W, LV_SIZE_CONTENT);
    lv_obj_align(picker_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(picker_panel, IVF_COLOR_CARD, 0);
    lv_obj_set_style_radius(picker_panel, IVF_CARD_RADIUS, 0);
    lv_obj_set_style_border_color(picker_panel, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(picker_panel, 1, 0);
    lv_obj_set_style_pad_all(picker_panel, 8, 0);
    lv_obj_clear_flag(picker_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(picker_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(picker_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(picker_panel, 4, 0);

    s_picker_title = lv_label_create(picker_panel);
    lv_label_set_text(s_picker_title, "");
    lv_obj_set_style_text_font(s_picker_title, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_picker_title, IVF_COLOR_TEXT, 0);

    s_picker_list = lv_obj_create(picker_panel);
    lv_obj_set_size(s_picker_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_picker_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_picker_list, 0, 0);
    lv_obj_set_style_pad_all(s_picker_list, 0, 0);
    lv_obj_clear_flag(s_picker_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_picker_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_picker_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_picker_list, 4, 0);

    ESP_LOGD(TAG, "Settings screen created (Phase 6)");
    return s_scr;
}
