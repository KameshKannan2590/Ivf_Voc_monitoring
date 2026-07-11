#pragma once

#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CrowPanel 4.3" DIS06043H — 480x272 RGB565 parallel panel */
#define DISPLAY_WIDTH       480
#define DISPLAY_HEIGHT      272

/* RGB panel GPIO — from CrowPanel v2.1 hardware schematic */
#define LCD_DE_GPIO         40
#define LCD_VSYNC_GPIO      41
#define LCD_HSYNC_GPIO      39
#define LCD_PCLK_GPIO       42
#define LCD_BL_GPIO         2

/* Pixel clock ~7 MHz (safe for long flex cable) */
#define LCD_PCLK_HZ         7000000

/* Horizontal timing (pixels) */
#define LCD_HSYNC_BACK_PORCH    43
#define LCD_HSYNC_FRONT_PORCH   8
#define LCD_HSYNC_PULSE_WIDTH   4

/* Vertical timing (lines) */
#define LCD_VSYNC_BACK_PORCH    12
#define LCD_VSYNC_FRONT_PORCH   8
#define LCD_VSYNC_PULSE_WIDTH   4

/**
 * Initialize the RGB LCD panel.
 * After this call the panel handle is ready; backlight is ON.
 *
 * @param[out] out_panel  Pointer to receive the panel handle.
 * @return ESP_OK on success.
 */
esp_err_t display_driver_init(esp_lcd_panel_handle_t *out_panel);

/**
 * Set display backlight brightness via LEDC PWM, 0-100 (%). 0 is fully off,
 * not just dim — there is no separate on/off API; callers that just want
 * "off" pass 0, "on" pass whatever brightness they want restored to.
 * Values above 100 are clamped. Lazily initializes the LEDC timer/channel
 * on GPIO LCD_BL_GPIO the first time it's called.
 */
void display_set_brightness(uint8_t percent);

#ifdef __cplusplus
}
#endif
