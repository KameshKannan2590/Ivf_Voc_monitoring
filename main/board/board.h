#pragma once

/*
 * board.h — CrowPanel DIS06043H v2.1 GPIO pin map
 *
 * Centralises all GPIO assignments for the CrowPanel ESP32-S3 4.3" board.
 * Individual driver headers (display_driver.h, touch_driver.h) still define
 * their own local macros and are the authoritative source at compile time.
 * This file is the single reference for board-level pin documentation and
 * will become the authoritative source when drivers are refactored in Phase 7.
 *
 * Do not include this file in driver code yet — drivers continue to use their
 * own local defines.  Include it from app_main.c or board.c (Phase 7) only.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ── RGB LCD panel (ST7262, 16-bit parallel) ────────────────────────────── */
#define BOARD_LCD_DE        40
#define BOARD_LCD_VSYNC     41
#define BOARD_LCD_HSYNC     39
#define BOARD_LCD_PCLK      42
#define BOARD_LCD_BL        2    /* backlight — GPIO HIGH/LOW (PWM in Phase 6) */
/* BOARD_LCD_AUX (GPIO38) removed Phase 6.1 — carried over from the generic
 * CrowPanel reference example, but confirmed against this board's own
 * schematic to not be required; freed for the I2C bus below (SCL). See
 * WORKLOG.md Phase 6.1 for the removal rationale. */

/* RGB data bus (16-bit) */
#define BOARD_LCD_D0        8
#define BOARD_LCD_D1        3
#define BOARD_LCD_D2        46
#define BOARD_LCD_D3        9
#define BOARD_LCD_D4        1
#define BOARD_LCD_D5        5
#define BOARD_LCD_D6        6
#define BOARD_LCD_D7        7
#define BOARD_LCD_D8        15
#define BOARD_LCD_D9        16
#define BOARD_LCD_D10       4
#define BOARD_LCD_D11       45
#define BOARD_LCD_D12       48
#define BOARD_LCD_D13       47
#define BOARD_LCD_D14       21
#define BOARD_LCD_D15       14

/* ── Resistive touch controller (XPT2046, SPI2) ────────────────────────── */
#define BOARD_TOUCH_SCK     12
#define BOARD_TOUCH_MISO    13
#define BOARD_TOUCH_MOSI    11
#define BOARD_TOUCH_CS      0
#define BOARD_TOUCH_INT     36

/* ── I2C bus (sensors) ───────────────────────────────────────────────────
 * Phase 6.1: wired up for the SHT41 temperature/humidity sensor. This is
 * the board's "GPIO_D" expansion header (HY2.0-4P) — per Elecrow's own
 * documentation, IO37/IO38 "can be used to simulate UART or IIC". GPIO38
 * was previously reserved by this project as LCD_AUX (drive-HIGH); removed
 * from display_driver.c after confirming against the board schematic that
 * it isn't required, freeing it for I2C use here.
 * sht41.h/.c define these same values locally (per this project's
 * convention of driver headers owning their own pin macros); board.h
 * remains the documentation reference.
 * SDA/SCL assignment (which of IO37/IO38 is which) is this project's own
 * convention, not something Elecrow's docs specify — confirm on first
 * bring-up and swap if the sensor doesn't ACK. */
#define BOARD_I2C_SDA       37
#define BOARD_I2C_SCL       38

#ifdef __cplusplus
}
#endif
