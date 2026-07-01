#include "status_badge.h"
#include <stdlib.h>

/* =======================================================================
 * status_badge.c — Pill-shaped status badge implementation
 * ======================================================================= */

/* Text labels — true compile-time constants */
static const char * const STATE_TEXT[] = {
    [BADGE_STATE_GOOD]     = "GOOD",
    [BADGE_STATE_MODERATE] = "MODERATE",
    [BADGE_STATE_POOR]     = "POOR",
    [BADGE_STATE_DANGER]   = "DANGER",
    [BADGE_STATE_ERROR]    = "ERROR",
};

/* Colours — runtime-initialised (lv_color_hex is not a constant expression) */
static lv_color_t s_state_colors[5];
static bool       s_colours_ready = false;

static void ensure_colours(void)
{
    if (s_colours_ready) return;
    s_state_colors[BADGE_STATE_GOOD]     = lv_color_hex(0x43A047);
    s_state_colors[BADGE_STATE_MODERATE] = lv_color_hex(0xFDD835);
    s_state_colors[BADGE_STATE_POOR]     = lv_color_hex(0xFB8C00);
    s_state_colors[BADGE_STATE_DANGER]   = lv_color_hex(0xE53935);
    s_state_colors[BADGE_STATE_ERROR]    = lv_color_hex(0x9E9E9E);
    s_colours_ready = true;
}

/* ── Handle definition ──────────────────────────────────────────────────── */

struct status_badge {
    lv_obj_t   *container;   /* pill root — the positionable object */
    lv_obj_t   *label;       /* text inside the pill                */
};

/* ── Creation ───────────────────────────────────────────────────────────── */

status_badge_t *status_badge_create(lv_obj_t *parent)
{
    ensure_colours();

    status_badge_t *b = (status_badge_t *)malloc(sizeof(*b));
    if (!b) return NULL;

    /* Pill container */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(cont, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_hor(cont, 10, 0);
    lv_obj_set_style_pad_ver(cont, 4, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Label */
    lv_obj_t *lbl = lv_label_create(cont);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    b->container = cont;
    b->label     = lbl;

    /* Set initial state */
    status_badge_set_state(b, BADGE_STATE_GOOD);
    return b;
}

/* ── Setters ────────────────────────────────────────────────────────────── */

void status_badge_set_state(status_badge_t *badge, badge_state_t state)
{
    if (!badge) return;
    ensure_colours();
    if ((unsigned)state >= (sizeof(STATE_TEXT) / sizeof(STATE_TEXT[0]))) return;

    lv_obj_set_style_bg_color(badge->container, s_state_colors[state], 0);
    lv_obj_set_style_bg_opa(badge->container, LV_OPA_COVER, 0);
    lv_label_set_text(badge->label, STATE_TEXT[state]);
}

void status_badge_set_text(status_badge_t *badge, const char *text)
{
    if (!badge || !text) return;
    lv_label_set_text(badge->label, text);
}

void status_badge_set_custom(status_badge_t *badge,
                              const char *text,
                              lv_color_t bg_color)
{
    if (!badge) return;
    lv_obj_set_style_bg_color(badge->container, bg_color, 0);
    lv_obj_set_style_bg_opa(badge->container, LV_OPA_COVER, 0);
    if (text) lv_label_set_text(badge->label, text);
}

/* ── Accessors ──────────────────────────────────────────────────────────── */

lv_obj_t *status_badge_get_obj(const status_badge_t *badge)
{
    return badge ? badge->container : NULL;
}

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

void status_badge_destroy(status_badge_t *badge)
{
    if (!badge) return;
    /* LVGL parent-child deletion handles the lv_obj tree */
    free(badge);
}
