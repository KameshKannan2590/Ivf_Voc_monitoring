#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Unified sensor reading snapshot.
 * All fields are populated each sensor cycle; fields that are unsupported
 * on the fitted sensor will retain 0.0f and sensor_ok = false per channel.
 */
typedef struct {
    float    voc_ppb;           /* Total VOC concentration (ppb)   */
    float    temperature_c;     /* Ambient temperature (°C)        */
    float    humidity_pct;      /* Relative humidity (%)           */
    uint32_t timestamp_ms;      /* esp_timer tick when sampled      */
    bool     sensor_ok;         /* false = sensor comm error        */
} sensor_data_t;

/**
 * Classification of current VOC level for UI color coding.
 */
typedef enum {
    SENSOR_LEVEL_GOOD    = 0,   /* Below warning threshold          */
    SENSOR_LEVEL_WARNING = 1,   /* Between warning and alarm        */
    SENSOR_LEVEL_DANGER  = 2,   /* At or above alarm threshold      */
    SENSOR_LEVEL_ERROR   = 3,   /* Sensor not responding            */
} sensor_level_t;

/**
 * Initialize sensor hardware and start background sampling task.
 * Currently uses a simulated stub; replace voc_sensor_stub.c with
 * a real driver (SGP30 / BME688 / ENS160) without changing this API.
 *
 * @return ESP_OK on success.
 */
esp_err_t sensor_manager_init(void);

/**
 * Get the latest sensor snapshot (thread-safe copy).
 * May be called from any task including the LVGL task.
 */
void sensor_manager_get_data(sensor_data_t *out);

/**
 * Classify VOC level against current thresholds.
 */
sensor_level_t sensor_get_voc_level(float voc_ppb);

/**
 * Classify temperature level against current thresholds.
 */
sensor_level_t sensor_get_temp_level(float temp_c);

/**
 * Classify humidity level against current thresholds.
 */
sensor_level_t sensor_get_hum_level(float hum_pct);

#ifdef __cplusplus
}
#endif
