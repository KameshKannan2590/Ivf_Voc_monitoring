#include "header.h"
#include "ui.h"        /* layout constants, colour palette, fonts */
#include "assets.h"
#include <stdlib.h>
#include <string.h>

/* =======================================================================
 * header.c — 272 × IVF_HEADER_H (50 px) screen header implementation
 *
 * Layout (left→right):
 *   5px | burger btn (44 px) | 6px | title label (clipped) | … |
 *   time/date stack (80 px) | 4px | wifi (20×20) | 4px | sd (20×20) | 8px
 *
 * Right-to-left derivation:
 *   HDR_SD_X      = 272 − 8 − 20          = 244   (sd card left edge, rightmost)
 *   HDR_WIFI_X    = 244 − 4 − 20          = 220   (wifi left edge, left of sd)
 *   HDR_TIME_ROFS = 8 + 20 + 4 + 20 + 4  =  56   (right offset for time/date labels)
 *   time/date right edge                   = 216   (= 272 − 56)
 *   Widest date "May 24, 2026" ≈ 76 px → left edge ≈ 140.
 *   Title clips at 132 px so it never reaches the time/date column.
 *   Burger btn at x=5, title starts at x=55 (5+44+6).
 * ======================================================================= */

/* ── Geometry ────────────────────────────────────────────────────────────── */

#define HDR_ICON_SIZE    20
#define HDR_ICON_Y       17    /* vertically centre 20 px icon in 50 px bar */

/* SD card is the rightmost element; WiFi sits to its left. */
#define HDR_SD_X        (IVF_SCREEN_W - 5 - HDR_ICON_SIZE)               /* x = 247 */
#define HDR_WIFI_X      (HDR_SD_X - 10 - HDR_ICON_SIZE)                  /* x = 217 */
#define HDR_TIME_ROFS   (5 + HDR_ICON_SIZE + 10 + HDR_ICON_SIZE + 4)     /* = 59    */
#define HDR_TIME_COL_W  80                                                /* reserved width for time/date */
#define HDR_AMPM_W      20    /* reserved width for "AM"/"PM" label + 2px gap */

/* Title clips before the time/date column (conservative 4 px gap). */
#define HDR_TITLE_MAX_X (HDR_WIFI_X - 4 - HDR_TIME_COL_W - 4)               /* x = 132 */

/* Burger button left margin and title gap */
#define HDR_BTN_X        5    /* burger button left edge                    */
#define HDR_BTN_W       20    /* burger button width                        */
#define HDR_TITLE_X     (HDR_BTN_X + HDR_BTN_W + 6)   /* = 31             */

/* ── Handle definition ───────────────────────────────────────────────────── */

struct header {
    lv_obj_t *bar;            /* the 272×50 root container          */
    lv_obj_t *title_lbl;      /* centre/left title text             */
    lv_obj_t *time_lbl;       /* HH:MM portion — montserrat_14      */
    lv_obj_t *ampm_lbl;       /* AM/PM suffix — montserrat_10       */
    lv_obj_t *date_lbl;       /* date string   — montserrat_10      */

    /* WiFi image widget — opacity reflects signal strength */
    lv_obj_t *wifi_img;

    /* SD card image widget (we recolor it per status) */
    lv_obj_t *sd_img;

    /* Leaf icon wrapper — hidden when menu is enabled */
    lv_obj_t *leaf_cont;

    /* Hamburger menu button — NULL until header_enable_menu() is called */
    lv_obj_t         *menu_btn;
    header_menu_cb_t  menu_cb;
    void             *menu_cb_ud;
};

/* ── Internal WiFi icon builder ─────────────────────────────────────────── */

static void build_wifi_icon(struct header *h, lv_obj_t *parent,
                              lv_coord_t x, lv_coord_t y)
{
    /* Vector image is 14×10 px — centre it in the 20×20 icon slot */
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, &Vector);
    lv_obj_set_pos(img, x + (HDR_ICON_SIZE - 14) / 2,
                        y + (HDR_ICON_SIZE - 10) / 2);
    lv_obj_set_style_img_recolor(img, IVF_COLOR_PRIMARY, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    h->wifi_img = img;
}

/* ── Internal SD card icon builder ─────────────────────────────────────── */

static void build_sd_icon(struct header *h, lv_obj_t *parent,
                            lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, &glyphs_sd_card_1_bold);
    lv_obj_set_pos(img, x, y);
    lv_obj_set_style_img_recolor(img, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    h->sd_img = img;
}

