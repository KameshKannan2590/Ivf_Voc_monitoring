#include "sensor_manager.h"
#include "sensor_backend.h"
#include "data/alarm_manager.h"

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

    while (1) {
        sensor_data_t fresh = {0};
        sensor_backend_sample(&fresh);

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        memcpy(&s_data, &fresh, sizeof(s_data));
        xSemaphoreGive(s_mutex);

        /* Feed alarm manager */
        alarm_manager_check(&fresh);

        vTaskDelay(pdMS_TO_TICKS(1000));   /* 1 Hz sampling */
    }
}

/* --- NVS helpers --- */
static void load_thresholds_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open("ivf_cfg", NVS_READONLY, &h) != ESP_OK) return;

    int32_t v;
    if (nvs_get_i32(h, "voc_warn",  &v) == ESP_OK) s_voc_warn_ppb  = (float)v;
    if (nvs_get_i32(h, "voc_alarm", &v) == ESP_OK) s_voc_alarm_ppb = (float)v;
    if (nvs_get_i32(h, "tmp_warn",  &v) == ESP_OK) s_temp_warn_c   = (float)v / 10.0f;
    if (nvs_get_i32(h, "tmp_alarm", &v) == ESP_OK) s_temp_alarm_c  = (float)v / 10.0f;
    if (nvs_get_i32(h, "hum_lo",    &v) == ESP_OK) s_hum_low_warn  = (float)v;
    if (nvs_get_i32(h, "hum_hi",    &v) == ESP_OK) s_hum_high_warn = (float)v;

    nvs_close(h);
    ESP_LOGI(TAG, "Loaded thresholds from NVS: VOC warn=%.0f alarm=%.0f",
             s_voc_warn_ppb, s_voc_alarm_ppb);
}

/* --- Public API --- */
esp_err_t sensor_manager_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    load_thresholds_from_nvs();
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
