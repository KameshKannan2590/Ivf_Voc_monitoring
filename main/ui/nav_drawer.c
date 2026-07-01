#include "nav_drawer.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "nav_drawer";

#define DRAWER_ANIM_MS  220

/* ── Internal handles ────────────────────────────────────────────────────── */
static lv_obj_t   *s_btn_menu  = NULL;
static lv_obj_t   *s_backdrop  = NULL;
static lv_obj_t   *s_drawer    = NULL;
static lv_obj_t   *s_items[4]  = {NULL, NULL, NULL, NULL};

static screen_id_t s_active    = SCREEN_DASHBOARD;
static bool        s_open      = false;

/* ── Local colors ─────────────────────────────────────────────────────────── */
#define DRAWER_BG       lv_color_hex(0xF8F9FA)
#define DRAWER_ACTIVE   lv_color_hex(0xE8F0FE)
#define DRAWER_TEXT     lv_color_hex(0x212121)
#define ICON_ACTIVE_COL lv_color_hex(0x1A73E8)
#define BTN_COL         lv_color_hex(0x1A73E8)

/* ── Nav items table ──────────────────────────────────────────────────────── */
static const struct {
    const char *sym;
    const char *label;
    screen_id_t screen;
} ITEMS[4] = {
    { LV_SYMBOL_HOME,     "Dashboard", SCREEN_DASHBOARD },
    { LV_SYMBOL_LIST,     "Chart",     SCREEN_CHART     },
    { LV_SYMBOL_FILE,     "Logs",      SCREEN_LOGS      },
    { LV_SYMBOL_SETTINGS, "Settings",  SCREEN_SETTINGS  },
};

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static void update_highlight(void)
{
    for (int i = 0; i < 4; i++) {
        if (!s_items[i]) continue;
        bool active = (ITEMS[i].screen == s_active);

        lv_obj_set_style_bg_color(s_items[i],
                                  active ? DRAWER_ACTIVE : DRAWER_BG, 0);
        lv_obj_set_style_bg_opa(s_items[i],
                                active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

        lv_obj_t *icon = lv_obj_get_child(s_items[i], 0);
        lv_obj_t *lbl  = lv_obj_get_child(s_items[i], 1);
        lv_color_t col = active ? ICON_ACTIVE_COL : DRAWER_TEXT;
        if (icon) lv_obj_set_style_text_color(icon, col, 0);
        if (lbl)  lv_obj_set_style_text_color(lbl,  col, 0);
    }
}

/* ── Animation ────────────────────────────────────────────────────────────── */

static void drawer_x_exec_cb(void *var, int32_t val)
{
    lv_obj_set_x((lv_obj_t *)var, (lv_coord_t)val);
}

static void drawer_anim_done_cb(lv_anim_t *a)
{
    (void)a;
    if (!s_open && s_backdrop)
        lv_obj_add_flag(s_backdrop, LV_OBJ_FLAG_HIDDEN);
}

static void slide(bool open)
{
    if (!s_drawer || !s_backdrop) return;

    lv_coord_t from_x = lv_obj_get_x(s_drawer);   /* current position */
    lv_coord_t to_x   = open ? 0 : -IVF_DRAWER_W;

    if (open) {
        lv_obj_clear_flag(s_backdrop, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_backdrop);
        lv_obj_move_foreground(s_drawer);
        lv_obj_move_foreground(s_btn_menu);
    }

    /* Cancel any in-progress slide before starting the reverse */
    lv_anim_del(s_drawer, drawer_x_exec_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_drawer);
    lv_anim_set_exec_cb(&a, drawer_x_exec_cb);
    lv_anim_set_values(&a, from_x, to_x);
    lv_anim_set_time(&a, DRAWER_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, drawer_anim_done_cb);
    lv_anim_start(&a);
}

/* ── Event callbacks ──────────────────────────────────────────────────────── */

static void menu_btn_cb(lv_event_t *e)
{
    (void)e;
    nav_drawer_toggle();
}

static void backdrop_cb(lv_event_t *e)
{
    (void)e;
    nav_drawer_close();
}

static void item_cb(lv_event_t *e)
{
    lv_obj_t *obj    = lv_event_get_target(e);
    screen_id_t dest = (screen_id_t)(uintptr_t)lv_obj_get_user_data(obj);
    ui_goto_screen(dest, true);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void nav_drawer_init(void)
{
    lv_obj_t *layer = lv_layer_top();

    /* ── Backdrop (click-outside area) ──────────────────────────────────── */
    s_backdrop = lv_obj_create(layer);
    lv_obj_set_size(s_backdrop, IVF_SCREEN_W, IVF_SCREEN_H);
    lv_obj_set_pos(s_backdrop, 0, 0);
    lv_obj_set_style_bg_color(s_backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_backdrop, LV_OPA_30, 0);
    lv_obj_set_style_border_width(s_backdrop, 0, 0);
    lv_obj_set_style_radius(s_backdrop, 0, 0);
    lv_obj_clear_flag(s_backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_backdrop, backdrop_cb, LV_EVENT_CLICKED, NULL);

    /* ── Drawer panel ────────────────────────────────────────────────────── */
    s_drawer = lv_obj_create(layer);
    lv_obj_set_pos(s_drawer, -IVF_DRAWER_W, IVF_HEADER_H);
    lv_obj_set_size(s_drawer, IVF_DRAWER_W, IVF_SCREEN_H - IVF_HEADER_H);
    lv_obj_set_style_bg_color(s_drawer, DRAWER_BG, 0);
    lv_obj_set_style_bg_opa(s_drawer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_drawer, 0, 0);
    lv_obj_set_style_border_width(s_drawer, 1, 0);
    lv_obj_set_style_border_side(s_drawer, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(s_drawer, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(s_drawer, 0, 0);
    lv_obj_clear_flag(s_drawer, LV_OBJ_FLAG_SCROLLABLE);

    /* "NAVIGATE" label */
    lv_obj_t *title = lv_label_create(s_drawer);
    lv_label_set_text(title, "NAVIGATE");
    lv_obj_set_style_text_font(title, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(title, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(title, IVF_PAD, 10);

    /* Divider */
    lv_obj_t *div = lv_obj_create(s_drawer);
    lv_obj_set_size(div, IVF_DRAWER_W, 1);
    lv_obj_set_pos(div, 0, 30);
    lv_obj_set_style_bg_color(div, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);
    lv_obj_clear_flag(div, LV_OBJ_FLAG_SCROLLABLE);

    /* Nav items */
    for (int i = 0; i < 4; i++) {
        lv_obj_t *item = lv_obj_create(s_drawer);
        s_items[i] = item;
        lv_obj_set_size(item, IVF_DRAWER_W - 8, 50);
        lv_obj_set_pos(item, 4, 38 + i * 56);
        lv_obj_set_style_bg_color(item, DRAWER_BG, 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(item, IVF_CARD_RADIUS, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_left(item, 12, 0);
        lv_obj_set_style_pad_ver(item, 0, 0);
        lv_obj_set_style_pad_column(item, 12, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_layout(item, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        lv_obj_t *icon = lv_label_create(item);
        lv_label_set_text(icon, ITEMS[i].sym);
        lv_obj_set_style_text_font(icon, IVF_FONT_NORMAL, 0);
        lv_obj_set_style_text_color(icon, DRAWER_TEXT, 0);

        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, ITEMS[i].label);
        lv_obj_set_style_text_font(lbl, IVF_FONT_NORMAL, 0);
        lv_obj_set_style_text_color(lbl, DRAWER_TEXT, 0);

        lv_obj_set_user_data(item, (void *)(uintptr_t)ITEMS[i].screen);
        lv_obj_add_event_cb(item, item_cb, LV_EVENT_CLICKED, NULL);
    }

    /* ── Floating ≡ menu button ──────────────────────────────────────────── */
    s_btn_menu = lv_btn_create(layer);
    lv_obj_set_size(s_btn_menu, IVF_NAV_BTN_SIZE, IVF_NAV_BTN_SIZE);
    lv_obj_set_pos(s_btn_menu, 8, IVF_SCREEN_H - IVF_NAV_BTN_SIZE - 8);
    lv_obj_set_style_bg_color(s_btn_menu, BTN_COL, 0);
    lv_obj_set_style_bg_opa(s_btn_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_btn_menu, IVF_NAV_BTN_SIZE / 2, 0);
    lv_obj_set_style_border_width(s_btn_menu, 0, 0);
    lv_obj_set_style_pad_all(s_btn_menu, 0, 0);
    lv_obj_set_style_shadow_width(s_btn_menu, 10, 0);
    lv_obj_set_style_shadow_ofs_y(s_btn_menu, 4, 0);
    lv_obj_set_style_shadow_color(s_btn_menu, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(s_btn_menu, LV_OPA_30, 0);
    lv_obj_add_event_cb(s_btn_menu, menu_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *menu_sym = lv_label_create(s_btn_menu);
    lv_label_set_text(menu_sym, LV_SYMBOL_LIST);
    lv_obj_set_style_text_font(menu_sym, IVF_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(menu_sym, lv_color_white(), 0);
    lv_obj_center(menu_sym);

    update_highlight();
    ESP_LOGD(TAG, "initialized");
}

void nav_drawer_open(void)
{
    if (s_open) return;
    s_open = true;
    slide(true);
}

void nav_drawer_close(void)
{
    if (!s_open) return;
    s_open = false;
    slide(false);
}

void nav_drawer_toggle(void)
{
    if (s_open) nav_drawer_close();
    else        nav_drawer_open();
}

void nav_drawer_set_active(screen_id_t screen)
{
    s_active = screen;
    update_highlight();
}

bool nav_drawer_is_open(void)
{
    return s_open;
}
