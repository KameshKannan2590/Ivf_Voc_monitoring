#include "sensor_backend.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

/* ── Phase 7: replace every TODO block with ENS160 + AHT21 I2C reads ── */

static const char *TAG = "sensor_hw";

void sensor_backend_init(void)
{
    /* TODO Phase 7:
     *   i2c_master_init();
     *   aht21_init();
     *   ens160_init();
     *   ens160_set_mode(ENS160_MODE_STANDARD);
     *   ens160_wait_for_ready();          // ~1 min warm-up
     */
    ESP_LOGW(TAG, "Hardware backend not yet implemented — sensor_ok=false");
}

void sensor_backend_sample(sensor_data_t *out)
{
    /* TODO Phase 7:
     *   aht21_read(&out->temperature_c, &out->humidity_pct);
     *   ens160_set_compensation(out->temperature_c, out->humidity_pct);
     *   out->sensor_ok = ens160_read_tvoc(&out->voc_ppb);
     */
    memset(out, 0, sizeof(*out));
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->sensor_ok    = false;   /* drives "ERROR" badge on dashboard */
}
