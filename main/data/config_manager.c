#include "config_manager.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "config_mgr";

#define NVS_NAMESPACE "ivf_cfg"

/* ── Defaults ─────────────────────────────────────────────────────────────
 * voc_warn's default (250) is intentionally NOT the pre-Settings hardcoded
 * value (150) — 150 doesn't fall on the {0,250,500,750,1000} dropdown this
 * screen exposes, so the nearest allowed value is used instead. Anyone who
 * already has "voc_warn"=150 saved in NVS keeps that value (only a fresh/
 * erased NVS falls back to this default). */
#define DEFAULT_BRIGHTNESS_PCT   70
#define DEFAULT_TIMEOUT_SEC      30
#define DEFAULT_DISPLAY_THRESH   500
#define DEFAULT_DISPLAY_MAX      1000
#define DEFAULT_VOC_WARN_PPB     250
#define DEFAULT_VOC_ALARM_PPB    500
#define DEFAULT_DARK_MODE        0

typedef struct {
    uint8_t  brightness_pct;
    uint16_t timeout_sec;
    int32_t  display_threshold_ppb;
    int32_t  display_max_ppb;
    int32_t  voc_warn_ppb;
    int32_t  voc_alarm_ppb;
    bool     dark_mode;
} app_config_t;

static app_config_t s_cfg;

static int32_t nvs_get_i32_or(nvs_handle_t h, const char *key, int32_t fallback)
{
    int32_t v;
    return (nvs_get_i32(h, key, &v) == ESP_OK) ? v : fallback;
}

static void nvs_set_i32_checked(const char *key, int32_t value)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS open for write failed (key=%s)", key);
        return;
    }
    nvs_set_i32(h, key, value);
    nvs_commit(h);
    nvs_close(h);
}

esp_err_t config_manager_init(void)
{
    s_cfg.brightness_pct         = DEFAULT_BRIGHTNESS_PCT;
    s_cfg.timeout_sec            = DEFAULT_TIMEOUT_SEC;
    s_cfg.display_threshold_ppb  = DEFAULT_DISPLAY_THRESH;
    s_cfg.display_max_ppb        = DEFAULT_DISPLAY_MAX;
    s_cfg.voc_warn_ppb           = DEFAULT_VOC_WARN_PPB;
    s_cfg.voc_alarm_ppb          = DEFAULT_VOC_ALARM_PPB;
    s_cfg.dark_mode              = DEFAULT_DARK_MODE;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "No saved settings yet, using defaults");
        return ESP_OK;
    }

    s_cfg.brightness_pct        = (uint8_t)nvs_get_i32_or(h, "bright_pct", DEFAULT_BRIGHTNESS_PCT);
    if (s_cfg.brightness_pct < CONFIG_DIM_BRIGHTNESS_PCT) s_cfg.brightness_pct = CONFIG_DIM_BRIGHTNESS_PCT;
    s_cfg.timeout_sec           = (uint16_t)nvs_get_i32_or(h, "timeout_s", DEFAULT_TIMEOUT_SEC);
    s_cfg.display_threshold_ppb = nvs_get_i32_or(h, "disp_thr",  DEFAULT_DISPLAY_THRESH);
    s_cfg.display_max_ppb       = nvs_get_i32_or(h, "disp_max",  DEFAULT_DISPLAY_MAX);
    s_cfg.voc_warn_ppb          = nvs_get_i32_or(h, "voc_warn",  DEFAULT_VOC_WARN_PPB);
    s_cfg.voc_alarm_ppb         = nvs_get_i32_or(h, "voc_alarm", DEFAULT_VOC_ALARM_PPB);
    s_cfg.dark_mode             = nvs_get_i32_or(h, "dark_mode", DEFAULT_DARK_MODE) != 0;

    nvs_close(h);
    ESP_LOGI(TAG, "Settings loaded: brightness=%u%% timeout=%us disp_thr=%ld disp_max=%ld voc_warn=%ld voc_alarm=%ld dark_mode=%d",
             s_cfg.brightness_pct, s_cfg.timeout_sec,
             (long)s_cfg.display_threshold_ppb, (long)s_cfg.display_max_ppb,
             (long)s_cfg.voc_warn_ppb, (long)s_cfg.voc_alarm_ppb, (int)s_cfg.dark_mode);
    return ESP_OK;
}

uint8_t config_manager_get_brightness_pct(void) { return s_cfg.brightness_pct; }
void config_manager_set_brightness_pct(uint8_t pct)
{
    if (pct > 100) pct = 100;
    if (pct < CONFIG_DIM_BRIGHTNESS_PCT) pct = CONFIG_DIM_BRIGHTNESS_PCT;
    s_cfg.brightness_pct = pct;
    nvs_set_i32_checked("bright_pct", pct);
}

uint16_t config_manager_get_timeout_sec(void) { return s_cfg.timeout_sec; }
void config_manager_set_timeout_sec(uint16_t sec)
{
    s_cfg.timeout_sec = sec;
    nvs_set_i32_checked("timeout_s", sec);
}

int32_t config_manager_get_display_threshold_ppb(void) { return s_cfg.display_threshold_ppb; }
void config_manager_set_display_threshold_ppb(int32_t ppb)
{
    s_cfg.display_threshold_ppb = ppb;
    nvs_set_i32_checked("disp_thr", ppb);
}

int32_t config_manager_get_display_max_ppb(void) { return s_cfg.display_max_ppb; }
void config_manager_set_display_max_ppb(int32_t ppb)
{
    s_cfg.display_max_ppb = ppb;
    nvs_set_i32_checked("disp_max", ppb);
}

int32_t config_manager_get_voc_warn_ppb(void) { return s_cfg.voc_warn_ppb; }
void config_manager_set_voc_warn_ppb(int32_t ppb)
{
    s_cfg.voc_warn_ppb = ppb;
    nvs_set_i32_checked("voc_warn", ppb);
}

int32_t config_manager_get_voc_alarm_ppb(void) { return s_cfg.voc_alarm_ppb; }
void config_manager_set_voc_alarm_ppb(int32_t ppb)
{
    s_cfg.voc_alarm_ppb = ppb;
    nvs_set_i32_checked("voc_alarm", ppb);
}

bool config_manager_get_dark_mode(void) { return s_cfg.dark_mode; }
void config_manager_set_dark_mode(bool dark)
{
    s_cfg.dark_mode = dark;
    nvs_set_i32_checked("dark_mode", dark ? 1 : 0);
}
