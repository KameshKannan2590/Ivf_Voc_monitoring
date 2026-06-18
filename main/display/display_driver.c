#include "display_driver.h"

#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "display";

/*
 * RGB565 data bus pin order: D[0..15] = B[0..4], G[0..5], R[0..4]
 * Mapping from CrowPanel DIS06043H v2.1 schematic:
 *   B0=8, B1=3, B2=46, B3=9,  B4=1
 *   G0=5, G1=6, G2=7,  G3=15, G4=16, G5=4
 *   R0=45,R1=48,R2=47, R3=21, R4=14
 */
static const int LCD_DATA_GPIO[16] = {
    8, 3, 46, 9, 1,          /* B0..B4 */
    5, 6, 7, 15, 16, 4,      /* G0..G5 */
    45, 48, 47, 21, 14       /* R0..R4 */
};

esp_err_t display_driver_init(esp_lcd_panel_handle_t *out_panel)
{
    ESP_LOGI(TAG, "Initializing RGB LCD 480x272");

    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width            = 16,
        .bits_per_pixel        = 16,
        /* Framebuffer in PSRAM (board has 2MB OPI PSRAM — N4R2 variant).
         * 64-byte DMA alignment required for PSRAM transfers on ESP32-S3. */
        .psram_trans_align     = 64,
        .num_fbs               = 1,
        .clk_src               = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num         = GPIO_NUM_NC,
        .pclk_gpio_num         = LCD_PCLK_GPIO,
        .vsync_gpio_num        = LCD_VSYNC_GPIO,
        .hsync_gpio_num        = LCD_HSYNC_GPIO,
        .de_gpio_num           = LCD_DE_GPIO,
        .data_gpio_nums        = {
            LCD_DATA_GPIO[0],  LCD_DATA_GPIO[1],  LCD_DATA_GPIO[2],
            LCD_DATA_GPIO[3],  LCD_DATA_GPIO[4],  LCD_DATA_GPIO[5],
            LCD_DATA_GPIO[6],  LCD_DATA_GPIO[7],  LCD_DATA_GPIO[8],
            LCD_DATA_GPIO[9],  LCD_DATA_GPIO[10], LCD_DATA_GPIO[11],
            LCD_DATA_GPIO[12], LCD_DATA_GPIO[13], LCD_DATA_GPIO[14],
            LCD_DATA_GPIO[15],
        },
        .timings = {
            .pclk_hz            = LCD_PCLK_HZ,
            .h_res              = DISPLAY_WIDTH,
            .v_res              = DISPLAY_HEIGHT,
            .hsync_back_porch   = LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch  = LCD_HSYNC_FRONT_PORCH,
            .hsync_pulse_width  = LCD_HSYNC_PULSE_WIDTH,
            .vsync_back_porch   = LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch  = LCD_VSYNC_FRONT_PORCH,
            .vsync_pulse_width  = LCD_VSYNC_PULSE_WIDTH,
            .flags = {
                .pclk_active_neg = 1,   /* CrowPanel panel requires inverted PCLK */
            },
        },
        .flags = {
            .fb_in_psram = 1,   /* framebuffer in PSRAM (board has OPI PSRAM, 261KB fits easily) */
        },
    };

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_rgb_panel(&panel_config, out_panel),
        TAG, "Failed to create RGB panel"
    );
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_reset(*out_panel),
        TAG, "Panel reset failed"
    );
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_init(*out_panel),
        TAG, "Panel init failed"
    );
    /* Hardware portrait rotation: swap X/Y axes then mirror Y.
     * Equivalent to 270° CW (left physical edge = portrait top).
     * LVGL renders at 272×480 logical; draw_bitmap maps to 480×272 physical. */
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_swap_xy(*out_panel, true),
        TAG, "Panel swap_xy failed"
    );
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_mirror(*out_panel, false, true),
        TAG, "Panel mirror_y failed"
    );

    /* Aux GPIO required by CrowPanel hardware */
    gpio_config_t aux_cfg = {
        .pin_bit_mask = (1ULL << LCD_AUX_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&aux_cfg);
    gpio_set_level(LCD_AUX_GPIO, 1);

    display_set_backlight(true);

    ESP_LOGI(TAG, "RGB LCD ready");
    return ESP_OK;
}

void display_set_backlight(bool on)
{
    gpio_config_t bl_cfg = {
        .pin_bit_mask = (1ULL << LCD_BL_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(LCD_BL_GPIO, on ? 1 : 0);
}
