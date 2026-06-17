# IVF VOC Environmental Monitoring System

Embedded HMI application for monitoring Volatile Organic Compound (VOC), temperature, and humidity levels in an IVF laboratory environment. Runs on CrowPanel ESP32-S3 4.3" HMI Display (480×272 RGB).

---

## Hardware

| Item | Detail |
|------|--------|
| Module | CrowPanel DIS06043H v2.1 |
| MCU | ESP32-S3 N4R2 (4 MB flash, 2 MB QSPI PSRAM) |
| Display | 4.3" RGB 480×272, ST7262 RGB-parallel driver |
| Touch | XPT2046 resistive touch controller (SPI) |
| Flash storage | NVS partition (threshold persistence) |
| Target sensors | VOC + Temperature + Humidity (see §Real Sensor Integration) |

**Touch calibration** (`touch/touch_driver.h`):

```
Raw X 200 → screen X 0   (left edge)
Raw X 4000 → screen X 479 (right edge)
Raw Y 200 → screen Y 0   (top edge)
Raw Y 3600 → screen Y 271 (bottom edge)
```

---

## Toolchain & Dependencies

| Component | Version |
|-----------|---------|
| ESP-IDF | 5.3.1 |
| LVGL | 8.3.x (managed component, `lvgl__lvgl`) |
| Target chip | esp32s3 |
| Config | `CONFIG_LV_CONF_SKIP=1` — all LVGL config via Kconfig/sdkconfig |

---

## Project Structure

```
main/
├── app_main.c                  Entry point — task creation, init sequence
├── lvgl_port/
│   ├── lvgl_port.c/.h          LVGL tick, task, mutex (core 1, 40 Hz)
│   └── lv_conf.h               LVGL feature config (included by Kconfig skip)
├── display/
│   └── display_driver.c/.h     ST7262 RGB-parallel init; DMA flush callback
├── touch/
│   └── touch_driver.c/.h       XPT2046 SPI driver; X/Y raw→pixel mapping
├── sensors/
│   └── sensor_manager.c/.h     Sensor task (core 0); data struct; level API
├── data/
│   └── alarm_manager.c/.h      Threshold checks; ring-buffer alarm history
└── ui/
    ├── ui.c/.h                  Screen registry; ui_goto_screen(); refresh
    └── screens/
        ├── screen_splash.c/.h   Boot splash + progress animation
        ├── screen_dashboard.c/.h Home screen — VOC tile + env cards + alarm badge
        ├── screen_voc_detail.c/.h 60-point trend chart + min/max/avg stats
        ├── screen_alarms.c/.h   Scrollable alarm history list; ACK ALL / CLEAR
        └── screen_settings.c/.h Threshold spinboxes; NVS save
```

---

## Software Architecture

### Layer diagram

```
┌─────────────────────────────────────────────────────────────────┐
│  Screens  (screen_*.c)   — LVGL widget trees, event callbacks   │
├─────────────────────────────────────────────────────────────────┤
│  UI core  (ui.c)         — screen registry, navigation, refresh │
├───────────────────────────┬─────────────────────────────────────┤
│  Alarm manager            │  Sensor manager                     │
│  (alarm_manager.c)        │  (sensor_manager.c)                 │
│  Debounced threshold      │  1 Hz sampling task (core 0)        │
│  checks → ring buffer     │  Mutex-protected sensor_data_t      │
├───────────────────────────┴─────────────────────────────────────┤
│  LVGL port  (lvgl_port.c) — 40 Hz handler task, core 1, mutex  │
├─────────────────────────┬───────────────────────────────────────┤
│  Display driver          │  Touch driver                        │
│  (display_driver.c)      │  (touch_driver.c)                    │
│  ST7262 RGB-parallel     │  XPT2046 SPI, calibrated X/Y map     │
└─────────────────────────┴───────────────────────────────────────┘
```

### FreeRTOS tasks

| Task | Core | Priority | Interval | Function |
|------|------|----------|----------|----------|
| `lvgl_task` | 1 | 4 | `lv_timer_handler()` at ~40 Hz | All LVGL rendering |
| `sensor_task` | 0 | 3 | 1000 ms | Sample sensors, feed alarm_manager |
| `ui_refresh_task` | 0 | 2 | 1000 ms | Call `ui_dashboard_refresh()` |

The LVGL mutex (`lvgl_port_lock / unlock`) gates all widget access from tasks outside `lvgl_task`.

### Navigation

`ui_goto_screen(target, forward)` — calls `lv_scr_load_anim` with MOVE_LEFT (forward) or MOVE_RIGHT (backward), 200 ms. Screens are pre-built at boot and never destroyed (`delete_old = false`).

On entry to each screen, data is immediately refreshed:
- `SCREEN_VOC_DETAIL` → `screen_voc_detail_update()`
- `SCREEN_ALARMS` → `screen_alarms_refresh()`
- `SCREEN_DASHBOARD` → refreshed every second by `ui_refresh_task`

### Data flow

