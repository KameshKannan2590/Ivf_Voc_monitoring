#include "screen_logs.h"
#include "ui/ui.h"
#include "header.h"
#include "card.h"
#include "assets.h"
#include "history_manager.h"
#include "calendar_util.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "logs";

/* ── State ────────────────────────────────────────────────────────────────── */

static lv_obj_t *s_scr             = NULL;
static header_t  *s_hdr            = NULL;
static lv_obj_t *s_lbl_total       = NULL;
static lv_obj_t *s_rows_container  = NULL;
static lv_obj_t *s_load_more_row   = NULL;

/* TVOC/TEMP/HUM column start-x, computed once from the header row's actual
 * rendered label widths (see screen_logs_create()) so data-row values line
 * up under their header exactly, whatever the header font/text turns out
 * to measure — no more guessing fixed offsets and getting them wrong. */
static lv_coord_t s_col_tvoc_x = 0;
static lv_coord_t s_col_temp_x = 0;
static lv_coord_t s_col_hum_x  = 0;

/* ── Pagination — "10 entries per page", Load More reveals the next 10 ──────
 * s_skip = how many of the newest records are already rendered. A single
 * page fetch is history_manager_get_latest_n(s_skip, LOGS_PAGE_SIZE, buf),
 * which returns records newest-first — exactly the "most recent on top"
 * order a log viewer wants, without needing to fetch/scan the entire
 * 90-day history to find the tail (see history_manager.c). */
#define LOGS_PAGE_SIZE       10
/* Embedded safety cap: unbounded "Load More" clicking would keep creating
 * LVGL row objects forever (up to 2160 across the full 90-day history) —
 * 10 pages (100 rows) is far more than anyone will realistically page
 * through by hand, and keeps the object count bounded. Load More simply
 * stops appearing past this point, even if older history remains. */
#define LOGS_MAX_LOADED_ROWS 100

static uint16_t s_skip        = 0;
static uint16_t s_total_count = 0;

/* ── Layout constants ─────────────────────────────────────────────────────── */
#define MARGIN            5
#define TOP_ROW_Y         8
#define TOP_ROW_H         40
#define TOPBAR_ICON_SIZE  18

#define TABLE_Y           (TOP_ROW_Y + TOP_ROW_H + 10)
#define TABLE_HEADER_H    26
#define ROW_H             30
#define DOT_SIZE          6

#define LOAD_MORE_H       28
#define LOAD_MORE_GAP     6

#define TABLE_H           (IVF_CONTENT_H - TABLE_Y - LOAD_MORE_H - LOAD_MORE_GAP - MARGIN)
#define ROWS_AREA_H       (TABLE_H - TABLE_HEADER_H)

/* First column is fixed (right after the dot); the rest are computed at
 * header-build time from actual label widths — see s_col_tvoc_x etc. above.
 * Figma spec: 26.5 px from the screen's left edge to the date/time text
 * itself (not the dot) — MARGIN (8, screen->card) is fixed, so the
 * row-relative remainder is 26.5-8=18.5, rounded to 19. */
#define COL_DATE_X   16

/* Header-row inter-column gaps (Figma spec): DATE&TIME -> TVOC = 21.5 px
 * (rounded to 22, lv_coord_t has no fractional pixels), TVOC -> TEMP = 6 px,
 * TEMP -> HUM = 8 px. */
#define COL_GAP_DATE_TVOC 22
#define COL_GAP_TVOC_TEMP 6
#define COL_GAP_TEMP_HUM  8

/* ── Menu button callback ────────────────────────────────────────────────── */

static void on_menu_btn(void *user_data)
{
    (void)user_data;
    ui_nav_drawer_toggle();
}

/* ── Export CSV — placeholder only ────────────────────────────────────────
 * CSV export needs SD/flash storage, which does not exist yet (Phase 9 in
 * the roadmap: data/sd_export.c/.h). Matches the design visually; tapping
 * it does not write anything anywhere yet. */
static void on_export_csv_click(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Export CSV tapped - not implemented yet (needs SD storage, Phase 9)");
}

/* ── Row builder ──────────────────────────────────────────────────────────── */

