#include "screen_settings.h"
#include "ui/ui.h"

#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "settings";

static lv_obj_t *s_scr = NULL;

lv_obj_t *screen_settings_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, IVF_SCREEN_W, IVF_SCREEN_H);
    lv_obj_set_style_bg_color(s_scr, IVF_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_build_header(s_scr, "SETTINGS");
    ui_build_tab_bar(s_scr, SCREEN_SETTINGS);

    /* Content area — Phase 6 adds:
       - Brightness slider (PWM backlight)
       - VOC / Temp / Humidity threshold spinboxes
       - NVS persistence via config_manager (not direct NVS calls)
       Restore lv_indev_wait_release(lv_indev_get_act()) in spinbox
       event handlers to prevent phantom touch repeats (see old settings). */

    ESP_LOGD(TAG, "Settings stub created");
    return s_scr;
}