```
sensor_task  →  sensor_data_t (mutex)  →  alarm_manager_check()
                     │                          │
                     ▼                          ▼
            screen_dashboard_update()    alarm_manager ring buffer
            screen_voc_detail_update()   screen_alarms_refresh()
```

### Settings persistence (NVS)

Namespace `ivf_cfg`, all keys `int32_t`:

| Key | Meaning | Unit |
|-----|---------|------|
| `voc_warn` | VOC warning threshold | ppb |
| `voc_alarm` | VOC alarm threshold | ppb |
| `tmp_warn` | Temperature warning | °C |
| `tmp_alarm` | Temperature alarm | °C |
| `hum_lo` | Humidity low threshold | % RH |
| `hum_hi` | Humidity high threshold | % RH |

Thresholds are loaded at `sensor_manager_init()` and used for both level colouring and alarm generation.

---

## Key LVGL Implementation Notes

These were discovered through hardware testing and are non-obvious.

### Resistive touch — scroll vs click

- `lv_obj_create()` sets `LV_OBJ_FLAG_SCROLLABLE` by default. Every container (header, nav bar, row) that should not scroll must explicitly call `lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE)`, otherwise touch gestures are consumed by the container and button click events are never delivered.
- Use `LV_EVENT_PRESSED` (fires at touch-down) for small control buttons, not `LV_EVENT_CLICKED` (requires clean press-release without movement). Resistive touch has inherent jitter that prevents clean releases.
- After a button press, call `lv_indev_wait_release(lv_indev_get_act())` to consume the remainder of the touch. Without this, micro-jitter after press causes the parent scrollable container to scroll 1–2 pixels, making content visually jump.
- Clear `LV_OBJ_FLAG_SCROLL_ELASTIC` and `LV_OBJ_FLAG_SCROLL_MOMENTUM` on scrollable content areas to prevent spring-back and throw animations.
- Use `lv_obj_set_ext_click_area(btn, 10)` on small buttons to extend the hit zone 10 px in all directions.

### Spinbox cursor highlight

LVGL always renders a coloured cursor on the active digit of a spinbox. To suppress it:

```c
lv_obj_set_style_bg_opa(sb, LV_OPA_TRANSP, LV_PART_CURSOR);
lv_obj_set_style_text_color(sb, IVF_COLOR_TEXT, LV_PART_CURSOR);
```

### Touch X-axis calibration

The XPT2046 raw X axis on this panel is **not inverted**: low raw value = left edge. The `map_x()` function uses the standard formula `(raw - MIN) * 479 / (MAX - MIN)`. If the X axis appears mirrored, check `TOUCH_RAW_X_MIN/MAX` values — do not invert the formula.

---

## Screens

### Dashboard (`screen_dashboard.c`)

- Left panel (260×202): large VOC value tile with colour-coded level (GOOD/WARNING/DANGER), alarm badge showing unacknowledged count.
- Right panel (202×202): two env cards stacked — Temperature (°C) and Humidity (% RH), values center-aligned.
- Tapping the VOC tile navigates to VOC Detail; tapping the alarm badge navigates to Alarms.
- Bottom nav bar: Settings gear button.

### VOC Detail (`screen_voc_detail.c`)

- 60-point scrolling line chart (1 Hz) showing ppb history, Y-range 0–600.
- Current value large label with GOOD/WARNING/DANGER colour and status badge.
- Stats row: MIN / AVG / MAX computed from the ring buffer.
- Pre-filled with 60 simulated data points at startup for visual demonstration.

### Alarms (`screen_alarms.c`)

- Scrollable list of alarm history entries (newest first), up to 32 entries.
- Each row: icon (⚠ active / ✓ acknowledged), type, measured value, threshold, uptime timestamp.
- Active alarms shown in danger colour; acknowledged in good colour.
- Bottom bar: ACK ALL and CLEAR buttons.
- When alarm history is empty, shows 4 hardcoded demonstration rows (POC mode).

### Settings (`screen_settings.c`)

- Six threshold rows: VOC Warn, VOC Alarm, Temp Warn, Temp Alarm, Humidity Low, Humidity High.
- Each row: label + spinbox + − and + buttons (36×30 px, 10 px extended click area).
- Values saved to NVS on SAVE; loaded from NVS at boot by `sensor_manager_init()`.

---

## Real Sensor Integration

The system currently runs in **simulation mode** inside `sensor_manager.c`. All code outside `sensor_manager.c` is sensor-agnostic and requires no changes for real hardware.

### What to change

#### 1. `main/sensors/sensor_manager.c`

This is the **only file** that needs modification for real sensors.

Remove the simulation block and replace with actual driver calls:

