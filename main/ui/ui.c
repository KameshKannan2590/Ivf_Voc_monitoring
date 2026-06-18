#include "ui.h"
#include "lvgl_port/lvgl_port.h"

#include "screens/screen_splash.h"
#include "screens/screen_dashboard.h"
#include "screens/screen_chart.h"
#include "screens/screen_logs.h"
#include "screens/screen_settings.h"

#include "esp_log.h"
#include <stdint.h>

static const char *TAG = "ui";

static lv_obj_t   *s_screens[SCREEN_COUNT];
static screen_id_t s_current = SCREEN_SPLASH;

static lv_style_t s_style_screen;
static lv_style_t s_style_card;
static lv_style_t s_style_label_primary;
static lv_style_t s_style_label_muted;

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

/* ── Shared UI builders ─────────────────────────────────────── */

static void tab_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *cell = lv_event_get_target(e);
    screen_id_t target = (screen_id_t)(uintptr_t)lv_obj_get_user_data(cell);
    ui_goto_screen(target, true);
}

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

void ui_build_tab_bar(lv_obj_t *parent, screen_id_t active_tab)
{
    static const struct {
        const char *sym;
        const char *lbl;
        screen_id_t id;
    } TABS[4] = {
        { LV_SYMBOL_HOME,     "Home",     SCREEN_DASHBOARD },
        { LV_SYMBOL_LIST,     "Chart",    SCREEN_CHART     },
        { LV_SYMBOL_FILE,     "Logs",     SCREEN_LOGS      },
        { LV_SYMBOL_SETTINGS, "Settings", SCREEN_SETTINGS  },
    };

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, IVF_SCREEN_W, IVF_TAB_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, IVF_COLOR_NAV, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(bar, IVF_COLOR_BORDER, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < 4; i++) {
        bool active = (TABS[i].id == active_tab);
        lv_color_t col = active ? IVF_COLOR_TAB_ACTIVE : IVF_COLOR_TAB_INACTIVE;

        lv_obj_t *cell = lv_obj_create(bar);
        lv_obj_set_size(cell, 68, IVF_TAB_H);
        lv_obj_set_style_bg_color(cell, IVF_COLOR_NAV, 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_set_style_pad_row(cell, 2, 0);     /* gap between icon and label */
        lv_obj_set_style_border_width(cell, active ? 3 : 0, 0);
        lv_obj_set_style_border_side(cell, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(cell, IVF_COLOR_TAB_ACTIVE, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_layout(cell, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *sym_lbl = lv_label_create(cell);
        lv_label_set_text(sym_lbl, TABS[i].sym);
        lv_obj_set_style_text_color(sym_lbl, col, 0);
        lv_obj_set_style_text_font(sym_lbl, IVF_FONT_NORMAL, 0);

        lv_obj_t *txt_lbl = lv_label_create(cell);
        lv_label_set_text(txt_lbl, TABS[i].lbl);
        lv_obj_set_style_text_color(txt_lbl, col, 0);
        lv_obj_set_style_text_font(txt_lbl, IVF_FONT_SMALL, 0);

        lv_obj_set_user_data(cell, (void *)(uintptr_t)TABS[i].id);
        lv_obj_add_event_cb(cell, tab_btn_event_cb, LV_EVENT_CLICKED, NULL);
    }
}

/* ── Navigation ─────────────────────────────────────────────── */

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

    lvgl_port_unlock();

    ESP_LOGI(TAG, "UI ready");
}

void ui_goto_screen(screen_id_t target, bool forward)
{
    (void)forward;
    if (target >= SCREEN_COUNT) return;

    lv_scr_load_anim(s_screens[target], LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
    s_current = target;

    if (target == SCREEN_CHART) screen_chart_refresh();
    if (target == SCREEN_LOGS)  screen_logs_refresh();

    ESP_LOGD(TAG, "-> screen %d", target);
}

void ui_dashboard_refresh(void)
{
    if (lvgl_port_lock(50)) {
        screen_dashboard_update();
        lvgl_port_unlock();
    }
}
