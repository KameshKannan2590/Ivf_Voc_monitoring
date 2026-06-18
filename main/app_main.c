#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "display/display_driver.h"
#include "touch/touch_driver.h"
#include "lvgl_port/lvgl_port.h"
#include "ui/ui.h"
#include "sensors/sensor_manager.h"
#include "data/alarm_manager.h"

static const char *TAG = "main";

/* ── UI refresh task: feeds new sensor readings into the dashboard ── */
static void ui_refresh_task(void *arg)
{
    /* Give LVGL a moment to finish drawing the splash screen */
    vTaskDelay(pdMS_TO_TICKS(500));

    while (1) {
        ui_dashboard_refresh();
        vTaskDelay(pdMS_TO_TICKS(1000));   /* 1 Hz UI update */
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  IVF VOC Monitor  v1.0.0");
    ESP_LOGI(TAG, "  CrowPanel ESP32-S3 4.3\" 480x272");
    ESP_LOGI(TAG, "========================================");

    /* ── NVS (for settings persistence) ── */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition erased, re-initializing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    /* ── Display ── */
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(display_driver_init(&panel));

    /* ── Touch ── */
    xpt2046_handle_t *tp = NULL;
    ESP_ERROR_CHECK(touch_driver_init(&tp));

    /* ── LVGL port (registers display + touch, starts LVGL task) ── */
    ESP_ERROR_CHECK(lvgl_port_init(panel, tp));

    /* ── Alarm manager (before sensor_manager calls it) ── */
    ESP_ERROR_CHECK(alarm_manager_init());

    /* ── Sensor manager (starts 1 Hz sampling task) ── */
    ESP_ERROR_CHECK(sensor_manager_init());

    /* ── UI (builds all screens, starts splash animation) ── */
    ui_init();

    /* ── Background task to push sensor data to the dashboard ── */
    xTaskCreate(ui_refresh_task, "ui_refresh", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "System running");

    /* app_main may return — FreeRTOS idle task keeps the scheduler alive */
}
