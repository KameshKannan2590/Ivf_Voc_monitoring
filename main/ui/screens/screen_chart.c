#include "screen_chart.h"
#include "ui/ui.h"

#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "chart";

static lv_obj_t *s_scr = NULL;

lv_obj_t *screen_chart_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, IVF_SCREEN_W, IVF_SCREEN_H);
    lv_obj_set_style_bg_color(s_scr, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_build_header(s_scr, "TVOC HISTORY");
    ui_build_tab_bar(s_scr, SCREEN_CHART);

    /* Content area — Phase 4 adds lv_chart with 90/30/7-day series
       and period-switching tab buttons. */

    ESP_LOGD(TAG, "Chart stub created");
    return s_scr;
}

void screen_chart_refresh(void)
{
    /* Phase 4: reload chart series from aggregated sensor history buffer */
}
