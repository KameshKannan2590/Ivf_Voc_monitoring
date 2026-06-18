#include "touch_driver.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <string.h>

static const char *TAG = "touch";

/* ── XPT2046 command bytes ──────────────────────────────────────────────────
 * Byte format: START(1) A2 A1 A0 MODE(0=12bit) SER/DFR(0=diff) PD1 PD0
 *   0xD0 = 1_101_0_0_00  → Y+ channel (gives X position in differential mode)
 *   0x90 = 1_001_0_0_00  → X+ channel (gives Y position in differential mode)
 *   0xB0 = 1_011_0_0_00  → Z1 pressure (used to detect touch)
 * PD=00 keeps the ADC powered on between conversions.
 * ────────────────────────────────────────────────────────────────────────── */
#define CMD_READ_X   0xD0
#define CMD_READ_Y   0x90
#define CMD_READ_Z1  0xB0

/* Touch detected when Z1 pressure ADC > this threshold (0-4095) */
#define Z1_TOUCH_THRESHOLD  50

/* Number of samples to average for noise reduction */
#define SAMPLE_COUNT  5

struct xpt2046_dev {
    spi_device_handle_t spi;
};

/* ── SPI helper: send 1-byte command, receive 2-byte (16-bit) ADC result ── */
static uint16_t xpt_read_channel(spi_device_handle_t spi, uint8_t cmd)
{
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = { 0 };

    spi_transaction_t t = {
        .length    = 24,          /* 8 cmd + 16 response bits */
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_polling_transmit(spi, &t);

    /*
     * Response layout in rx[1..2]:
     *   rx[1]: [null_bit][D11..D5]
     *   rx[2]: [D4..D0][000]
     * 12-bit result = combined >> 3, null_bit is always 0 so no masking needed.
     */
    return (uint16_t)(((rx[1] << 8) | rx[2]) >> 3);
}

/* ── Coordinate mapping — portrait mode (LV_DISP_ROT_270) ──────────────────
 * Physical CMD_READ_X channel (raw_x) covers the landscape horizontal axis.
 * Physical CMD_READ_Y channel (raw_y) covers the landscape vertical axis.
 *
 * After LV_DISP_ROT_270 (physical LEFT edge = portrait TOP):
 *   portrait X ← landscape Y  (raw_y)   inverted, TOUCH_OUT_X_MAX … 0
 *   portrait Y ← landscape X  (raw_x)   direct,   0 … TOUCH_OUT_Y_MAX
 *
 * map_x(raw_x) → portrait Y (direct landscape X)
 * map_y(raw_y) → portrait X (inverted landscape Y)
 * ────────────────────────────────────────────────────────────────────────── */
static uint16_t map_x(uint16_t raw)
{
    /* Landscape X → portrait Y (direct) */
    if (raw <= TOUCH_RAW_X_MIN) return 0;
    if (raw >= TOUCH_RAW_X_MAX) return TOUCH_OUT_Y_MAX;
    return (uint16_t)((uint32_t)(raw - TOUCH_RAW_X_MIN) * TOUCH_OUT_Y_MAX
                      / (TOUCH_RAW_X_MAX - TOUCH_RAW_X_MIN));
}

static uint16_t map_y(uint16_t raw)
{
    /* Landscape Y → portrait X (inverted) */
    if (raw <= TOUCH_RAW_Y_MIN) return TOUCH_OUT_X_MAX;
    if (raw >= TOUCH_RAW_Y_MAX) return 0;
    return (uint16_t)(TOUCH_OUT_X_MAX -
                      (uint32_t)(raw - TOUCH_RAW_Y_MIN) * TOUCH_OUT_X_MAX
                      / (TOUCH_RAW_Y_MAX - TOUCH_RAW_Y_MIN));
}

/* ── Public API ─────────────────────────────────────────────────────────── */
esp_err_t touch_driver_init(xpt2046_handle_t **out_handle)
{
    ESP_LOGI(TAG, "Initializing XPT2046 (SPI2 SCK=%d MISO=%d MOSI=%d CS=%d INT=%d)",
             TOUCH_SCK_GPIO, TOUCH_MISO_GPIO, TOUCH_MOSI_GPIO,
             TOUCH_CS_GPIO, TOUCH_INT_GPIO);

    /* SPI bus */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = TOUCH_MOSI_GPIO,
        .miso_io_num     = TOUCH_MISO_GPIO,
        .sclk_io_num     = TOUCH_SCK_GPIO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 16,
    };
    ESP_RETURN_ON_ERROR(
        spi_bus_initialize(TOUCH_SPI_HOST, &bus_cfg, SPI_DMA_DISABLED),
        TAG, "SPI2 bus init failed"
    );

    /* XPT2046 SPI device — Mode 0, CS managed by driver */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = TOUCH_SPI_CLK_HZ,
        .mode           = 0,            /* CPOL=0, CPHA=0 */
        .spics_io_num   = TOUCH_CS_GPIO,
        .queue_size     = 1,
        .pre_cb         = NULL,
        .post_cb        = NULL,
    };
    xpt2046_handle_t *dev = heap_caps_calloc(1, sizeof(xpt2046_handle_t),
                                              MALLOC_CAP_DEFAULT);
    if (!dev) return ESP_ERR_NO_MEM;

    ESP_RETURN_ON_ERROR(
        spi_bus_add_device(TOUCH_SPI_HOST, &dev_cfg, &dev->spi),
        TAG, "SPI device add failed"
    );

    /* INT pin — input, pull-up, goes LOW on touch */
    gpio_config_t int_cfg = {
        .pin_bit_mask = (1ULL << TOUCH_INT_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "INT GPIO config failed");

    *out_handle = dev;
    ESP_LOGI(TAG, "XPT2046 ready");
    return ESP_OK;
}

bool touch_driver_read(xpt2046_handle_t *handle, uint16_t *x, uint16_t *y)
{
    /* Fast exit: INT pin HIGH means no touch */
    if (gpio_get_level(TOUCH_INT_GPIO) != 0) {
        return false;
    }

    /* Confirm with Z1 pressure to reject noise on INT pin */
    uint16_t z1 = xpt_read_channel(handle->spi, CMD_READ_Z1);
    if (z1 < Z1_TOUCH_THRESHOLD) {
        return false;
    }

    /* Average SAMPLE_COUNT readings for each axis */
    uint32_t sum_x = 0, sum_y = 0;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        sum_x += xpt_read_channel(handle->spi, CMD_READ_X);
        sum_y += xpt_read_channel(handle->spi, CMD_READ_Y);
    }
    uint16_t raw_x = (uint16_t)(sum_x / SAMPLE_COUNT);
    uint16_t raw_y = (uint16_t)(sum_y / SAMPLE_COUNT);

    *x = map_x(raw_x);
    *y = map_y(raw_y);
    return true;
}
