#pragma once

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * XPT2046 SPI pins — CrowPanel DIS06043H v2.1
 */
#define TOUCH_SPI_HOST      SPI2_HOST
#define TOUCH_SPI_CLK_HZ    2000000   /* 2 MHz — XPT2046 max reliable speed */
#define TOUCH_SCK_GPIO      12
#define TOUCH_MISO_GPIO     13
#define TOUCH_MOSI_GPIO     11
#define TOUCH_CS_GPIO       0
#define TOUCH_INT_GPIO      36

/*
 * Raw ADC calibration for XPT2046 on CrowPanel DIS06043H v2.1.
 * These constants describe the physical sensor ADC range and do NOT change
 * with display orientation — they are hardware properties of the resistive panel.
 *   CMD_READ_X channel: raw 200 → landscape left (0),  raw 4000 → landscape right (479)
 *   CMD_READ_Y channel: raw 200 → landscape top  (0),  raw 3600 → landscape bottom (271)
 *
 * In portrait mode (LV_DISP_ROT_90) the mapping applied by map_x() / map_y() is:
 *   *x = map_y(raw_y)          → portrait X: 0 … TOUCH_OUT_X_MAX (271)
 *   *y = TOUCH_OUT_Y_MAX
 *         - map_x(raw_x)       → portrait Y: 0 … TOUCH_OUT_Y_MAX (479)
 * If LV_DISP_ROT_270 is used instead, swap the inversion.
 */
#define TOUCH_RAW_X_MIN   200
#define TOUCH_RAW_X_MAX   4000
#define TOUCH_RAW_Y_MIN   200
#define TOUCH_RAW_Y_MAX   3600

/* Output coordinate ranges reported by touch_driver_read() in portrait mode */
#define TOUCH_OUT_X_MAX   271   /* portrait logical width  - 1 */
#define TOUCH_OUT_Y_MAX   479   /* portrait logical height - 1 */

/* Opaque handle returned by touch_driver_init */
typedef struct xpt2046_dev xpt2046_handle_t;

/**
 * Initialize XPT2046: set up SPI2 bus + device, configure INT GPIO.
 *
 * @param[out] out_handle  Pointer to receive the allocated handle.
 * @return ESP_OK on success.
 */
esp_err_t touch_driver_init(xpt2046_handle_t **out_handle);

/**
 * Read a single touch sample (call from the LVGL input-device callback).
 * Returns the calibrated screen coordinates via x/y.
 *
 * @param handle  Handle from touch_driver_init.
 * @param x       Portrait logical X (0 … TOUCH_OUT_X_MAX = 271).
 * @param y       Portrait logical Y (0 … TOUCH_OUT_Y_MAX = 479).
 * @return true if the screen is currently being touched.
 */
bool touch_driver_read(xpt2046_handle_t *handle, uint16_t *x, uint16_t *y);

#ifdef __cplusplus
}
#endif
