#include "navigation_drawer.h"
#include "icon_button.h"
#include "assets.h"
#include "ui.h"           /* IVF_* constants */
#include <stdlib.h>
#include <string.h>

/* =======================================================================
 * navigation_drawer.c — Standalone slide-in navigation drawer component
 *
 * Topology (all on lv_layer_top()):
 *   [backdrop]  — semi-transparent full-screen overlay, click-to-close
 *   [drawer]    — 200 px panel sliding from left
 *     [header]  — app name / close button area
 *     [items]   — rows of icon + label buttons
 *   [fab_btn]   — floating [≡] button (via icon_button component)
 *
 * Animation mirrors nav_drawer.c: reads current x before starting so that
 * mid-animation reversals start from the actual animated position.
 * ======================================================================= */

/* ── Geometry ────────────────────────────────────────────────────────────── */

#define ITEM_H            52
#define ITEM_ICON_W       28
#define ITEM_PAD          12
#define DRAWER_HEADER_H   148  /* top section: circle(68)+badge(18)+title(20)+pill(20)+gaps+divider */

/* ── Handle definition ───────────────────────────────────────────────────── */

struct navigation_drawer {
    nav_drawer_cfg_t   cfg;
    nav_drawer_item_t  items_copy[NAV_DRAWER_MAX_ITEMS];

    icon_button_t     *fab;          /* floating [≡] button            */
    lv_obj_t          *backdrop;     /* semi-transparent full-screen    */
    lv_obj_t          *drawer;       /* sliding panel on lv_layer_top() */
    lv_obj_t          *item_rows[NAV_DRAWER_MAX_ITEMS];  /* row objects  */
    void              *item_ctxs[NAV_DRAWER_MAX_ITEMS];  /* malloc'd item_ctx_t* per row */

    uint8_t            item_count;
    uint8_t            active_id;
    bool               is_open;
};

/* ── Animation ──────────────────────────────────────────────────────────── */

#if 0
/* Animated slide (120ms, then 220ms before that) — replaced with an
 * instant jump per request ("why can't it just appear so animation
 * won't lag"), since every frame is a full-screen redraw
 * (lvgl_port.c full_refresh=1) and even 120ms felt like lag on real
 * hardware. Kept here, not deleted, in case animation is wanted back. */
static void drawer_x_exec_cb(void *var, int32_t val)
{
    lv_obj_set_x((lv_obj_t *)var, (lv_coord_t)val);
}

static void drawer_anim_done_cb(lv_anim_t *a)
{
    lv_obj_t *drawer = (lv_obj_t *)a->var;
    /* If we animated to the closed position, hide the backdrop */
    if (lv_obj_get_x(drawer) < 0) {
        lv_obj_t *layer = lv_layer_top();
        /* Find the backdrop sibling — it is the first child of lv_layer_top */
        uint32_t child_cnt = lv_obj_get_child_cnt(layer);
        for (uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t *child = lv_obj_get_child(layer, (int32_t)i);
            /* The backdrop has full screen size and semi-transparent bg */
            if (lv_obj_get_width(child) == IVF_SCREEN_W &&
                lv_obj_get_height(child) == IVF_SCREEN_H) {
                lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
                break;
            }
        }
    }
}

static void slide(struct navigation_drawer *d, bool open)
{
    lv_coord_t target_x = open ? 0 : -(lv_coord_t)d->cfg.drawer_width;
    lv_coord_t from_x   = lv_obj_get_x(d->drawer);

    if (from_x == target_x) return;

    if (open) {
        lv_obj_clear_flag(d->backdrop, LV_OBJ_FLAG_HIDDEN);
    }

    lv_anim_del(d->drawer, drawer_x_exec_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, d->drawer);
    lv_anim_set_exec_cb(&a, drawer_x_exec_cb);
    lv_anim_set_values(&a, from_x, target_x);
    lv_anim_set_time(&a, 120);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, drawer_anim_done_cb);
    lv_anim_start(&a);

    d->is_open = open;
}
#endif

