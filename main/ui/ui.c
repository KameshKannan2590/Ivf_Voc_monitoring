#include "ui.h"
#include "assets.h"
#include "components/navigation_drawer/navigation_drawer.h"
#include "lvgl_port/lvgl_port.h"

#include "screens/screen_splash.h"
#include "screens/screen_dashboard.h"
#include "screens/screen_chart.h"
#include "screens/screen_logs.h"
#include "screens/screen_settings.h"

#include "esp_log.h"
#include "esp_timer.h"
#include <stdint.h>
#include <stdio.h>

static const char *TAG = "ui";

static lv_obj_t            *s_screens[SCREEN_COUNT];
static screen_id_t          s_current    = SCREEN_SPLASH;
static navigation_drawer_t *s_nav_drawer = NULL;
static lv_timer_t          *s_dash_timer = NULL;

static lv_style_t s_style_screen;
static lv_style_t s_style_card;
static lv_style_t s_style_label_primary;
static lv_style_t s_style_label_muted;

/* ── Navigation drawer items ──────────────────────────────────── */

static const nav_drawer_item_t APP_NAV_ITEMS[] = {
    { NULL, "Dashboard",  SCREEN_DASHBOARD, &material_symbols_dashboard_rounded },
    { NULL, "TVOC Chart", SCREEN_CHART,     &solar_chart_bold_duotone           },
    { NULL, "Data Logs",  SCREEN_LOGS,      &icon_park_solid_log                },
    { NULL, "Settings",   SCREEN_SETTINGS,  &ant_design_setting_filled          },
};

static void on_nav_item_selected(uint8_t id, void *user_data)
{
    (void)user_data;
    ui_goto_screen((screen_id_t)id, true);
}

/* ── Shared styles ────────────────────────────────────────────── */

static void build_shared_styles(void)
{
    lv_style_init(&s_style_screen);
    lv_style_set_bg_color(&s_style_screen, IVF_COLOR_BG);
    lv_style_set_bg_opa(&s_style_screen, LV_OPA_COVER);
    lv_style_set_border_width(&s_style_screen, 0);
    lv_style_set_pad_all(&s_style_screen, 0);

    lv_style_init(&s_style_card);
    lv_style_set_bg_color(&s_style_card, IVF_COLOR_CARD);
    lv_style_set_bg_opa(&s_style_card, LV_OPA_COVER);
    lv_style_set_border_color(&s_style_card, IVF_COLOR_BORDER);
    lv_style_set_border_width(&s_style_card, 1);
    lv_style_set_radius(&s_style_card, IVF_CARD_RADIUS);
    lv_style_set_pad_all(&s_style_card, IVF_PAD);

    lv_style_init(&s_style_label_primary);
    lv_style_set_text_color(&s_style_label_primary, IVF_COLOR_TEXT);
    lv_style_set_text_font(&s_style_label_primary, IVF_FONT_NORMAL);

    lv_style_init(&s_style_label_muted);
    lv_style_set_text_color(&s_style_label_muted, IVF_COLOR_TEXT_MUTED);
    lv_style_set_text_font(&s_style_label_muted, IVF_FONT_SMALL);
}

/* ── Shared UI builders ─────────────────────────────────────────── */

lv_obj_t *ui_build_header(lv_obj_t *parent, const char *title)
{
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, IVF_SCREEN_W, IVF_HEADER_H);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, IVF_COLOR_NAV, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, IVF_COLOR_BORDER, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(hdr);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, IVF_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(lbl, IVF_COLOR_TEXT, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    return hdr;
}

/* ── Dashboard refresh timer (runs inside lv_timer_handler — no mutex needed) */

static void dash_timer_cb(lv_timer_t *t)
{
    (void)t;

    screen_dashboard_update();

    /* Elapsed-time clock (boot-relative until RTC/SNTP is wired up) */
    int64_t us   = esp_timer_get_time();
    int32_t secs = (int32_t)(us / 1000000LL);
    int32_t h    = (secs / 3600) % 24;
    int32_t m    = (secs / 60) % 60;

    char time_buf[12];
    int32_t h12 = h % 12;
    if (h12 == 0) h12 = 12;
    snprintf(time_buf, sizeof(time_buf), "%02"PRId32":%02"PRId32" %s",
             h12, m, h >= 12 ? "PM" : "AM");
    dashboard_set_time(time_buf);
}

/* ── Navigation ─────────────────────────────────────────────────── */

void ui_init(void)
{
    ESP_LOGI(TAG, "Building UI");

    lvgl_port_lock(0);

    build_shared_styles();

    s_screens[SCREEN_SPLASH]    = screen_splash_create();
    s_screens[SCREEN_DASHBOARD] = screen_dashboard_create();
    s_screens[SCREEN_CHART]     = screen_chart_create();
    s_screens[SCREEN_LOGS]      = screen_logs_create();
    s_screens[SCREEN_SETTINGS]  = screen_settings_create();

    lv_disp_load_scr(s_screens[SCREEN_SPLASH]);
    s_current = SCREEN_SPLASH;
    screen_splash_start();

    nav_drawer_cfg_t nav_cfg = {
        .items          = APP_NAV_ITEMS,
        .item_count     = 4,
        .drawer_width   = IVF_DRAWER_W,
        .on_navigate    = on_nav_item_selected,
        .user_data      = NULL,
        .create_fab     = false,
        .header_title   = "Environmental Monitor",
        .header_status  = "Normal",
        .footer_version = "Version v1.2.0",
    };
    s_nav_drawer = navigation_drawer_create(&nav_cfg);
    navigation_drawer_set_active(s_nav_drawer, SCREEN_DASHBOARD);

    /* 1 Hz sensor + clock refresh — fires inside lv_timer_handler, no mutex needed */
    s_dash_timer = lv_timer_create(dash_timer_cb, 1000, NULL);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "UI ready");
}

void ui_goto_screen(screen_id_t target, bool forward)
{
    (void)forward;
    if (target >= SCREEN_COUNT) return;

    navigation_drawer_close(s_nav_drawer);
    navigation_drawer_set_active(s_nav_drawer, target);
    lv_scr_load_anim(s_screens[target], LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    s_current = target;

    if (target == SCREEN_CHART) screen_chart_refresh();
    if (target == SCREEN_LOGS)  screen_logs_refresh();

    ESP_LOGD(TAG, "-> screen %d", target);
}

void ui_nav_drawer_toggle(void)
{
    navigation_drawer_toggle(s_nav_drawer);
}

