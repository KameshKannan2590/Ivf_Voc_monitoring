#include "sht41.h"

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "sht41";

/* ── I2C bus — CrowPanel DIS06043H "GPIO_D" expansion header (HY2.0-4P) ─────
 * IO37/IO38 — Elecrow's own documentation lists these as usable to
 * "simulate UART or IIC". GPIO38 was previously reserved by this project as
 * an LCD "AUX" pin (drive-HIGH); confirmed against the board schematic to
 * not be required and removed from display_driver.c to free it for I2C.
 * SDA/SCL assignment below is this project's own convention (Elecrow's docs
 * don't specify polarity) — confirm on first bring-up, swap if unresponsive. */
#define SHT41_I2C_PORT       I2C_NUM_0
#define SHT41_I2C_SDA_GPIO   37
#define SHT41_I2C_SCL_GPIO   38
#define SHT41_I2C_FREQ_HZ    100000    /* 100 kHz standard mode */
#define SHT41_I2C_ADDR       0x44      /* SHT4x default (ADR pin low) */

/* ── Sensirion SHT4x command set (datasheet §4.4) ───────────────────────── */
#define SHT41_CMD_MEASURE_HIGH_REP   0xFD   /* high repeatability, no heater */
#define SHT41_CMD_SOFT_RESET         0x94

#define SHT41_MEASURE_WAIT_MS   10   /* high-rep conversion: 8.3 ms max + margin */
#define SHT41_RESET_WAIT_MS      1   /* soft reset: <=1 ms typical */
#define SHT41_READ_RETRIES       3
#define SHT41_I2C_TIMEOUT_MS    50

static bool s_i2c_ready = false;

/* Installs the I2C master driver on SHT41_I2C_PORT if not already installed.
 * Tolerates ESP_ERR_INVALID_STATE so a future sensor driver can share this
 * same bus/port without a double-install failure. */
static esp_err_t i2c_bus_ensure_init(void)
{
    if (s_i2c_ready) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing I2C master: I2C%d SDA=GPIO%d SCL=GPIO%d %d Hz",
             SHT41_I2C_PORT, SHT41_I2C_SDA_GPIO, SHT41_I2C_SCL_GPIO, SHT41_I2C_FREQ_HZ);

    i2c_config_t cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = SHT41_I2C_SDA_GPIO,
        .scl_io_num       = SHT41_I2C_SCL_GPIO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = SHT41_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(SHT41_I2C_PORT, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config() failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(SHT41_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "I2C%d driver already installed — reusing existing bus", SHT41_I2C_PORT);
        err = ESP_OK;   /* bus already installed — shared with another driver */
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install() failed: %s", esp_err_to_name(err));
        return err;
    }

    s_i2c_ready = true;
    ESP_LOGI(TAG, "I2C master initialized successfully");
    return ESP_OK;
}

/* Sensirion CRC-8: polynomial 0x31 (x^8+x^5+x^4+1), init 0xFF. */
static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t sht41_send_cmd(uint8_t cmd)
{
    return i2c_master_write_to_device(SHT41_I2C_PORT, SHT41_I2C_ADDR,
                                       &cmd, 1,
                                       pdMS_TO_TICKS(SHT41_I2C_TIMEOUT_MS));
}

static esp_err_t sht41_read_once(float *temperature_c, float *humidity_percent)
{
    esp_err_t err = sht41_send_cmd(SHT41_CMD_MEASURE_HIGH_REP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Measurement command (0x%02X) failed: %s",
                 SHT41_CMD_MEASURE_HIGH_REP, esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(SHT41_MEASURE_WAIT_MS));

    uint8_t raw[6] = {0};
    err = i2c_master_read_from_device(SHT41_I2C_PORT, SHT41_I2C_ADDR,
                                       raw, sizeof(raw),
                                       pdMS_TO_TICKS(SHT41_I2C_TIMEOUT_MS));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read of measurement result failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t t_crc_calc = crc8(&raw[0], 2);
    uint8_t h_crc_calc = crc8(&raw[3], 2);
    if (t_crc_calc != raw[2]) {
        ESP_LOGE(TAG, "CRC validation FAILED (temperature): raw=%02X %02X %02X calc=0x%02X recv=0x%02X",
                 raw[0], raw[1], raw[2], t_crc_calc, raw[2]);
        return ESP_ERR_INVALID_CRC;
    }
    if (h_crc_calc != raw[5]) {
        ESP_LOGE(TAG, "CRC validation FAILED (humidity): raw=%02X %02X %02X calc=0x%02X recv=0x%02X",
                 raw[3], raw[4], raw[5], h_crc_calc, raw[5]);
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t t_ticks  = ((uint16_t)raw[0] << 8) | raw[1];
    uint16_t rh_ticks = ((uint16_t)raw[3] << 8) | raw[4];

    /* Sensirion SHT4x conversion formulas (datasheet §4.6) */
    float t  = -45.0f + 175.0f * ((float)t_ticks  / 65535.0f);
    float rh =  -6.0f + 125.0f * ((float)rh_ticks / 65535.0f);
    if (rh < 0.0f)   rh = 0.0f;
    if (rh > 100.0f) rh = 100.0f;

    ESP_LOGI(TAG, "SHT41 read OK: CRC valid, temperature=%.2f C, humidity=%.2f %%RH", t, rh);

    *temperature_c    = t;
    *humidity_percent = rh;
    return ESP_OK;
}

esp_err_t sht41_init(void)
{
    esp_err_t err = i2c_bus_ensure_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SHT41 init aborted — I2C bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Probing SHT41 at address 0x%02X (soft reset command 0x%02X)",
             SHT41_I2C_ADDR, SHT41_CMD_SOFT_RESET);
    err = sht41_send_cmd(SHT41_CMD_SOFT_RESET);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SHT41 NOT detected at address 0x%02X — soft reset failed: %s. "
                 "Check wiring (SDA=GPIO%d, SCL=GPIO%d) and sensor power. "
                 "sensor_manager will keep retrying every sample cycle.",
                 SHT41_I2C_ADDR, esp_err_to_name(err), SHT41_I2C_SDA_GPIO, SHT41_I2C_SCL_GPIO);
        return ESP_OK;   /* not fatal — sht41_read() retries every cycle regardless */
    }

    vTaskDelay(pdMS_TO_TICKS(SHT41_RESET_WAIT_MS));
    ESP_LOGI(TAG, "SHT41 detected and reset OK (I2C%d SDA=GPIO%d SCL=GPIO%d addr=0x%02X)",
             SHT41_I2C_PORT, SHT41_I2C_SDA_GPIO, SHT41_I2C_SCL_GPIO, SHT41_I2C_ADDR);
    return ESP_OK;
}

esp_err_t sht41_read(float *temperature_c, float *humidity_percent)
{
    if (!temperature_c || !humidity_percent) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= SHT41_READ_RETRIES; attempt++) {
        err = sht41_read_once(temperature_c, humidity_percent);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "read attempt %d/%d failed: %s",
                 attempt, SHT41_READ_RETRIES, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGE(TAG, "SHT41 read failed after %d attempts (last error: %s)",
             SHT41_READ_RETRIES, esp_err_to_name(err));
    return err;
}