```c
// REMOVE these:
typedef struct { float voc_base; float temp_base; float hum_base; uint32_t tick; } sim_state_t;
static sim_state_t s_sim = { ... };
static void sim_sample(sensor_data_t *d) { ... }

// REPLACE sensor_task() inner loop with:
static void sensor_task(void *arg)
{
    // one-time sensor init (I2C addresses, warm-up delay)
    // e.g., sgp30_iaq_init(&s_sgp30);
    // vTaskDelay(pdMS_TO_TICKS(15000));  // SGP30 needs 15 s warm-up

    while (1) {
        sensor_data_t fresh = {0};

        // Read VOC
        uint16_t tvoc_ppb, eco2_ppm;
        if (sgp30_measure_iaq(&s_sgp30, &tvoc_ppb, &eco2_ppm) == ESP_OK) {
            fresh.voc_ppb  = (float)tvoc_ppb;
            fresh.sensor_ok = true;
        } else {
            fresh.sensor_ok = false;
        }

        // Read Temperature + Humidity
        float temp_c, hum_pct;
        if (sht31_read(&s_sht31, &temp_c, &hum_pct) == ESP_OK) {
            fresh.temperature_c = temp_c;
            fresh.humidity_pct  = hum_pct;
        } else {
            fresh.sensor_ok = false;
        }

        fresh.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        memcpy(&s_data, &fresh, sizeof(s_data));
        xSemaphoreGive(s_mutex);

        alarm_manager_check(&fresh);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

#### 2. `main/app_main.c` — add I2C bus init

Before `sensor_manager_init()`, initialize the I2C master bus:

```c
#include "driver/i2c.h"

static void i2c_master_init(void)
{
    i2c_config_t cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = GPIO_NUM_X,   // set to your SDA pin
        .scl_io_num       = GPIO_NUM_Y,   // set to your SCL pin
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,       // 100 kHz standard mode
    };
    i2c_param_config(I2C_NUM_0, &cfg);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
}
```

#### 3. Recommended sensors

| Parameter | Recommended sensor | I2C address | ESP-IDF component |
|-----------|-------------------|-------------|-------------------|
| VOC (TVOC) | Sensirion SGP30 | 0x58 | `sensirion/sgp30` |
| VOC (index) | Sensirion SGP40 | 0x59 | `sensirion/sgp40` |
| Temp + Humidity | Sensirion SHT31 | 0x44 | `sensirion/sht3x` |
| Temp + Humidity (alt) | Sensirion SHT40 | 0x44 | `sensirion/sht4x` |

SGP30 reports TVOC in ppb directly — maps 1:1 to `voc_ppb` in `sensor_data_t`. SGP40 reports a VOC Index (1–500 dimensionless) — if using SGP40, adjust the alarm thresholds in `alarm_manager.c` and the chart Y-range in `screen_voc_detail.c` accordingly.

#### 4. `sensor_manager.c` — error handling

Set `fresh.sensor_ok = false` any time an I2C read fails. The alarm manager already handles this:

```c
if (!data->sensor_ok) {
    check_threshold(ALARM_SENSOR_ERROR, true, 0.0f, 0.0f);
}
```

This will generate a `SENSOR ERROR` alarm entry visible in the Alarms screen.

#### 5. Touch calibration tuning

If touch coordinates are inaccurate after assembly, adjust these four constants in `touch/touch_driver.h`:

```c
#define TOUCH_RAW_X_MIN   200   // raw ADC at left edge
#define TOUCH_RAW_X_MAX   4000  // raw ADC at right edge
#define TOUCH_RAW_Y_MIN   200   // raw ADC at top edge
#define TOUCH_RAW_Y_MAX   3600  // raw ADC at bottom edge
```

To calibrate: log `touch_driver_read()` raw values while touching each corner. Do not invert the X formula — the hardware is not mirrored.

#### 6. `alarm_manager.c` — threshold defaults

The compile-time defaults in `alarm_manager.c` are used if no NVS values exist yet (first boot). Update these to match your operating environment:

```c
#define VOC_ALARM_PPB   500.0f   // raise alarm if TVOC exceeds this
#define TEMP_HIGH_C      28.0f
#define TEMP_LOW_C       18.0f
#define HUM_HIGH_PCT     65.0f
#define HUM_LOW_PCT      35.0f
```

The operator can adjust all thresholds at runtime from the Settings screen; changes persist in NVS across reboots.

### No changes required elsewhere

| File | Reason |
|------|--------|
| All UI screens | Read only from `sensor_data_t` and `alarm_entry_t` |
| `alarm_manager.c` | Operates on `sensor_data_t` fields — sensor-agnostic |
| `display_driver.c` | Hardware already real |
| `touch_driver.c` | Hardware already real (calibration values may need tuning) |
| `ui.c`, `lvgl_port.c` | No sensor dependency |
| NVS threshold keys | No change — `sensor_manager_init()` loads them the same way |

---

## Build & Flash

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Partition table uses the default 4 MB single-app layout. NVS is on the standard `nvs` partition.

---

## Known Limitations (simulation mode)

- VOC, temperature, and humidity values are generated by a sine-wave function in `sensor_manager.c::sim_sample()`. No real sensor is read.
- Alarm history is empty at startup; the Alarms screen shows 4 hardcoded POC rows as a visual placeholder.
- VOC Detail chart is pre-filled with 60 simulated data points at boot.
- These are the **only** behaviours that change when real sensors are connected.
