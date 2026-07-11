#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sensirion SHT41 temperature/humidity driver — I2C, official Sensirion
 * command set (datasheet section 4.4). No LVGL/UI dependency; the caller
 * (sensor_manager.c) owns polling cadence and error-recovery policy.
 *
 * Initialize the I2C master bus (idempotent — safe to share the port with a
 * future sensor driver) and soft-reset the SHT41.
 *
 * @return ESP_OK on success. A failed soft reset is logged but does not fail
 *         init — sht41_read() will simply keep failing until the sensor
 *         responds, which the caller already handles.
 */
esp_err_t sht41_init(void);

/**
 * Trigger a high-repeatability measurement, wait for conversion, read back
 * the raw ticks, validate both CRC-8 checksums, and convert to engineering
 * units. Retries internally on I2C/CRC failure before giving up.
 *
 * @param[out] temperature_c      Degrees Celsius.
 * @param[out] humidity_percent   Relative humidity, 0-100 %.
 * @return ESP_OK on success. On failure *temperature_c / *humidity_percent
 *         are left untouched — the caller should keep its last-known value.
 */
esp_err_t sht41_read(float *temperature_c, float *humidity_percent);

#ifdef __cplusplus
}
#endif
