#include "sensor_manager.h"
#include "sensor_backend.h"
#include "data/alarm_manager.h"
#include "data/history_manager.h"
#include "data/config_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "sensor_mgr";

/* --- Thresholds (loaded from NVS, defaults below) --- */
static float s_voc_warn_ppb   = 300.0f;
static float s_voc_alarm_ppb  = 500.0f;
static float s_temp_warn_c    = 26.0f;
static float s_temp_alarm_c   = 28.0f;
static float s_hum_low_warn   = 35.0f;
static float s_hum_high_warn  = 65.0f;

/* --- Live data + protection mutex --- */
static sensor_data_t     s_data;
static SemaphoreHandle_t s_mutex = NULL;

/* --- Sensor task --- */
static void sensor_task(void *arg)
{
    ESP_LOGI(TAG, "Sensor task running");

    uint32_t history_tick = 0;   /* decimates the 1 Hz loop to ~1 sample/minute */

    while (1) {
        sensor_data_t fresh = {0};
        sensor_backend_sample(&fresh);

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        memcpy(&s_data, &fresh, sizeof(s_data));
        xSemaphoreGive(s_mutex);

        /* Feed alarm manager */
        alarm_manager_check(&fresh);

        /* Feed history manager (Phase 5.3) — skip on a sensor fault so a
         * comms error doesn't record a false 0.0 reading into history. */
        if (++history_tick % 60 == 0 && fresh.sensor_ok) {
            history_manager_add_sample(fresh.voc_ppb, fresh.temperature_c, fresh.humidity_pct);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));   /* 1 Hz sampling */
    }
}

/* --- NVS helpers ---
 * VOC thresholds are owned by config_manager (Phase 6) since the Settings
 * screen writes them — reading through the same module avoids two paths
 * to the same "voc_warn"/"voc_alarm" NVS keys. Temp/humidity thresholds
 * have no Settings UI yet, so they still read NVS directly, unchanged. */
static void load_thresholds(void)
{
    s_voc_warn_ppb  = (float)config_manager_get_voc_warn_ppb();
    s_voc_alarm_ppb = (float)config_manager_get_voc_alarm_ppb();

    nvs_handle_t h;
    if (nvs_open("ivf_cfg", NVS_READONLY, &h) == ESP_OK) {
        int32_t v;
        if (nvs_get_i32(h, "tmp_warn",  &v) == ESP_OK) s_temp_warn_c   = (float)v / 10.0f;
        if (nvs_get_i32(h, "tmp_alarm", &v) == ESP_OK) s_temp_alarm_c  = (float)v / 10.0f;
        if (nvs_get_i32(h, "hum_lo",    &v) == ESP_OK) s_hum_low_warn  = (float)v;
        if (nvs_get_i32(h, "hum_hi",    &v) == ESP_OK) s_hum_high_warn = (float)v;
        nvs_close(h);
    }

    ESP_LOGI(TAG, "Loaded thresholds: VOC warn=%.0f alarm=%.0f",
             s_voc_warn_ppb, s_voc_alarm_ppb);
}

void sensor_manager_reload_thresholds(void)
{
    load_thresholds();
}

/* --- Public API --- */
esp_err_t sensor_manager_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    load_thresholds();
    sensor_backend_init();

    memset(&s_data, 0, sizeof(s_data));

    xTaskCreatePinnedToCore(
        sensor_task, "sensor",
        4096, NULL, 3, NULL, 0   /* core 0, higher priority than LVGL */
    );

    ESP_LOGI(TAG, "Sensor manager initialized");
    return ESP_OK;
}

void sensor_manager_get_data(sensor_data_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(out, &s_data, sizeof(*out));
    xSemaphoreGive(s_mutex);
}

sensor_level_t sensor_get_voc_level(float voc_ppb)
{
    if (voc_ppb >= s_voc_alarm_ppb) return SENSOR_LEVEL_DANGER;
    if (voc_ppb >= s_voc_warn_ppb)  return SENSOR_LEVEL_WARNING;
    return SENSOR_LEVEL_GOOD;
}

sensor_level_t sensor_get_temp_level(float temp_c)
{
    if (temp_c >= s_temp_alarm_c) return SENSOR_LEVEL_DANGER;
    if (temp_c >= s_temp_warn_c)  return SENSOR_LEVEL_WARNING;
    return SENSOR_LEVEL_GOOD;
}

sensor_level_t sensor_get_hum_level(float hum_pct)
{
    if (hum_pct < s_hum_low_warn || hum_pct > s_hum_high_warn)
        return SENSOR_LEVEL_WARNING;
    return SENSOR_LEVEL_GOOD;
}