static void logs_make_row(lv_obj_t *parent, const history_record_t *rec)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, IVF_SCREEN_W - 2 * MARGIN, ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    /* Status dot — green under the shared early-warning threshold, orange
     * at/above it (VOC_WARNING_THRESHOLD_PPB, same constant Chart uses). */
    lv_color_t dot_color = (rec->avg_voc_ppb >= VOC_WARNING_THRESHOLD_PPB) ? IVF_COLOR_WARNING : IVF_COLOR_GOOD;
    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
    lv_obj_set_pos(dot, 0, (ROW_H - DOT_SIZE) / 2);
    lv_obj_set_style_bg_color(dot, dot_color, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);

    char dt_buf[24];
    calendar_util_format_datetime(rec->timestamp_s, dt_buf, sizeof(dt_buf));
    lv_obj_t *lbl_dt = lv_label_create(row);
    lv_label_set_text(lbl_dt, dt_buf);
    lv_obj_set_style_text_font(lbl_dt, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_dt, IVF_COLOR_TEXT, 0);
    lv_obj_set_pos(lbl_dt, COL_DATE_X, (ROW_H - 12) / 2);

    /* Standard snprintf, not lv_snprintf: LV_SPRINTF_USE_FLOAT is off in
     * this project's sdkconfig (see screen_chart.c for the same note). */
    char vbuf[12];
    snprintf(vbuf, sizeof(vbuf), "%.0f", (double)rec->avg_voc_ppb);
    lv_obj_t *lbl_tvoc = lv_label_create(row);
    lv_label_set_text(lbl_tvoc, vbuf);
    lv_obj_set_style_text_font(lbl_tvoc, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_tvoc, IVF_COLOR_TEXT, 0);
    lv_obj_set_pos(lbl_tvoc, s_col_tvoc_x, (ROW_H - 12) / 2);

    snprintf(vbuf, sizeof(vbuf), "%.1f", (double)rec->temperature_c);
    lv_obj_t *lbl_temp = lv_label_create(row);
    lv_label_set_text(lbl_temp, vbuf);
    lv_obj_set_style_text_font(lbl_temp, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_temp, IVF_COLOR_TEXT, 0);
    lv_obj_set_pos(lbl_temp, s_col_temp_x, (ROW_H - 12) / 2);

    snprintf(vbuf, sizeof(vbuf), "%.0f", (double)rec->humidity_pct);
    lv_obj_t *lbl_hum = lv_label_create(row);
    lv_label_set_text(lbl_hum, vbuf);
    lv_obj_set_style_text_font(lbl_hum, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_hum, IVF_COLOR_TEXT, 0);
    lv_obj_set_pos(lbl_hum, s_col_hum_x, (ROW_H - 12) / 2);
}

/* ── Pagination control ──────────────────────────────────────────────────── */

static void logs_update_load_more_visibility(void)
{
    bool more = (s_skip < s_total_count) && (s_skip < LOGS_MAX_LOADED_ROWS);
    if (more) lv_obj_clear_flag(s_load_more_row, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_load_more_row, LV_OBJ_FLAG_HIDDEN);
}

static void logs_load_page(void)
{
    history_record_t buf[LOGS_PAGE_SIZE];
    uint16_t n = history_manager_get_latest_n(s_skip, LOGS_PAGE_SIZE, buf);
    for (uint16_t i = 0; i < n; i++) {
        logs_make_row(s_rows_container, &buf[i]);
    }
    s_skip += n;
    logs_update_load_more_visibility();
}

static void on_load_more_click(lv_event_t *e)
{
    (void)e;
    logs_load_page();
}

/* Rebuilds the list from scratch — always the newest page first, regardless
 * of what was loaded before the user last left this screen (same "reset on
 * entry" policy screen_chart_refresh() uses for its default mode). */
static void logs_reset_and_load_first_page(void)
{
    lv_obj_clean(s_rows_container);
    s_skip = 0;

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    s_total_count = history_manager_get_count_in_range(0, now);

    char total_buf[32];
    snprintf(total_buf, sizeof(total_buf), "Total record: %u", s_total_count);
    lv_label_set_text(s_lbl_total, total_buf);

    logs_load_page();
}

/* ── Public API ────────────────────────────────────────────────────────────── */

