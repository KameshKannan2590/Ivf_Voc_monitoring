#include "alarm_manager.h"
#include "config_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include <string.h>

static const char *TAG = "alarm_mgr";

/* Circular ring buffer for alarm history */
static alarm_entry_t  s_history[ALARM_MAX_COUNT];
static int            s_count = 0;       /* total entries (capped at ALARM_MAX_COUNT) */
static int            s_head  = 0;       /* index of oldest entry */
static SemaphoreHandle_t s_mutex = NULL;

/* Per-type debounce: consecutive samples above threshold before firing */
#define ALARM_DEBOUNCE_COUNT 3
static int s_debounce[ALARM_TYPE_COUNT] = {0};
static bool s_active_state[ALARM_TYPE_COUNT] = {false};

/* --- Thresholds ---
 * VOC alarm threshold is runtime-configurable (Phase 6, config_manager) —
 * it's the real critical-alert trigger point behind Settings' "High Alert
 * Threshold" control, deliberately the SAME "voc_alarm" NVS value
 * sensor_manager reads (that one's for gauge color coding only; this one
 * actually raises ALARM_VOC_HIGH). Temp/humidity have no Settings UI yet,
 * so they stay fixed constants for now. */
static float s_voc_alarm_ppb = 500.0f;
#define TEMP_HIGH_C     28.0f
#define TEMP_LOW_C      18.0f
#define HUM_HIGH_PCT    65.0f
#define HUM_LOW_PCT     35.0f

static void push_alarm(alarm_type_t type, float value, float threshold)
{
    alarm_entry_t *slot;
    if (s_count < ALARM_MAX_COUNT) {
        slot = &s_history[(s_head + s_count) % ALARM_MAX_COUNT];
        s_count++;
    } else {
        /* Overwrite oldest */
        slot   = &s_history[s_head];
        s_head = (s_head + 1) % ALARM_MAX_COUNT;
    }
    slot->type             = type;
    slot->measured_value   = value;
    slot->threshold_value  = threshold;
    slot->timestamp_ms     = (uint32_t)(esp_timer_get_time() / 1000ULL);
    slot->active           = true;
    slot->acknowledged     = false;

    ESP_LOGW(TAG, "ALARM: %s  val=%.1f  thr=%.1f",
             alarm_type_str(type), value, threshold);
}

static void check_threshold(alarm_type_t type, bool condition,
                             float value, float threshold)
{
    if (condition) {
        s_debounce[type]++;
        if (s_debounce[type] == ALARM_DEBOUNCE_COUNT && !s_active_state[type]) {
            s_active_state[type] = true;
            push_alarm(type, value, threshold);
        }
    } else {
        s_debounce[type] = 0;
        s_active_state[type] = false;
    }
}

/* --- Public API --- */
esp_err_t alarm_manager_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    memset(s_history, 0, sizeof(s_history));
    s_voc_alarm_ppb = (float)config_manager_get_voc_alarm_ppb();
    ESP_LOGI(TAG, "Alarm manager initialized (VOC alarm threshold=%.0f)", s_voc_alarm_ppb);
    return ESP_OK;
}

void alarm_manager_reload_thresholds(void)
{
    s_voc_alarm_ppb = (float)config_manager_get_voc_alarm_ppb();
    ESP_LOGI(TAG, "VOC alarm threshold reloaded: %.0f", s_voc_alarm_ppb);
}

void alarm_manager_check(const sensor_data_t *data)
{
    if (!s_mutex) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!data->sensor_ok) {
        check_threshold(ALARM_SENSOR_ERROR, true, 0.0f, 0.0f);
    } else {
        check_threshold(ALARM_SENSOR_ERROR, false, 0.0f, 0.0f);
        check_threshold(ALARM_VOC_HIGH,     data->voc_ppb       >= s_voc_alarm_ppb,
                        data->voc_ppb,       s_voc_alarm_ppb);
        check_threshold(ALARM_TEMP_HIGH,    data->temperature_c >= TEMP_HIGH_C,
                        data->temperature_c, TEMP_HIGH_C);
        check_threshold(ALARM_TEMP_LOW,     data->temperature_c <= TEMP_LOW_C,
                        data->temperature_c, TEMP_LOW_C);
        check_threshold(ALARM_HUM_HIGH,     data->humidity_pct  >= HUM_HIGH_PCT,
                        data->humidity_pct,  HUM_HIGH_PCT);
        check_threshold(ALARM_HUM_LOW,      data->humidity_pct  <= HUM_LOW_PCT,
                        data->humidity_pct,  HUM_LOW_PCT);
    }

    xSemaphoreGive(s_mutex);
}

int alarm_manager_active_count(void)
{
    if (!s_mutex) return 0;
    int cnt = 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < s_count; i++) {
        int idx = (s_head + i) % ALARM_MAX_COUNT;
        if (s_history[idx].active && !s_history[idx].acknowledged) cnt++;
    }
    xSemaphoreGive(s_mutex);
    return cnt;
}

int alarm_manager_get_history(alarm_entry_t *buf, int max_cnt)
{
    if (!s_mutex) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int n = s_count < max_cnt ? s_count : max_cnt;
    /* Copy newest-first */
    for (int i = 0; i < n; i++) {
        int src = (s_head + s_count - 1 - i) % ALARM_MAX_COUNT;
        memcpy(&buf[i], &s_history[src], sizeof(alarm_entry_t));
    }
    xSemaphoreGive(s_mutex);
    return n;
}

void alarm_manager_acknowledge(int index)
{
    if (!s_mutex || index < 0 || index >= s_count) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int idx = (s_head + s_count - 1 - index) % ALARM_MAX_COUNT;
    s_history[idx].acknowledged = true;
    s_history[idx].active       = false;
    xSemaphoreGive(s_mutex);
}

void alarm_manager_acknowledge_all(void)
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < s_count; i++) {
        int idx = (s_head + i) % ALARM_MAX_COUNT;
        s_history[idx].acknowledged = true;
        s_history[idx].active       = false;
    }
    xSemaphoreGive(s_mutex);
}

void alarm_manager_clear_history(void)
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memset(s_history, 0, sizeof(s_history));
    s_count = 0;
    s_head  = 0;
    xSemaphoreGive(s_mutex);
}

const char *alarm_type_str(alarm_type_t type)
{
    switch (type) {
        case ALARM_VOC_HIGH:     return "VOC HIGH";
        case ALARM_TEMP_HIGH:    return "TEMP HIGH";
        case ALARM_TEMP_LOW:     return "TEMP LOW";
        case ALARM_HUM_HIGH:     return "HUMIDITY HIGH";
        case ALARM_HUM_LOW:      return "HUMIDITY LOW";
        case ALARM_SENSOR_ERROR: return "SENSOR ERROR";
        default:                 return "UNKNOWN";
    }
}