/* ── Leaf icon builder ──────────────────────────────────────────────────── */

static lv_obj_t *build_leaf_icon(lv_obj_t *parent, lv_coord_t x, lv_coord_t y)
{
    /* Transparent wrapper so the whole icon can be hidden in one flag call */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, HDR_ICON_SIZE, HDR_ICON_SIZE);
    lv_obj_set_pos(cont, x, y);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *body = lv_obj_create(cont);
    lv_obj_set_size(body, 12, 16);
    lv_obj_set_pos(body, 4, 2);    /* positions are relative to cont */
    lv_obj_set_style_radius(body, 6, 0);
    lv_obj_set_style_bg_color(body, IVF_COLOR_GOOD, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *rib = lv_obj_create(cont);
    lv_obj_set_size(rib, 2, 14);
    lv_obj_set_pos(rib, 9, 3);
    lv_obj_set_style_radius(rib, 1, 0);
    lv_obj_set_style_bg_color(rib, lv_color_darken(IVF_COLOR_GOOD, LV_OPA_20), 0);
    lv_obj_set_style_bg_opa(rib, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rib, 0, 0);
    lv_obj_clear_flag(rib, LV_OBJ_FLAG_SCROLLABLE);

    return cont;
}

/* ── Menu button event ──────────────────────────────────────────────────── */

static void menu_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    struct header *h = (struct header *)lv_event_get_user_data(e);
    if (h && h->menu_cb) {
        h->menu_cb(h->menu_cb_ud);
    }
}

/* ── Public API ─────────────────────────────────────────────────────────── */

