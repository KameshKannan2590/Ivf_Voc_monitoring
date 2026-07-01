#include "card.h"
#include <stdlib.h>
#include <string.h>

/* =======================================================================
 * card.c — Reusable card container implementation
 * ======================================================================= */

/* ── Handle definition ──────────────────────────────────────────────────── */

struct card {
    lv_obj_t *outer;    /* styled card shell with border/shadow     */
    lv_obj_t *title;    /* optional title label (NULL if not used)  */
    lv_obj_t *content;  /* inner container for caller's widgets     */
};

/* ── Creation ───────────────────────────────────────────────────────────── */

card_t *card_create(lv_obj_t *parent, const card_cfg_t *cfg)
{
    if (!cfg) return NULL;

    card_t *c = (card_t *)malloc(sizeof(*c));
    if (!c) return NULL;

    /* Outer shell */
    lv_obj_t *outer = lv_obj_create(parent);
    lv_obj_set_size(outer, cfg->w, cfg->h);
    lv_obj_set_style_bg_color(outer, cfg->bg_color, 0);
    lv_obj_set_style_bg_opa(outer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(outer, cfg->radius, 0);
    lv_obj_set_style_border_color(outer, cfg->border_color, 0);
    lv_obj_set_style_border_width(outer, cfg->border_width, 0);
    lv_obj_set_style_pad_all(outer, cfg->pad, 0);
    lv_obj_clear_flag(outer, LV_OBJ_FLAG_SCROLLABLE);

    if (cfg->shadow) {
        lv_obj_set_style_shadow_width(outer, 8, 0);
        lv_obj_set_style_shadow_ofs_y(outer, 2, 0);
        lv_obj_set_style_shadow_color(outer, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(outer, LV_OPA_10, 0);
    }

    c->outer   = outer;
    c->title   = NULL;
    c->content = NULL;

    /* Optional title label */
    lv_coord_t content_y = 0;
    if (cfg->title && cfg->title[0] != '\0') {
        lv_obj_t *title = lv_label_create(outer);
        lv_label_set_text(title, cfg->title);
        lv_obj_set_style_text_color(title, lv_color_hex(0x616161), 0);
        lv_obj_set_pos(title, 0, 0);
        c->title    = title;
        content_y   = 20;   /* reserve 20 px for title row */
    }

    /* Inner content container — transparent, no border, no scroll */
    lv_obj_t *inner = lv_obj_create(outer);
    lv_obj_set_size(inner, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_pos(inner, 0, content_y);
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(inner, 0, 0);
    lv_obj_set_style_pad_all(inner, 0, 0);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);
    c->content = inner;

    return c;
}

/* ── Accessors ──────────────────────────────────────────────────────────── */

lv_obj_t *card_get_content(const card_t *card)
{
    return card ? card->content : NULL;
}

lv_obj_t *card_get_obj(const card_t *card)
{
    return card ? card->outer : NULL;
}

/* ── Setters ────────────────────────────────────────────────────────────── */

void card_set_title(card_t *card, const char *title)
{
    if (!card || !title) return;

    if (card->title) {
        lv_label_set_text(card->title, title);
    } else {
        /* Lazily create the title label the first time */
        lv_obj_t *lbl = lv_label_create(card->outer);
        lv_label_set_text(lbl, title);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x616161), 0);
        lv_obj_set_pos(lbl, 0, 0);
        card->title = lbl;
    }
}

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

void card_destroy(card_t *card)
{
    if (!card) return;
    free(card);
}