static void slide(struct navigation_drawer *d, bool open)
{
    lv_coord_t target_x = open ? 0 : -(lv_coord_t)d->cfg.drawer_width;

    if (lv_obj_get_x(d->drawer) == target_x) return;

    if (open) {
        lv_obj_clear_flag(d->backdrop, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_set_x(d->drawer, target_x);

    if (!open) {
        lv_obj_add_flag(d->backdrop, LV_OBJ_FLAG_HIDDEN);
    }

    d->is_open = open;
}

/* ── Item click handler ─────────────────────────────────────────────────── */

typedef struct {
    struct navigation_drawer *d;
    uint8_t                   id;
} item_ctx_t;

static void item_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    item_ctx_t *ctx = (item_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;

    struct navigation_drawer *d = ctx->d;
    navigation_drawer_close(d);
    navigation_drawer_set_active(d, ctx->id);

    if (d->cfg.on_navigate) {
        d->cfg.on_navigate(ctx->id, d->cfg.user_data);
    }
}

/* ── FAB click handler ──────────────────────────────────────────────────── */

static void fab_click_cb(void *user_data)
{
    struct navigation_drawer *d = (struct navigation_drawer *)user_data;
    navigation_drawer_toggle(d);
}

/* ── Backdrop click handler ─────────────────────────────────────────────── */

static void backdrop_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    struct navigation_drawer *d =
        (struct navigation_drawer *)lv_event_get_user_data(e);
    navigation_drawer_close(d);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

navigation_drawer_t *navigation_drawer_create(const nav_drawer_cfg_t *cfg)
{
    if (!cfg || !cfg->items || cfg->item_count == 0 || !cfg->on_navigate)
        return NULL;

    navigation_drawer_t *d =
        (navigation_drawer_t *)malloc(sizeof(*d));
    if (!d) return NULL;

    memset(d, 0, sizeof(*d));
    d->cfg        = *cfg;
    d->item_count = cfg->item_count < NAV_DRAWER_MAX_ITEMS
                    ? cfg->item_count : NAV_DRAWER_MAX_ITEMS;

    /* Copy item descriptors so caller's array need not be permanent */
    for (int i = 0; i < d->item_count; i++) {
        d->items_copy[i] = cfg->items[i];
    }
    d->cfg.items = d->items_copy;

    if (d->cfg.drawer_width <= 0) d->cfg.drawer_width = IVF_DRAWER_W;

    lv_obj_t *layer = lv_layer_top();

    /* ── Backdrop ────────────────────────────────────────────────────── */
    lv_obj_t *backdrop = lv_obj_create(layer);
    lv_obj_set_size(backdrop, IVF_SCREEN_W, IVF_SCREEN_H);
    lv_obj_set_pos(backdrop, 0, 0);
    lv_obj_set_style_bg_color(backdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_set_style_radius(backdrop, 0, 0);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(backdrop, backdrop_event_cb, LV_EVENT_CLICKED, d);
    d->backdrop = backdrop;

    /* ── Drawer panel ────────────────────────────────────────────────── */
    lv_coord_t dw = d->cfg.drawer_width;
    lv_coord_t dh = IVF_SCREEN_H;   /* full screen height — covers header */

    lv_obj_t *drawer = lv_obj_create(layer);
    lv_obj_set_size(drawer, dw, dh);
    lv_obj_set_pos(drawer, -dw, 0);   /* start off-screen, full height from y=0 */
    lv_obj_set_style_bg_color(drawer, IVF_COLOR_NAV, 0);
    lv_obj_set_style_bg_opa(drawer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(drawer, 0, 0);
    lv_obj_set_style_radius(drawer, 0, 0);
    lv_obj_set_style_pad_all(drawer, 0, 0);
    lv_obj_set_style_shadow_width(drawer, 16, 0);
    lv_obj_set_style_shadow_ofs_x(drawer, 4, 0);
    lv_obj_set_style_shadow_color(drawer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(drawer, LV_OPA_20, 0);
    lv_obj_clear_flag(drawer, LV_OBJ_FLAG_SCROLLABLE);
    d->drawer = drawer;

    /* ── Drawer top section ────────────────────────────────────────── */
    {
        /* Blue circle background (56×56, centered in drawer width) */
        const lv_coord_t CIRC_D = 56;
        const lv_coord_t CIRC_Y = 12;
        const lv_coord_t CIRC_X = (dw - CIRC_D) / 2;   /* = 72 */

        lv_obj_t *circle = lv_obj_create(drawer);
        lv_obj_set_size(circle, CIRC_D, CIRC_D);
        lv_obj_set_pos(circle, CIRC_X, CIRC_Y);
        lv_obj_set_style_bg_color(circle, IVF_COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(circle, 0, 0);
        lv_obj_set_style_pad_all(circle, 0, 0);
        lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        /* Shield icon centered in circle */
        lv_obj_t *shield_img = lv_img_create(circle);
        lv_img_set_src(shield_img, &shield);
        lv_obj_center(shield_img);

        /* "36 × 36" size badge — fixed-width pill below circle */
        const lv_coord_t BADGE_Y = CIRC_Y + CIRC_D + 6;   /* = 74 */
        const lv_coord_t BADGE_W = 68;
        lv_obj_t *size_badge = lv_obj_create(drawer);
        lv_obj_set_size(size_badge, BADGE_W, 18);
        lv_obj_set_pos(size_badge, (dw - BADGE_W) / 2, BADGE_Y);
        lv_obj_set_style_bg_color(size_badge, lv_color_hex(0xEEEEEE), 0);
        lv_obj_set_style_bg_opa(size_badge, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(size_badge, 4, 0);
        lv_obj_set_style_border_width(size_badge, 0, 0);
        lv_obj_set_style_pad_all(size_badge, 0, 0);
        lv_obj_clear_flag(size_badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *size_lbl = lv_label_create(size_badge);
        lv_label_set_text(size_lbl, "IVF EMS");
        lv_obj_set_style_text_font(size_lbl, IVF_FONT_SMALL, 0);
        lv_obj_set_style_text_color(size_lbl, IVF_COLOR_TEXT_MUTED, 0);
        lv_obj_align(size_lbl, LV_ALIGN_CENTER, 0, 0);

        /* "Environmental Monitor" title — full-width centered text */
        const lv_coord_t TITLE_Y = BADGE_Y + 18 + 5;   /* = 97 */
        lv_obj_t *title_lbl = lv_label_create(drawer);
        lv_label_set_text(title_lbl,
                          d->cfg.header_title ? d->cfg.header_title : "");
        lv_obj_set_size(title_lbl, dw, LV_SIZE_CONTENT);
        lv_obj_set_pos(title_lbl, 0, TITLE_Y);
        lv_obj_set_style_text_font(title_lbl, IVF_FONT_NORMAL, 0);
        lv_obj_set_style_text_color(title_lbl, IVF_COLOR_TEXT, 0);
        lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_CENTER, 0);

        /* "Normal" status pill — fixed-width centered */
        const lv_coord_t PILL_Y  = TITLE_Y + 22 + 4;   /* = 123 */
        const lv_coord_t PILL_W  = 64;
        lv_obj_t *pill = lv_obj_create(drawer);
        lv_obj_set_size(pill, PILL_W, 20);
        lv_obj_set_pos(pill, (dw - PILL_W) / 2, PILL_Y);
        lv_obj_set_style_bg_color(pill, lv_color_hex(0x43A047), 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(pill, 0, 0);
        lv_obj_set_style_pad_all(pill, 0, 0);
        lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *status_lbl = lv_label_create(pill);
        lv_label_set_text(status_lbl,
                          d->cfg.header_status ? d->cfg.header_status : "");
        lv_obj_set_style_text_font(status_lbl, IVF_FONT_SMALL, 0);
        lv_obj_set_style_text_color(status_lbl, lv_color_white(), 0);
        lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, 0);

        /* Divider between top section and menu items */
        lv_obj_t *drw_div = lv_obj_create(drawer);
        lv_obj_set_size(drw_div, dw, 1);
        lv_obj_set_pos(drw_div, 0, DRAWER_HEADER_H - 4);
        lv_obj_set_style_bg_color(drw_div, IVF_COLOR_BORDER, 0);
        lv_obj_set_style_bg_opa(drw_div, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(drw_div, 0, 0);
        lv_obj_set_style_radius(drw_div, 0, 0);
        lv_obj_clear_flag(drw_div, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    /* ── Item rows ───────────────────────────────────────────────────── */
    for (int i = 0; i < d->item_count; i++) {
        const nav_drawer_item_t *item = &d->items_copy[i];

        lv_obj_t *row = lv_btn_create(drawer);
        lv_obj_set_size(row, dw, ITEM_H);
        lv_obj_set_pos(row, 0, DRAWER_HEADER_H + i * ITEM_H);
        lv_obj_set_style_bg_color(row, IVF_COLOR_NAV, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, IVF_COLOR_BORDER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Icon — PNG asset if available, otherwise fall back to symbol label */
        lv_obj_t *icon;
        if (item->img_src) {
            icon = lv_img_create(row);
            lv_img_set_src(icon, item->img_src);
            lv_obj_set_style_img_recolor(icon, IVF_COLOR_NAV_INACTIVE, 0);
            lv_obj_set_style_img_recolor_opa(icon, LV_OPA_COVER, 0);
            lv_obj_set_pos(icon, ITEM_PAD, (ITEM_H - 18) / 2);
        } else {
            icon = lv_label_create(row);
            lv_obj_set_style_text_font(icon, IVF_FONT_NORMAL, 0);
            lv_obj_set_style_text_color(icon, IVF_COLOR_NAV_INACTIVE, 0);
            lv_label_set_text(icon, item->symbol ? item->symbol : "");
            lv_obj_set_pos(icon, ITEM_PAD, (ITEM_H - 20) / 2);
        }

        /* Text label */
        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, IVF_FONT_NORMAL, 0);
        lv_obj_set_style_text_color(lbl, IVF_COLOR_TEXT, 0);
        lv_label_set_text(lbl, item->label ? item->label : "");
        lv_obj_set_pos(lbl, ITEM_PAD + ITEM_ICON_W + 8, (ITEM_H - 20) / 2);

        /* Bottom separator */
        lv_obj_t *sep = lv_obj_create(drawer);
        lv_obj_set_size(sep, dw, 1);
        lv_obj_set_pos(sep, 0, DRAWER_HEADER_H + (i + 1) * ITEM_H - 1);
        lv_obj_set_style_bg_color(sep, IVF_COLOR_BORDER, 0);
        lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(sep, 0, 0);
        lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        /* Allocate per-item context (lives until drawer is destroyed) */
        item_ctx_t *ctx = (item_ctx_t *)malloc(sizeof(*ctx));
        if (ctx) {
            ctx->d  = d;
            ctx->id = item->id;
            lv_obj_add_event_cb(row, item_event_cb, LV_EVENT_CLICKED, ctx);
        }

        d->item_rows[i] = row;
        d->item_ctxs[i] = ctx;   /* track for cleanup — avoids LVGL event introspection */
    }

    /* ── Version footer ─────────────────────────────────────────────── */
    if (d->cfg.footer_version) {
        lv_obj_t *ver_lbl = lv_label_create(drawer);
        lv_label_set_text(ver_lbl, d->cfg.footer_version);
        lv_obj_set_size(ver_lbl, dw, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(ver_lbl, IVF_FONT_SMALL, 0);
        lv_obj_set_style_text_color(ver_lbl, IVF_COLOR_TEXT_MUTED, 0);
        lv_obj_set_style_text_align(ver_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(ver_lbl, LV_ALIGN_BOTTOM_MID, 0, -IVF_PAD);
    }

    /* ── Floating action button ──────────────────────────────────────── */
    if (d->cfg.create_fab) {
        icon_button_cfg_t fab_cfg = {
            .size         = IVF_NAV_BTN_SIZE,
            .x            = IVF_PAD,
            .y            = IVF_SCREEN_H - IVF_NAV_BTN_SIZE - IVF_PAD,
            .bg_color     = IVF_COLOR_PRIMARY,
            .symbol       = LV_SYMBOL_LIST,
            .font         = IVF_FONT_NORMAL,
            .icon_color   = lv_color_white(),
            .circular     = true,
            .shadow_width = 10,
        };
        d->fab = icon_button_create(layer, &fab_cfg);
        icon_button_set_click_cb(d->fab, fab_click_cb, d);
    }

    return d;
}

void navigation_drawer_open(navigation_drawer_t *drawer)
{
    if (!drawer || drawer->is_open) return;
    slide(drawer, true);
}

void navigation_drawer_close(navigation_drawer_t *drawer)
{
    if (!drawer || !drawer->is_open) return;
    slide(drawer, false);
}

void navigation_drawer_toggle(navigation_drawer_t *drawer)
{
    if (!drawer) return;
    slide(drawer, !drawer->is_open);
}

void navigation_drawer_set_active(navigation_drawer_t *drawer, uint8_t item_id)
{
    if (!drawer) return;
    drawer->active_id = item_id;

    for (int i = 0; i < drawer->item_count; i++) {
        if (!drawer->item_rows[i]) continue;
        bool       active = (drawer->items_copy[i].id == item_id);
        lv_color_t bg     = active ? IVF_COLOR_NAV_ACTIVE : IVF_COLOR_NAV;
        lv_color_t col    = active ? IVF_COLOR_PRIMARY    : IVF_COLOR_TEXT;
        lv_opa_t   opa    = active ? LV_OPA_COVER         : LV_OPA_TRANSP;
        lv_obj_set_style_bg_color(drawer->item_rows[i], bg, 0);
        lv_obj_set_style_bg_opa(drawer->item_rows[i], opa, 0);
        lv_obj_t *icon_obj = lv_obj_get_child(drawer->item_rows[i], 0);
        lv_obj_t *text_lbl = lv_obj_get_child(drawer->item_rows[i], 1);
        if (icon_obj) {
            if (lv_obj_check_type(icon_obj, &lv_img_class)) {
                lv_obj_set_style_img_recolor(icon_obj, col, 0);
                lv_obj_set_style_img_recolor_opa(icon_obj, LV_OPA_COVER, 0);
            } else {
                lv_obj_set_style_text_color(icon_obj, col, 0);
            }
        }
        if (text_lbl) lv_obj_set_style_text_color(text_lbl, col, 0);
    }
}

bool navigation_drawer_is_open(const navigation_drawer_t *drawer)
{
    return drawer ? drawer->is_open : false;
}

void navigation_drawer_destroy(navigation_drawer_t *drawer)
{
    if (!drawer) return;

    /* Free per-item event contexts (tracked in item_ctxs — no LVGL introspection needed) */
    for (int i = 0; i < drawer->item_count; i++) {
        free(drawer->item_ctxs[i]);
        drawer->item_ctxs[i] = NULL;
    }

    if (drawer->fab) {
        lv_obj_del(icon_button_get_obj(drawer->fab));
        icon_button_destroy(drawer->fab);
    }
    if (drawer->backdrop) lv_obj_del(drawer->backdrop);
    if (drawer->drawer)   lv_obj_del(drawer->drawer);

    free(drawer);
}
