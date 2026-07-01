#include "screen_logs.h"
#include "ui/ui.h"

#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "logs";

static lv_obj_t *s_scr = NULL;

lv_obj_t *screen_logs_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, IVF_SCREEN_W, IVF_SCREEN_H);
    lv_obj_set_style_bg_color(s_scr, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_build_header(s_scr, "DATA LOGS");

    /* Content area — Phase 5 adds lv_table with columns:
       Time / TVOC (ppb) / Temp (°C) / Humidity (%) / Status */

    ESP_LOGD(TAG, "Logs stub created");
    return s_scr;
}

void screen_logs_refresh(void)
{
    /* Phase 5: reload lv_table rows from sensor record ring buffer */
}
