#include "lvgl_port.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "display/display_driver.h"
#include "display/display_power.h"
#include "ui.h"

#include "lvgl.h"
#include "esp_heap_caps.h"

static const char *TAG = "lvgl_port";

static SemaphoreHandle_t      s_lvgl_mutex = NULL;
static esp_lcd_panel_handle_t s_panel      = NULL;
static xpt2046_handle_t      *s_tp         = NULL;

/*
 * Full-frame draw buffer in PSRAM (480×272×2 = 261 KB).
 * Required for full_refresh=1 mode used with software rotation.
 * Allocated at init from PSRAM; the panel's own framebuffer is also in PSRAM.
 */
static lv_color_t *s_draw_buf = NULL;

/* -----------------------------------------------------------------------
 * Display flush callback
 * LVGL has already applied the LV_DISP_ROT_90 transform to color_map before
 * calling here — the buffer is in physical 480×272 layout.  Under full_refresh=1
 * area is always {0,0,479,271}.  Write the full frame to the PSRAM framebuffer.
 * ----------------------------------------------------------------------- */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              color_map);
    lv_disp_flush_ready(drv);
}

/* -----------------------------------------------------------------------
 * Touch read callback — called by LVGL every LV_INDEV_DEF_READ_PERIOD ms
 * ----------------------------------------------------------------------- */
static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    xpt2046_handle_t *tp = (xpt2046_handle_t *)drv->user_data;

    uint16_t x = 0, y = 0;
    bool pressed = touch_driver_read(tp, &x, &y);

    if (pressed && display_power_is_dimmed()) {
        /* This is the touch that wakes a dimmed screen — consume it
         * entirely (report released) rather than letting it also act on
         * whatever widget is underneath, same pattern as a phone lock
         * screen. The user has to tap again, on the now-lit screen, to
         * actually interact. */
        display_power_wake();
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (pressed) {
        display_power_notify_touch();
        /* touch_driver_read: *x = portrait_Y (0-479), *y = portrait_X (0-271).
         * LVGL ROT_NONE 272x480 expects point.x = portrait_X, point.y = portrait_Y. */
        lv_coord_t point_x = (lv_coord_t)y;
        lv_coord_t point_y = (lv_coord_t)x;

        /* Nav drawer lives on lv_layer_top(), which LVGL's own indev hit-test
         * is supposed to check ahead of the active screen — but that was not
         * holding up on real hardware: taps in the dimmed area beyond the
         * drawer's own width were still reaching whatever sat underneath on
         * the active screen. Rather than trust that layering, decide this
         * case ourselves before LVGL ever sees the press: swallow the touch
         * and close the drawer directly, so nothing behind it is ever
         * dispatched to. Taps within the drawer's own width fall through to
         * normal LVGL handling (nav items, drawer background). */
        if (ui_nav_drawer_is_open() && point_x >= IVF_DRAWER_W) {
            ui_nav_drawer_close_from_touch();
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }

        data->point.x = point_x;
        data->point.y = point_y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* -----------------------------------------------------------------------
 * LVGL timer task — core 1, calls lv_timer_handler every 5 ms
 * ----------------------------------------------------------------------- */
static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started on core %d", xPortGetCoreID());

    while (1) {
        /* Advance LVGL's internal tick counter.
         * Required because the managed-component LVGL uses CONFIG_LV_CONF_SKIP=y
         * (Kconfig mode), which disables LV_TICK_CUSTOM.  Without this call
         * animations, timeouts, and the splash auto-advance will not work. */
        lv_tick_inc(LVGL_TIMER_PERIOD_MS);

        if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(s_lvgl_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(LVGL_TIMER_PERIOD_MS));
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
esp_err_t lvgl_port_init(esp_lcd_panel_handle_t panel,
                         xpt2046_handle_t *tp)
{
    s_panel = panel;
    s_tp    = tp;

    s_lvgl_mutex = xSemaphoreCreateMutex();
    if (!s_lvgl_mutex) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return ESP_ERR_NO_MEM;
    }

    lv_init();

    /* Full-frame draw buffer in PSRAM (480×272×2 = 261 KB).
     * full_refresh=1 requires a buffer sized to the entire physical frame. */
    static lv_disp_draw_buf_t draw_buf;
    const size_t buf_pixels = DISPLAY_WIDTH * DISPLAY_HEIGHT;
    s_draw_buf = heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!s_draw_buf) {
        ESP_LOGE(TAG, "LVGL draw buffer alloc failed (%u bytes)",
                 (unsigned)(buf_pixels * sizeof(lv_color_t)));
        return ESP_ERR_NO_MEM;
    }
    lv_disp_draw_buf_init(&draw_buf, s_draw_buf, NULL, buf_pixels);

    /* Display driver — portrait logical resolution (272×480).
     * Hardware rotation (SWAP_XY|MIRROR_Y) in display_driver.c transposes to the
     * 480×272 physical panel.  full_refresh=1: entire frame per flush cycle. */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = DISPLAY_LOGICAL_W;
    disp_drv.ver_res      = DISPLAY_LOGICAL_H;
    disp_drv.rotated      = LVGL_ROTATION;
    disp_drv.full_refresh = 1;
    disp_drv.flush_cb     = lvgl_flush_cb;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.user_data    = panel;
    lv_disp_drv_register(&disp_drv);

    /* Touch input device */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type      = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb   = lvgl_touch_read_cb;
    indev_drv.user_data = tp;
    lv_indev_drv_register(&indev_drv);

    /* LVGL task pinned to APP core */
    BaseType_t ret = xTaskCreatePinnedToCore(
        lvgl_task, "lvgl",
        LVGL_TASK_STACK_SIZE, NULL,
        LVGL_TASK_PRIORITY, NULL,
        LVGL_TASK_CORE
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LVGL port initialized (logical %dx%d rot90, draw buf %u px, task %d ms)",
             DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H,
             (unsigned)buf_pixels, LVGL_TIMER_PERIOD_MS);
    return ESP_OK;
}

bool lvgl_port_lock(uint32_t timeout_ms)
{
    return xSemaphoreTake(s_lvgl_mutex,
                          timeout_ms == 0 ? portMAX_DELAY
                                          : pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    xSemaphoreGive(s_lvgl_mutex);
}