lv_obj_t *screen_logs_create(void)
{
    calendar_util_init_reference();

    /* ── Screen root ──────────────────────────────────────────────────────── */
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, IVF_SCREEN_W, IVF_SCREEN_H);
    lv_obj_set_style_bg_color(s_scr, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Header — shared component, pixel identical to Dashboard/Chart ─────── */
    s_hdr = header_create(s_scr);
    header_set_title(s_hdr, "LOGS");
    header_set_wifi_strength(s_hdr, WIFI_STRENGTH_HIGH);
    header_set_sd_status(s_hdr, SD_STATUS_ABSENT);
    header_set_time(s_hdr, "08:25 AM");
    header_set_date(s_hdr, "May 24, 2026");
    header_enable_menu(s_hdr, on_menu_btn, NULL);

    /* ── Content container ─────────────────────────────────────────────────── */
    lv_obj_t *content = lv_obj_create(s_scr);
    lv_obj_set_size(content, IVF_SCREEN_W, IVF_CONTENT_H);
    lv_obj_set_pos(content, 0, IVF_HEADER_H);
    lv_obj_set_style_bg_color(content, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_CLICKABLE);

    /* ── Top bar: datalog icon + "Total record: N" (left) / Export CSV (right) */
    card_cfg_t top_cfg = {
        .w            = IVF_SCREEN_W - 2 * MARGIN,
        .h            = TOP_ROW_H,
        .radius       = IVF_CARD_RADIUS,
        .bg_color     = IVF_COLOR_CARD,
        .border_color = IVF_COLOR_BORDER,
        .border_width = 1,
        .pad          = IVF_PAD,
        .shadow       = false,
        .title        = NULL,
    };
    card_t   *top_card    = card_create(content, &top_cfg);
    lv_obj_t *top_content = card_get_content(top_card);
    lv_obj_set_pos(card_get_obj(top_card), MARGIN, TOP_ROW_Y);
    lv_obj_clear_flag(top_content, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t top_content_h = TOP_ROW_H - 2 * IVF_PAD;
    lv_coord_t top_content_w = IVF_SCREEN_W - 2 * MARGIN - 2 * IVF_PAD;

    assets_draw_datalog_icon(top_content, 0, (top_content_h - TOPBAR_ICON_SIZE) / 2, IVF_COLOR_PRIMARY);

    s_lbl_total = lv_label_create(top_content);
    lv_label_set_text(s_lbl_total, "Total record: 0");
    lv_obj_set_style_text_font(s_lbl_total, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_lbl_total, IVF_COLOR_TEXT, 0);
    lv_obj_set_pos(s_lbl_total, TOPBAR_ICON_SIZE + 6, (top_content_h - 12) / 2);

    lv_coord_t export_w = 96, export_h = 24;
    lv_obj_t *export_btn = lv_btn_create(top_content);
    lv_obj_set_size(export_btn, export_w, export_h);
    lv_obj_set_pos(export_btn, top_content_w - export_w, (top_content_h - export_h) / 2);
    lv_obj_set_style_bg_color(export_btn, IVF_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(export_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(export_btn, IVF_CARD_RADIUS, 0);
    lv_obj_set_style_border_width(export_btn, 0, 0);
    lv_obj_add_event_cb(export_btn, on_export_csv_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *export_lbl = lv_label_create(export_btn);
    lv_label_set_text(export_lbl, LV_SYMBOL_UPLOAD " Export CSV");
    lv_obj_set_style_text_font(export_lbl, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(export_lbl, lv_color_white(), 0);
    lv_obj_center(export_lbl);

    /* ── Table card: header row + scrollable data rows ───────────────────────
     * The rows area is a fixed-height scrollable region (not a growing card)
     * so "Load More" can append rows without pushing anything else on the
     * screen around — the ~10th row onward is reached by scrolling. */
    card_cfg_t table_cfg = {
        .w            = IVF_SCREEN_W - 2 * MARGIN,
        .h            = TABLE_H,
        .radius       = IVF_CARD_RADIUS,
        .bg_color     = IVF_COLOR_CARD,
        .border_color = IVF_COLOR_BORDER,
        .border_width = 1,
        .pad          = 0,
        .shadow       = false,
        .title        = NULL,
    };
    card_t   *table_card    = card_create(content, &table_cfg);
    lv_obj_t *table_content = card_get_content(table_card);
    lv_obj_set_pos(card_get_obj(table_card), MARGIN, TABLE_Y);
    lv_obj_clear_flag(table_content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr_row = lv_obj_create(table_content);
    lv_obj_set_size(hdr_row, IVF_SCREEN_W - 2 * MARGIN, TABLE_HEADER_H);
    lv_obj_set_pos(hdr_row, 0, 0);
    lv_obj_set_style_bg_opa(hdr_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr_row, 1, 0);
    lv_obj_set_style_border_side(hdr_row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr_row, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(hdr_row, 0, 0);
    lv_obj_set_style_radius(hdr_row, 0, 0);
    lv_obj_clear_flag(hdr_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(hdr_row, LV_OBJ_FLAG_CLICKABLE);

    /* Each header label is positioned relative to the PREVIOUS label's actual
     * rendered width (lv_obj_align_to), not a guessed fixed offset — that's
     * what was causing the headers to overlap. Data rows then reuse these
     * exact resolved x positions (captured below) so values line up under
     * their header. */
    lv_obj_t *hdr_date = lv_label_create(hdr_row);
    lv_label_set_text(hdr_date, "DATE & TIME");
    lv_obj_set_style_text_font(hdr_date, IVF_FONT_TINY, 0);
    lv_obj_set_style_text_color(hdr_date, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(hdr_date, COL_DATE_X, (TABLE_HEADER_H - 10) / 2);

    /* Units restored per request (ppb/C/% must be in the header). No space
     * before the "(" — that's the few px of slack needed to keep the whole
     * row inside the table now that COL_DATE_X grew to hit the 26.5 px
     * margin; see the defensive width check below. */
    lv_obj_t *hdr_tvoc = lv_label_create(hdr_row);
    lv_label_set_text(hdr_tvoc, "TVOC(ppb)");
    lv_obj_set_style_text_font(hdr_tvoc, IVF_FONT_TINY, 0);
    lv_obj_set_style_text_color(hdr_tvoc, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align_to(hdr_tvoc, hdr_date, LV_ALIGN_OUT_RIGHT_MID, COL_GAP_DATE_TVOC, 0);
    s_col_tvoc_x = lv_obj_get_x(hdr_tvoc);

    lv_obj_t *hdr_temp = lv_label_create(hdr_row);
    lv_label_set_text(hdr_temp, "TEMP(C)");
    lv_obj_set_style_text_font(hdr_temp, IVF_FONT_TINY, 0);
    lv_obj_set_style_text_color(hdr_temp, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align_to(hdr_temp, hdr_tvoc, LV_ALIGN_OUT_RIGHT_MID, COL_GAP_TVOC_TEMP, 0);
    s_col_temp_x = lv_obj_get_x(hdr_temp);

    lv_obj_t *hdr_hum = lv_label_create(hdr_row);
    lv_label_set_text(hdr_hum, "HUM(%)");
    lv_obj_set_style_text_font(hdr_hum, IVF_FONT_TINY, 0);
    lv_obj_set_style_text_color(hdr_hum, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align_to(hdr_hum, hdr_temp, LV_ALIGN_OUT_RIGHT_MID, COL_GAP_TEMP_HUM, 0);
    s_col_hum_x = lv_obj_get_x(hdr_hum);

    /* Defensive check: warn (rather than silently clip) if the last column
     * still runs past the table's right edge on this build's actual font
     * metrics — cannot be verified visually in this environment. */
    lv_coord_t table_w   = IVF_SCREEN_W - 2 * MARGIN;
    lv_coord_t hum_right = s_col_hum_x + lv_obj_get_width(hdr_hum);
    if (hum_right > table_w) {
        ESP_LOGW(TAG, "HUM column right edge (%d) exceeds table width (%d) - "
                       "will be clipped, columns need more room", (int)hum_right, (int)table_w);
    }

    s_rows_container = lv_obj_create(table_content);
    lv_obj_set_size(s_rows_container, IVF_SCREEN_W - 2 * MARGIN, ROWS_AREA_H);
    lv_obj_set_pos(s_rows_container, 0, TABLE_HEADER_H);
    lv_obj_set_style_bg_opa(s_rows_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_rows_container, 0, 0);
    lv_obj_set_style_pad_all(s_rows_container, 0, 0);
    lv_obj_set_style_radius(s_rows_container, 0, 0);
    lv_obj_set_scroll_dir(s_rows_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_rows_container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(s_rows_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_rows_container, LV_OBJ_FLAG_CLICKABLE);
    /* Column layout: each appended row simply becomes the next flex item,
     * stacking top-to-bottom automatically — no manual y-offset bookkeeping
     * needed across repeated "Load More" appends. */
    lv_obj_set_flex_flow(s_rows_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_rows_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* ── "Load More" — below the table, hidden once the 90-day history (or
     * the LOGS_MAX_LOADED_ROWS safety cap) is exhausted ─────────────────── */
    s_load_more_row = lv_obj_create(content);
    lv_obj_set_size(s_load_more_row, IVF_SCREEN_W - 2 * MARGIN, LOAD_MORE_H);
    lv_obj_set_pos(s_load_more_row, MARGIN, TABLE_Y + TABLE_H + LOAD_MORE_GAP);
    lv_obj_set_style_bg_opa(s_load_more_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_load_more_row, 0, 0);
    lv_obj_set_style_pad_all(s_load_more_row, 0, 0);
    lv_obj_clear_flag(s_load_more_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_load_more_row, on_load_more_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *load_more_lbl = lv_label_create(s_load_more_row);
    lv_label_set_text(load_more_lbl, "Load More " LV_SYMBOL_DOWN);
    lv_obj_set_style_text_font(load_more_lbl, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(load_more_lbl, IVF_COLOR_PRIMARY, 0);
    lv_obj_center(load_more_lbl);

    logs_reset_and_load_first_page();

    ESP_LOGD(TAG, "Logs screen created (Phase 5.8)");
    return s_scr;
}

void screen_logs_refresh(void)
{
    logs_reset_and_load_first_page();
}