header_t *header_create(lv_obj_t *parent)
{
    header_t *h = (header_t *)malloc(sizeof(*h));
    if (!h) return NULL;

    memset(h, 0, sizeof(*h));

    /* Header bar */
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, IVF_SCREEN_W, IVF_HEADER_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    h->bar = bar;

    /* Leaf icon at left */
    h->leaf_cont = build_leaf_icon(bar, 8, HDR_ICON_Y);

    /* Title label — clips before time/date column (HDR_TITLE_MAX_X) */
    lv_obj_t *title = lv_label_create(bar);
    lv_obj_set_style_text_font(title, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(title, IVF_COLOR_TEXT, 0);
    lv_label_set_text(title, "");
    lv_obj_set_pos(title, 31, 16);
    lv_obj_set_width(title, HDR_TITLE_MAX_X - 31);   /* 122 px: leaf area (34) to clip edge (156) */
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    h->title_lbl = title;

    /* WiFi icon — far right */
    build_wifi_icon(h, bar, HDR_WIFI_X, HDR_ICON_Y);

    /* SD card icon — left of WiFi */
    build_sd_icon(h, bar, HDR_SD_X, HDR_ICON_Y);

    /* Time label — "HH:MM" at 14px, sits left of AM/PM */
    lv_obj_t *time_lbl = lv_label_create(bar);
    lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(time_lbl, IVF_COLOR_TEXT, 0);
    lv_label_set_text(time_lbl, "--:--");
    lv_obj_align(time_lbl, LV_ALIGN_TOP_RIGHT, -(HDR_TIME_ROFS + HDR_AMPM_W), 8);
    h->time_lbl = time_lbl;

    /* AM/PM label — 10px, baseline-aligned with time label */
    lv_obj_t *ampm_lbl = lv_label_create(bar);
    lv_obj_set_style_text_font(ampm_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ampm_lbl, IVF_COLOR_TEXT, 0);
    lv_label_set_text(ampm_lbl, "");
    lv_obj_align(ampm_lbl, LV_ALIGN_TOP_RIGHT, -HDR_TIME_ROFS, 12);
    h->ampm_lbl = ampm_lbl;

    /* Date label — 10px, below the time row */
    lv_obj_t *date_lbl = lv_label_create(bar);
    lv_obj_set_style_text_font(date_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(date_lbl, IVF_COLOR_TEXT_MUTED, 0);
    lv_label_set_text(date_lbl, "");
    lv_obj_align(date_lbl, LV_ALIGN_TOP_RIGHT, -HDR_TIME_ROFS, 26);
    h->date_lbl = date_lbl;

    /* Initial state */
    header_set_wifi_strength(h, WIFI_STRENGTH_NONE);

    return h;
}

void header_set_title(header_t *hdr, const char *title)
{
    if (!hdr || !title) return;
    lv_label_set_text(hdr->title_lbl, title);
}

void header_set_time(header_t *hdr, const char *time_str)
{
    if (!hdr || !time_str) return;

    const char *sp = strrchr(time_str, ' ');
    if (sp) {
        char buf[16];
        size_t len = (size_t)(sp - time_str);
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, time_str, len);
        buf[len] = '\0';
        lv_label_set_text(hdr->time_lbl, buf);
        lv_label_set_text(hdr->ampm_lbl, sp + 1);
    } else {
        lv_label_set_text(hdr->time_lbl, time_str);
        lv_label_set_text(hdr->ampm_lbl, "");
    }
    lv_obj_align(hdr->time_lbl, LV_ALIGN_TOP_RIGHT, -(HDR_TIME_ROFS + HDR_AMPM_W), 8);
    lv_obj_align(hdr->ampm_lbl, LV_ALIGN_TOP_RIGHT, -HDR_TIME_ROFS, 12);
}

void header_set_date(header_t *hdr, const char *date_str)
{
    if (!hdr || !date_str) return;
    lv_label_set_text(hdr->date_lbl, date_str);
    lv_obj_align(hdr->date_lbl, LV_ALIGN_TOP_RIGHT, -HDR_TIME_ROFS, 26);
}

void header_set_wifi_strength(header_t *hdr, wifi_strength_t strength)
{
    if (!hdr || !hdr->wifi_img) return;

    static const lv_opa_t OPA[] = {
        LV_OPA_20,   /* NONE — visibly absent  */
        LV_OPA_50,   /* LOW                    */
        LV_OPA_80,   /* MED                    */
        LV_OPA_COVER,/* HIGH                   */
    };
    lv_obj_set_style_img_opa(hdr->wifi_img, OPA[strength], 0);
}

void header_set_sd_status(header_t *hdr, sd_status_t status)
{
    if (!hdr || !hdr->sd_img) return;

    lv_color_t col;
    lv_opa_t   opa;

    switch (status) {
    case SD_STATUS_ABSENT:
        col = IVF_COLOR_TEXT_MUTED;
        opa = LV_OPA_30;
        break;
    case SD_STATUS_PRESENT:
        col = IVF_COLOR_TEXT_MUTED;
        opa = LV_OPA_COVER;
        break;
    case SD_STATUS_ERROR:
        col = IVF_COLOR_DANGER;
        opa = LV_OPA_COVER;
        break;
    default:
        return;
    }

    lv_obj_set_style_img_recolor(hdr->sd_img, col, 0);
    lv_obj_set_style_img_recolor_opa(hdr->sd_img, opa, 0);
}

void header_enable_menu(header_t *hdr, header_menu_cb_t cb, void *user_data)
{
    if (!hdr) return;

    hdr->menu_cb    = cb;
    hdr->menu_cb_ud = user_data;

    /* Hide the decorative leaf icon */
    if (hdr->leaf_cont) {
        lv_obj_add_flag(hdr->leaf_cont, LV_OBJ_FLAG_HIDDEN);
    }

    /* Idempotent — only create the button once */
    if (hdr->menu_btn) return;

    lv_obj_t *btn = lv_btn_create(hdr->bar);
    lv_obj_set_size(btn, HDR_BTN_W, IVF_HEADER_H);
    lv_obj_set_pos(btn, HDR_BTN_X, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(btn, IVF_COLOR_BORDER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img = lv_img_create(btn);
    lv_img_set_src(img, &menu_burger_horizontal_bold);
    lv_obj_set_style_img_recolor(img, IVF_COLOR_TEXT, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);

    lv_obj_add_event_cb(btn, menu_btn_event_cb, LV_EVENT_CLICKED, hdr);
    hdr->menu_btn = btn;

    /* Shift title right to clear the burger button + 6 px gap */
    lv_obj_set_x(hdr->title_lbl, HDR_TITLE_X);
    lv_obj_set_width(hdr->title_lbl, HDR_TITLE_MAX_X - HDR_TITLE_X);
}

lv_obj_t *header_get_obj(const header_t *hdr)
{
    return hdr ? hdr->bar : NULL;
}

void header_destroy(header_t *hdr)
{
    if (!hdr) return;
    free(hdr);
}
