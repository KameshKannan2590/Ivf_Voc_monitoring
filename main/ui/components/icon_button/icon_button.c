#include "icon_button.h"
#include "ui.h"    /* IVF_FONT_NORMAL */
#include <stdlib.h>

/* =======================================================================
 * icon_button.c — Floating circular icon button implementation
 * ======================================================================= */

/* ── Handle definition ──────────────────────────────────────────────────── */

struct icon_button {
    lv_obj_t        *btn;          /* the LVGL button object             */
    lv_obj_t        *label;        /* symbol label centred on the button */
    icon_button_cb_t cb;
    void            *user_data;
};

/* ── Internal LVGL event handler ────────────────────────────────────────── */

static void btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    icon_button_t *self = (icon_button_t *)lv_event_get_user_data(e);
    if (self && self->cb) self->cb(self->user_data);
}

/* ── Creation ───────────────────────────────────────────────────────────── */

icon_button_t *icon_button_create(lv_obj_t *parent,
                                   const icon_button_cfg_t *cfg)
{
    if (!cfg) return NULL;

    icon_button_t *self = (icon_button_t *)malloc(sizeof(*self));
    if (!self) return NULL;
    self->cb        = NULL;
    self->user_data = NULL;

    /* Button */
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, cfg->size, cfg->size);
    lv_obj_set_pos(btn, cfg->x, cfg->y);
    lv_obj_set_style_bg_color(btn, cfg->bg_color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    if (cfg->circular) {
        lv_obj_set_style_radius(btn, cfg->size / 2, 0);
    }

    if (cfg->shadow_width > 0) {
        lv_obj_set_style_shadow_width(btn, cfg->shadow_width, 0);
        lv_obj_set_style_shadow_ofs_y(btn, 2, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
    }

    /* Symbol label */
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, cfg->symbol ? cfg->symbol : "");
    lv_obj_set_style_text_color(lbl, cfg->icon_color, 0);
    if (cfg->font) {
        lv_obj_set_style_text_font(lbl, cfg->font, 0);
    } else {
        lv_obj_set_style_text_font(lbl, IVF_FONT_NORMAL, 0);
    }
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    self->btn   = btn;
    self->label = lbl;

    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, self);

    return self;
}

/* ── Configuration ──────────────────────────────────────────────────────── */

void icon_button_set_click_cb(icon_button_t *btn,
                               icon_button_cb_t cb,
                               void *user_data)
{
    if (!btn) return;
    btn->cb        = cb;
    btn->user_data = user_data;
}

void icon_button_set_symbol(icon_button_t *btn, const char *symbol)
{
    if (!btn || !symbol) return;
    lv_label_set_text(btn->label, symbol);
}

/* ── Accessors ──────────────────────────────────────────────────────────── */

lv_obj_t *icon_button_get_obj(const icon_button_t *btn)
{
    return btn ? btn->btn : NULL;
}

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

void icon_button_destroy(icon_button_t *btn)
{
    if (!btn) return;
    /* Do NOT delete btn->btn here — LVGL parent owns the object tree */
    free(btn);
}
