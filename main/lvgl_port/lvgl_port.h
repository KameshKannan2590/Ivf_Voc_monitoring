#pragma once

#include "esp_lcd_panel_ops.h"
#include "touch/touch_driver.h"
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Portrait logical resolution — LVGL coordinate space.
 * Physical panel is 480×272 (landscape). Hardware rotation is applied by the
 * RGB panel driver (SWAP_XY + MIRROR_Y in display_driver.c) so that LVGL's
 * 272×480 portrait layout maps correctly when the device is held with the
 * LEFT physical edge up.  No LVGL software rotation is used.
 */
#define DISPLAY_LOGICAL_W   272   /* portrait logical width  (= physical height) */
#define DISPLAY_LOGICAL_H   480   /* portrait logical height (= physical width)  */

/* No LVGL software rotation — hardware rotation handles the transpose. */
#define LVGL_ROTATION       LV_DISP_ROT_NONE

/* FreeRTOS task for lv_timer_handler() — pinned to core 1 */
#define LVGL_TASK_STACK_SIZE    (8 * 1024)
#define LVGL_TASK_PRIORITY      2
#define LVGL_TASK_CORE          1
#define LVGL_TIMER_PERIOD_MS    5

/**
 * Initialize LVGL, register display and touch drivers, start the LVGL task.
 *
 * @param panel  Initialized RGB LCD panel handle (from display_driver_init).
 * @param tp     Initialized XPT2046 handle (from touch_driver_init).
 * @return ESP_OK on success.
 */
esp_err_t lvgl_port_init(esp_lcd_panel_handle_t panel,
                         xpt2046_handle_t *tp);

/**
 * Acquire LVGL mutex before calling any lv_* API from another task.
 * @param timeout_ms  Timeout ms; 0 = wait forever.
 * @return true if lock acquired.
 */
bool lvgl_port_lock(uint32_t timeout_ms);

/**
 * Release LVGL mutex (must follow every successful lvgl_port_lock call).
 */
void lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif
