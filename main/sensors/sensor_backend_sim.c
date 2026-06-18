#include "sensor_backend.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "sensor_sim";

/* Simulation state — gentle sine-wave so the UI shows realistic trending */
static struct {
    float    voc_base;
    float    temp_base;
    float    hum_base;
    uint32_t tick;
} s_sim = {
    .voc_base  = 120.0f,
    .temp_base = 22.5f,
    .hum_base  = 48.0f,
    .tick      = 0,
};

void sensor_backend_init(void)
{
    ESP_LOGI(TAG, "Simulation backend active (sine-wave stub)");
}

void sensor_backend_sample(sensor_data_t *out)
{
    s_sim.tick++;
    float t = (float)s_sim.tick * 0.05f;

    out->voc_ppb       = s_sim.voc_base  + 40.0f * sinf(t * 0.7f)
                                         + 15.0f * sinf(t * 2.3f);
    out->temperature_c = s_sim.temp_base + 0.8f  * sinf(t * 0.3f);
    out->humidity_pct  = s_sim.hum_base  + 5.0f  * sinf(t * 0.5f);
    out->timestamp_ms  = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->sensor_ok     = true;

    /* Clamp to physically plausible ranges */
    if (out->voc_ppb      < 0.0f)   out->voc_ppb      = 0.0f;
    if (out->humidity_pct < 0.0f)   out->humidity_pct = 0.0f;
    if (out->humidity_pct > 100.0f) out->humidity_pct = 100.0f;
}
