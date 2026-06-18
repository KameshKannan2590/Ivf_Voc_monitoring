#include "screen_splash.h"
#include "ui/ui.h"

#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "splash";

static lv_obj_t *s_scr      = NULL;
static lv_obj_t *s_bar      = NULL;
static lv_obj_t *s_status   = NULL;
static lv_timer_t *s_timer  = NULL;
static int        s_step    = 0;

/* Init steps shown during splash */
static const char * const INIT_STEPS[] = {
    "Initializing display...",
    "Initializing touch...",
    "Starting sensors...",
    "Loading configuration...",
    "Starting monitoring...",
    "Ready",
};
#define INIT_STEP_COUNT  (int)(sizeof(INIT_STEPS) / sizeof(INIT_STEPS[0]))

static void splash_timer_cb(lv_timer_t *timer)
{
    s_step++;
    int pct = s_step * 100 / INIT_STEP_COUNT;

    lv_bar_set_value(s_bar, pct, LV_ANIM_ON);

    if (s_step < INIT_STEP_COUNT) {
        lv_label_set_text(s_status, INIT_STEPS[s_step - 1]);
    } else {
        lv_label_set_text(s_status, INIT_STEPS[INIT_STEP_COUNT - 1]);
        lv_timer_del(s_timer);
        s_timer = NULL;

        /* Transition to dashboard */
        ui_goto_screen(SCREEN_DASHBOARD, true);
    }
}

lv_obj_t *screen_splash_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, IVF_SCREEN_W, IVF_SCREEN_H);
    lv_obj_set_style_bg_color(s_scr, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);

    /* ── Title ── */
    lv_obj_t *title = lv_label_create(s_scr);
    lv_label_set_text(title, "IVF VOC Monitor");
    lv_obj_set_style_text_font(title, IVF_FONT_XLARGE, 0);
    lv_obj_set_style_text_color(title, IVF_COLOR_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -60);

    /* ── Subtitle ── */
    lv_obj_t *sub = lv_label_create(s_scr);
    lv_label_set_text(sub, "Environmental Monitoring System");
    lv_obj_set_style_text_font(sub, IVF_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(sub, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, -28);

    /* ── Separator line ── */
    lv_obj_t *line_obj = lv_obj_create(s_scr);
    lv_obj_set_size(line_obj, 200, 1);
    lv_obj_set_style_bg_color(line_obj, IVF_COLOR_BORDER, 0);
    lv_obj_set_style_bg_opa(line_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line_obj, 0, 0);
    lv_obj_align(line_obj, LV_ALIGN_CENTER, 0, -10);

    /* ── Progress bar ── */
    s_bar = lv_bar_create(s_scr);
    lv_obj_set_size(s_bar, 240, 10);
    lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, 30);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(s_bar, IVF_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, IVF_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 5, LV_PART_INDICATOR);

    /* ── Status text ── */
    s_status = lv_label_create(s_scr);
    lv_label_set_text(s_status, "Starting...");
    lv_obj_set_style_text_font(s_status, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_status, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 52);

    /* ── Version ── */
    lv_obj_t *ver = lv_label_create(s_scr);
    lv_label_set_text(ver, "v1.0.0");
    lv_obj_set_style_text_font(ver, IVF_FONT_SMALL, 0);
    lv_obj_set_style_text_color(ver, IVF_COLOR_TEXT_MUTED, 0);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_RIGHT, -12, -8);

    ESP_LOGD(TAG, "Splash screen created");
    return s_scr;
}

void screen_splash_start(void)
{
    s_step  = 0;
    s_timer = lv_timer_create(splash_timer_cb, 400, NULL);
}
