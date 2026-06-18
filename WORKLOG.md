# IVF VOC Monitoring System — Work Log

**Device:** CrowPanel DIS06043H v2.1 (ESP32-S3 N4R2, 480×272 RGB565)  
**Stack:** ESP-IDF 5.3.1 · LVGL 8.4.0 (managed component)  
**UI orientation:** Portrait 272×480 (hardware rotation via RGB panel SWAP_XY + MIRROR_Y)  
**Last updated:** 2026-06-18

---

## Project Overview

Environmental monitoring system for IVF laboratory air quality. Measures TVOC, temperature, and humidity. Displays live readings, 90/30/7-day trend charts, a data log table, and configurable alert thresholds. Will integrate a real ENS160 + AHT21 sensor combo once the UI framework is complete.

---

## Hardware Reference

| Item | Detail |
|------|--------|
| SoC | ESP32-S3 N4R2 (4 MB flash, 2 MB OPI PSRAM) |
| Panel | ST7262 RGB565 parallel, 480×272 physical |
| Touch | XPT2046 resistive, SPI2 |
| LVGL rotation | `LV_DISP_ROT_NONE` — hardware rotation: RGB panel SWAP_XY + MIRROR_Y |
| Draw buffer | Full-frame PSRAM: 272×480×2 = 261 KB |
| NVS namespace | `ivf_cfg` (keys: voc_warn, voc_alarm, tmp_warn, tmp_alarm, hum_lo, hum_hi) |

---

## Migration Roadmap (9 Phases)

### ✅ Phase 1 — Portrait Rotation Infrastructure
**Status: COMPLETE (rotation implementation corrected in Phase 2.1)**

Set up logical portrait coordinate space (272×480) and full-frame PSRAM draw buffer.

**Files changed:**
- `main/lvgl_port/lvgl_port.h` — Added `DISPLAY_LOGICAL_W=272`, `DISPLAY_LOGICAL_H=480`; removed line-buffer constants; set `LVGL_ROTATION` (later corrected to `LV_DISP_ROT_NONE`)
- `main/lvgl_port/lvgl_port.c` — Replaced dual static line buffers with single full-frame PSRAM allocation (261 KB); set `disp_drv.hor_res=272`, `ver_res=480`, `full_refresh=1`
- `main/touch/touch_driver.h` — Added portrait output range constants `TOUCH_OUT_X_MAX=271`, `TOUCH_OUT_Y_MAX=479`
- `main/touch/touch_driver.c` — Rewrote `map_x()` / `map_y()` for portrait coordinate remapping
- `main/board/board.h` — Created: central GPIO pin map for LCD (16 data + 6 control pins), XPT2046 SPI, I2C placeholder

**Files preserved (zero changes):**
- `display/display_driver.c/.h` — Physical RGB timing for ST7262 (`DISPLAY_WIDTH=480`, `DISPLAY_HEIGHT=272`, `fb_in_psram=1`, `psram_trans_align=64`, `pclk_active_neg=1`)

---

### ✅ Phase 2.1 — Display Orientation Debugging & Hardware Rotation Fix
**Status: COMPLETE · Build verified (1373/1373, zero errors)**

Investigated why portrait content was appearing incorrectly after Phase 1 and Phase 2, traced
the root cause through the LVGL 8.4.0 source and ESP-IDF RGB panel source, and implemented
the correct hardware-rotation fix.

**Root cause found:**
LVGL 8.4.0 `lv_refr.c` line 1181 explicitly blocks software rotation when `full_refresh=1`:
```c
if (disp_refr->driver->full_refresh && drv->sw_rotate) {
    LV_LOG_ERROR("cannot rotate a full refreshed display!");
    return;
}
```
Additionally the rotation path at line 1293 only fires when `sw_rotate=1`, which was never set.
Result: `disp_drv.rotated = LV_DISP_ROT_270` was silently ignored — no rotation applied at all.

Without SWAP_XY set in the RGB panel, `draw_bitmap` clamped `y_end = MIN(480, v_res=272) = 272`,
so only a 272×272 square of portrait content was written to the top-left of the physical panel.
The header ("AIR QUALITY MONITOR") appeared at the physical top = the 480 px (long) side of the
landscape display instead of the short 272 px side.

**Fix:**
Replace LVGL software rotation with the RGB panel's built-in hardware rotation. The combined
`ROTATE_MASK_SWAP_XY | ROTATE_MASK_MIRROR_Y` mask maps LVGL logical `(lx, ly)` →
physical framebuffer position `((v_res−1−lx) × h_res + ly)` = physical `(px=ly, py=271−lx)`.
When the device is held left-edge-up (90° CW), the viewer sees portrait `(lx, ly)` correctly
with no mirroring.

**Files changed:**
- `main/display/display_driver.c` — Added `esp_lcd_panel_swap_xy(*out_panel, true)` and
  `esp_lcd_panel_mirror(*out_panel, false, true)` after `esp_lcd_panel_init()`
- `main/lvgl_port/lvgl_port.h` — Changed `LVGL_ROTATION` to `LV_DISP_ROT_NONE`; updated comment
- `main/lvgl_port/lvgl_port.c` — Updated comment (was "90° software rotation")
- `main/touch/touch_driver.c` — `map_x()` direct (landscape X → portrait Y), `map_y()` inverted
  (landscape Y → portrait X). These are correct for SWAP_XY + MIRROR_Y left-edge-up orientation.

**Pixel path (verified correct):**
```
LVGL renders portrait 272×480 into PSRAM draw buffer
flush_cb: esp_lcd_panel_draw_bitmap(0, 0, 272, 480, buf)
draw_bitmap SWAP_XY|MIRROR_Y: (lx,ly) → fb[(271-lx)*480 + ly]
ST7262 DMA → 480×272 physical panel, device mounted left-edge-up → portrait ✓
```

**Confirmed on device:** header "AIR QUALITY MONITOR" appears at top of portrait (short) side. ✓

---

### ✅ Phase 2 — UI Framework Migration
**Status: COMPLETE · Build verified (1373/1373, zero errors)**

Replaced dark-theme 480×272 landscape navigation shell with light-theme 272×480 portrait navigation shell. Four content screens are empty stubs proving the tab bar and fade navigation work.

**Files rewritten:**
- `main/ui/ui.h` — Light palette (`#FFFFFF` bg, `#1A73E8` primary, `#43A047` good, `#FB8C00` warning, `#E53935` danger); portrait layout constants (`IVF_SCREEN_W=272`, `IVF_SCREEN_H=480`, `IVF_HEADER_H=44`, `IVF_TAB_H=50`, `IVF_CONTENT_H=386`); renamed screen IDs (`SCREEN_CHART`, `SCREEN_LOGS`); added `ui_build_header()` and `ui_build_tab_bar()` declarations
- `main/ui/ui.c` — Shared builders (`ui_build_header`, `ui_build_tab_bar`); fade navigation (`LV_SCR_LOAD_ANIM_FADE_IN`); new screen registry; light-theme shared styles
- `main/ui/screens/screen_dashboard.c` — Portrait stub: white 272×480, header "AIR QUALITY MONITOR", 4-tab bar; `screen_dashboard_update()` is no-op
- `main/ui/screens/screen_settings.c` — Portrait stub: white 272×480, header "SETTINGS", tab bar

**Files created:**
- `main/ui/screens/screen_chart.c/.h` — Replaces screen_voc_detail; stub: header "TVOC HISTORY", tab bar
- `main/ui/screens/screen_logs.c/.h` — Replaces screen_alarms; stub: header "DATA LOGS", tab bar

**Files minimally modified:**
- `main/ui/screens/screen_splash.c` — Line 48: `480,272` → `IVF_SCREEN_W,IVF_SCREEN_H`; Line 78: bar width 280→240 (was wider than portrait screen)

**Files deleted:**
- `main/ui/screens/screen_voc_detail.c/.h` — Replaced by screen_chart
- `main/ui/screens/screen_alarms.c/.h` — Replaced by screen_logs

**Files preserved (zero changes):**
- All hardware drivers, sensor_manager, alarm_manager, app_main, board.h

**Architecture changes:**
- Color theme: dark `#0D1117` → light `#FFFFFF`
- Navigation: `MOVE_LEFT/RIGHT` slide → `FADE_IN` (200 ms)
- Navigation model: back-button → 4-tab bar (Home/Chart/Logs/Settings)
- Tab bar: 272×50 px, 4 equal cells (68 px each), active = 3 px primary-blue top indicator
- Header bar: 272×44 px, title centred, bottom border 1 px `#E0E0E0`

**Pending hardware verification:**
- Confirm portrait orientation (not inverted) on physical device
- Confirm touch tap targets align to visible tab bar cells

---

### ✅ Phase 3A — Dashboard Visual Implementation (Frozen)
**Status: FROZEN · Final build verified (zero errors, zero warnings)**

**Goal:** Replace the empty dashboard stub with the full approved visual design — multi-zone arc gauge, TVOC reading in arc centre with level badge, temperature and humidity sensor cards each with a sparkline.

**Geometry (frozen values):**

| Constant | Value | Notes |
|----------|-------|-------|
| `ARC_SIZE` | 210 px | Diameter |
| `ARC_WIDTH` | 18 px | Thicker track |
| `ARC_CX` | 136 | Content-relative arc centre X |
| `ARC_CY` | 160 | Content-relative arc centre Y (shifted down to clear TVOC title) |
| `ARC_TOP_X` | 31 | = ARC_CX − ARC_SIZE/2 |
| `ARC_TOP_Y` | 55 | = ARC_CY − ARC_SIZE/2 |
| `CARD_Y` | 255 | Content-relative top of sensor cards |
| `CARD_W` | 124 px | Per card |
| `CARD_H` | 110 px | Reduced from 140 to give arc breathing room |
| Chart height | 36 px | Sparkline within card |
| Sparkline points | 30 | Smooth polyline |

**Design implemented:**
- Content area: 272×386 px (y=44, h=IVF_CONTENT_H=386)
- Arc gauge: 210×210 px, track width 18 px, 4 static colour zones (see below)
- Scale labels 0/250/500/750/1000 — pixel-exact absolute positions via `make_scale_label_abs()` (tuned on device, no cosf/sinf at runtime)
- Centre value stack: "245" (`IVF_FONT_HUGE`) / "ppb" (`IVF_FONT_NORMAL`) / "GOOD ✓" pill badge — flex column in transparent 130×115 container at (71, 103)
- Sensor cards: 124×110 each at y=255 content-relative
- Sparklines: 30-point, smooth polyline, no point markers (`lv_style_size=0` on `LV_PART_INDICATOR`)
- Header: leaf dot (green 14×14 circle) LEFT_MID x=8; title `IVF_FONT_SMALL` LEFT_MID x=26; `LV_SYMBOL_WIFI` at TOP_RIGHT x=−28; `LV_SYMBOL_SD_CARD` at TOP_RIGHT x=−8; time y=18; date y=30

**Arc gauge zones:**
| Zone | ppb range | LVGL angles | Colour |
|------|-----------|-------------|--------|
| Green | 0 – 250 | 135° → 202° | `#43A047` |
| Yellow | 250 – 500 | 202° → 270° | `#FDD835` (local `DASH_COLOR_YELLOW`) |
| Orange | 500 – 750 | 270° → 338° | `#FB8C00` |
| Red | 750 – 1000 | 338° → 45° | `#E53935` |

All 4 zone arcs always fully visible (static). No indicator arm — value shown by centre label only.

**Scale label pixel coordinates (content-relative, centre of text):**
| Label | X | Y |
|-------|---|---|
| "0" | 48 | 245 |
| "250" | 20 | 125 |
| "500" | 136 | 40 |
| "750" | 253 | 125 |
| "1000" | 220 | 245 |

**Key fixes across three iterations:**
- Removed dark navy `s_arc_value` arc that was covering zone arcs
- Added 4th yellow zone (was missing in first build)
- Corrected zone angle boundaries and scale labels (0/250/500/750/1000)
- Chart dots removed via `lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR)`
- Icons: `lv_obj` primitives (stem+bulb thermometer, round-rect drop)
- Card labels uppercased: "TEMPERATURE", "HUMIDITY"
- Header title: moved from CENTER to LEFT_MID, font reduced to `IVF_FONT_SMALL` (prevents overlap with right-side time/date)
- Added `LV_SYMBOL_SD_CARD` to header (right of WiFi)
- TVOC title / "500" label overlap resolved: `ARC_CY` 131→160, `ARC_WIDTH` 14→18
- Scale labels switched from runtime trigonometry to `make_scale_label_abs()` with device-tuned pixel coords
- Card height 140→110 px, chart height reduced to 36 px
- Data points increased to 30 for smoother sparklines

**Files changed:**
- `main/ui/screens/screen_dashboard.c` — Full implementation (three iterations + freeze cleanup)
- `main/ui/screens/screen_dashboard.h` — Added `dashboard_set_time()` and `dashboard_set_date()` API

**Static widget handles (runtime-updated in Phase 3B):**
```c
s_lbl_tvoc_value   s_lbl_temp_value   s_lbl_hum_value
s_lbl_level        s_chart_temp       s_chart_hum
s_lbl_time         s_lbl_date
```

**Mock values (hardcoded for Phase 3A):** TVOC=245 ppb, Temp=28.4 °C, Humidity=63 %

---

### ⬜ Phase 3B — Dashboard Data Binding
**Status: NOT STARTED**

**Goal:** Wire `screen_dashboard_update()` to live `sensor_manager` data so the dashboard reflects real (or simulated) sensor readings.

**Files to modify:**
- `main/ui/screens/screen_dashboard.c` — Implement `screen_dashboard_update()`:
  1. Call `sensor_manager_get_data(&d)`
  2. Update `s_lbl_tvoc_value` with `d.voc_ppb`
  3. Update `s_lbl_level` badge colour + text based on `sensor_get_voc_level(d.voc_ppb)`
  4. Update `s_lbl_temp_value` / `s_lbl_hum_value`
  5. Push new chart points: `lv_chart_set_next_value(s_chart_temp, ser, (int16_t)(d.temperature_c * 10))`
- `main/app_main.c` — Confirm `ui_refresh_task` calls `ui_dashboard_refresh()` at 1 Hz (already present)

**Acceptance criteria:**
- Dashboard values update every 1 s matching `sensor_manager` simulation sine wave
- Badge colour transitions green→yellow→orange→red at correct thresholds
- Sparklines scroll left as new readings arrive
- No LVGL heap assertion on sustained 1 Hz refresh

---

### ⬜ Phase 4 — TVOC History Chart (90/30/7-day)
**Status: NOT STARTED**

**Goal:** Implement `screen_chart.c` with aggregated TVOC history chart, period-switching, and axis labels.

**Design spec:**
- Three period buttons at top of content: "90D" / "30D" / "7D" (toggle group)
- `lv_chart` below buttons, full content width, line mode
- Y-axis: 0–1000 ppb with grid lines at 300 and 500 (warning/alarm thresholds)
- X-axis: time labels (day/date) auto-fitted to selected period
- Single TVOC series; colour = `IVF_COLOR_PRIMARY`
- Data source: a ring buffer of aggregated daily min/max/avg (new module: `data/history_manager.c`)

**Files to create:**
- `main/data/history_manager.c/.h` — Hourly and daily TVOC aggregation ring buffers; `history_get_daily()`, `history_get_hourly()` APIs

**Files to modify:**
- `main/ui/screens/screen_chart.c` — Full implementation
- `main/sensors/sensor_manager.c` — Hook to push readings into history_manager at each 1 Hz tick
- `main/CMakeLists.txt` — Add history_manager.c

**sdkconfig prerequisite:**
- Confirm `CONFIG_LV_USE_CHART=y`

**Acceptance criteria:**
- Tapping "90D" / "30D" / "7D" redraws chart with correct number of x-points
- Chart scrolls when more data than visible points exist
- Threshold grid lines visible at correct ppb values
- `screen_chart_refresh()` called on tab navigation updates series

---

### ⬜ Phase 5 — Data Logs Screen + Sensor Record Buffer
**Status: NOT STARTED**

**Goal:** Implement `screen_logs.c` with a scrollable `lv_table` showing per-minute sensor records.

**Design spec:**
- `lv_table` columns: Time | TVOC (ppb) | Temp (°C) | Hum (%) | Status
- Column widths: 60 / 60 / 52 / 44 / 56 = 272 px total
- Table fills 386 px content height; vertical scroll enabled
- Status cell: coloured text ("GOOD" green / "WARN" amber / "ALARM" red)
- Data source: new ring buffer of 1-minute averaged sensor records (max 1440 = 24 h)
- Simplify `alarm_manager.c`: remove ring buffer, keep threshold check function only

**Files to create:**
- `main/data/record_manager.c/.h` — 1-minute averaged sensor record ring buffer

**Files to modify:**
- `main/ui/screens/screen_logs.c` — Full implementation; `screen_logs_refresh()` rebuilds table rows
- `main/data/alarm_manager.c/.h` — Simplify: remove ring buffer; keep `alarm_check()` returning level
- `main/app_main.c` — Fix tight coupling: replace direct `ui_dashboard_refresh()` call in `ui_refresh_task` with an event/message queue so the UI task pulls updates instead of being called across tasks
- `main/CMakeLists.txt` — Add record_manager.c

**sdkconfig prerequisite:**
- Verify `CONFIG_LV_USE_TABLE=y`

**Acceptance criteria:**
- Table shows at least 24 rows (1 per minute of simulated data)
- Table scrolls smoothly on swipe
- Status column colour matches sensor level at record time
- Navigating away and back re-populates the table via `screen_logs_refresh()`

---

### ⬜ Phase 6 — Settings Screen (Brightness + Thresholds)
**Status: NOT STARTED**

**Goal:** Implement `screen_settings.c` with PWM brightness slider and threshold spinboxes, saving to NVS via a `config_manager` abstraction layer.

**Design spec:**
- Brightness section: horizontal `lv_slider` (0–100%), live PWM output to backlight GPIO
- VOC thresholds: Warning (default 300 ppb), Alarm (500 ppb) — spinbox rows
- Temp thresholds: Warning (26 °C), Alarm (28 °C) — spinbox rows
- Humidity thresholds: Low (35 %), High (65 %) — spinbox rows
- Save button: writes all values to NVS, shows brief "Saved ✓" confirmation
- `lv_indev_wait_release()` in all spinbox handlers (prevents phantom touch repeats)

**Files to create:**
- `main/data/config_manager.c/.h` — NVS read/write abstraction; `config_get_thresholds()`, `config_set_thresholds()`; removes direct NVS access from UI code

**Files to modify:**
- `main/ui/screens/screen_settings.c` — Full implementation using config_manager
- `main/display/display_driver.c/.h` — Add PWM backlight function `display_set_backlight_pct(uint8_t pct)` replacing GPIO toggle
- `main/app_main.c` — Load thresholds from config_manager at boot and pass to sensor_manager
- `main/CMakeLists.txt` — Add config_manager.c

**sdkconfig prerequisite:**
- Verify `CONFIG_LV_USE_SLIDER=y`; add `CONFIG_LEDC_ENABLED=y` for PWM backlight

**Acceptance criteria:**
- Slider moves → backlight brightness changes in real time
- Spinbox +/- buttons do not bounce or repeat phantom presses
- Save → NVS persists; reboot reads back same values and applies to sensor classification
- Settings tab shows current saved values when re-entered

---

### ⬜ Phase 7 — Real Sensor Integration (ENS160 + AHT21)
**Status: NOT STARTED**

**Goal:** Replace the 1 Hz sine-wave simulation in `sensor_manager.c` with real ENS160 (TVOC/eCO2) + AHT21 (temperature/humidity) I2C sensor reads.

**Hardware:**
- ENS160 + AHT21 combined module on I2C bus
- I2C pins: SDA = GPIO 17, SCL = GPIO 18 (defined in `board.h`)
- ENS160 I2C address: 0x53 (ADDR pin low) or 0x52 (ADDR pin high)
- AHT21 I2C address: 0x38

**Integration notes:**
- ENS160 requires compensation input: temperature and humidity from AHT21 at startup and periodically
- ENS160 must reach "running" status before TVOC readings are valid (warm-up ~1 min)
- Read AHT21 first → feed to ENS160 compensation registers → read ENS160 TVOC
- ENS160 data validity flag must be checked before accepting readings

**Files to modify:**
- `main/sensors/sensor_manager.c` — Replace simulation task with real I2C reads; add warm-up state; preserve `sensor_data_t` struct and mutex API
- `main/board/board.h` — Confirm I2C pin assignments (SDA=17, SCL=18 already defined)
- `main/app_main.c` — Add `i2c_master_init()` call before `sensor_manager_init()`

**Files to create:**
- `main/sensors/ens160_driver.c/.h` — ENS160 I2C read/write, mode set, TVOC/eCO2 read, compensation write
- `main/sensors/aht21_driver.c/.h` — AHT21 trigger/read/CRC, returns temperature and humidity
- `main/CMakeLists.txt` — Add driver files

**Acceptance criteria:**
- Dashboard shows real TVOC, temperature, humidity (not sine wave)
- ENS160 warm-up period: dashboard shows "Warming up..." and suppresses TVOC readings
- Sensor error displayed if I2C transaction fails 3 consecutive times
- AHT21 CRC check passes; invalid reads retried before reporting error

---

### ⬜ Phase 8 — Data Persistence (NVS Log + SD Card Export)
**Status: NOT STARTED**

**Goal:** Persist sensor records to NVS flash (short-term) and optionally export to SD card CSV (long-term).

**Design:**
- NVS persistence: write 1-minute averaged records to NVS on a 5-minute flush cycle (survives reboot)
- Record limit: 1440 records (24 h in NVS); oldest overwritten when full
- SD card: optional export button on Data Logs screen writes all records to `/sdcard/ivf_log_YYYYMMDD.csv`
- SD pins: use SDMMC or SPI2 — to be determined based on available GPIOs in `board.h`
- SNTP time sync: if WiFi available, sync RTC for accurate timestamps

**Files to modify:**
- `main/data/record_manager.c` — Add NVS flush and restore-on-boot
- `main/ui/screens/screen_logs.c` — Add "Export CSV" button (visible only when SD card detected)

**Files to create:**
- `main/data/sd_export.c/.h` — SD card mount/unmount, CSV write

**Acceptance criteria:**
- After reboot, logs screen shows records from before the reboot
- Tapping Export writes valid CSV to SD card
- Records have correct timestamps (real time if SNTP synced, elapsed ms otherwise)

---

### ⬜ Phase 9 — Polish, OTA, and Production Hardening
**Status: NOT STARTED**

**Goal:** Final production readiness — OTA firmware update, watchdog, error screens, display sleep, and memory audit.

**Items:**
- **OTA update**: ESP-IDF OTA over WiFi or BLE; version displayed on splash screen
- **Watchdog**: enable task watchdog for LVGL task and sensor task; proper feed calls
- **Display sleep**: auto-dim after 5 min idle (LEDC PWM step-down); wake on touch
- **Error screen**: dedicated full-screen error view for critical failures (sensor permanently lost, NVS corrupt, OOM)
- **Memory audit**: `heap_caps_print_heap_info()` on boot; verify PSRAM allocation succeeds; assert if draw buffer malloc fails
- **Startup self-test**: touch calibration verification (tap two corners on first boot), sensor communication check
- **Splash version**: display firmware version string from `IDF_VER` and `APP_VERSION`
- **Code review**: remove all `ESP_LOGD` in production build; set `CONFIG_LOG_DEFAULT_LEVEL_WARN`

**Acceptance criteria:**
- OTA flash completes without power cycle
- Device recovers from 72-hour continuous soak test without panic or memory leak
- Display dims after 5 min and brightens on touch
- All production configs set in `sdkconfig.defaults`

---

## File Inventory (Current State)

### Hardware / LVGL Port (frozen — do not modify)
| File | Status | Notes |
|------|--------|-------|
| `main/display/display_driver.c/.h` | ✅ Phase 2.1 complete | ST7262 RGB565, PSRAM fb, SWAP_XY+MIRROR_Y hardware rotation |
| `main/touch/touch_driver.c/.h` | ✅ Phase 2.1 complete | map_x direct, map_y inverted — correct for left-edge-up |
| `main/lvgl_port/lvgl_port.c/.h` | ✅ Phase 2.1 complete | `LV_DISP_ROT_NONE`, full-frame PSRAM draw buffer, hardware rotation |
| `main/board/board.h` | ✅ Phase 1 complete | Central GPIO pin map |

### UI Framework (Phase 2 complete)
| File | Status | Notes |
|------|--------|-------|
| `main/ui/ui.h` | ✅ Phase 2 complete | Light theme, portrait constants, new screen IDs |
| `main/ui/ui.c` | ✅ Phase 2 complete | Tab bar builder, header builder, fade navigation |
| `main/ui/screens/screen_splash.c/.h` | ✅ Phase 2 complete | Portrait size fix |
| `main/ui/screens/screen_dashboard.c/.h` | ✅ Phase 3A frozen | Multi-zone arc 4 zones, pixel-exact scale labels, header with WiFi+SD card, 110px cards, 30pt sparklines, mock data. Phase 3B wires sensor_manager. |
| `main/ui/screens/screen_chart.c/.h` | ⬜ Stub | Full content Phase 4 |
| `main/ui/screens/screen_logs.c/.h` | ⬜ Stub | Full content Phase 5 |
| `main/ui/screens/screen_settings.c/.h` | ⬜ Stub | Full content Phase 6 |

### Data / Sensors
| File | Status | Notes |
|------|--------|-------|
| `main/sensors/sensor_manager.c/.h` | ⬜ Simulation | Replace with real I2C reads in Phase 7 |
| `main/data/alarm_manager.c/.h` | ⬜ Pending simplify | Simplify in Phase 5 (remove ring buffer) |
| `main/data/history_manager.c/.h` | ❌ Not created | Create in Phase 4 |
| `main/data/record_manager.c/.h` | ❌ Not created | Create in Phase 5 |
| `main/data/config_manager.c/.h` | ❌ Not created | Create in Phase 6 |
| `main/sensors/ens160_driver.c/.h` | ❌ Not created | Create in Phase 7 |
| `main/sensors/aht21_driver.c/.h` | ❌ Not created | Create in Phase 7 |

### Application
| File | Status | Notes |
|------|--------|-------|
| `main/app_main.c` | ✅ Working | Fix task coupling in Phase 5 |

---

## Known Issues / Decisions Pending Hardware Test

1. ~~**Rotation direction**: RESOLVED in Phase 2.1.~~ Hardware rotation via `esp_lcd_panel_swap_xy` +
   `esp_lcd_panel_mirror` in `display_driver.c`. `LVGL_ROTATION = LV_DISP_ROT_NONE`. LVGL software
   rotation cannot work with `full_refresh=1` (LVGL 8.4.0 explicitly blocks it).

2. **Touch calibration**: Raw ADC min/max values (`TOUCH_RAW_X_MIN=200`, `TOUCH_RAW_X_MAX=4000`, `TOUCH_RAW_Y_MIN=200`, `TOUCH_RAW_Y_MAX=3600`) are initial estimates. Fine-tune after Phase 2 flash by tapping all four corners and logging raw values.

3. **sdkconfig widget verification:**
   - `CONFIG_LV_USE_ARC=y` ✅ confirmed — arc gauge builds and runs (Phase 3A)
   - `CONFIG_LV_USE_CHART=y` ✅ confirmed — sparklines build and run (Phase 3A)
   - `CONFIG_LV_USE_TABLE=y` ⬜ verify before Phase 5 — logs table
   - `CONFIG_LV_USE_SLIDER=y` ⬜ verify before Phase 6 — brightness slider

4. **app_main.c tight coupling**: `ui_refresh_task` calls `ui_dashboard_refresh()` directly across FreeRTOS tasks. This is safe for now (protected by `lvgl_port_lock`) but will be refactored to an event queue in Phase 5.

5. **ENS160 I2C address**: confirm ADDR pin wiring before Phase 7 (0x52 or 0x53).

---

## Build Record

| Date | Phase | Result | Binary size |
|------|-------|--------|-------------|
| 2026-06-17 | Phase 2 | ✅ 1373/1373, zero errors | 0x88A90 (553 KB, 47% free) |
| 2026-06-17 | Phase 2.1 rotation fix | ✅ 1373/1373, zero errors | 0x88D00 (553 KB, 47% free) |
| 2026-06-17 | Phase 3A (first build — 1 warning) | ✅ build OK, 1 warning (-Wunused-function `tvoc_to_angle`) | 0xAD980 (689 KB, 32% free) |
| 2026-06-18 | Phase 3A (corrected — 4 zones, no indicator arc) | ✅ 1373/1373, zero errors, zero warnings | 0xA84A0 (689 KB, 34% free) |
| 2026-06-18 | Phase 3A visual refinement + freeze (header layout, gauge shift, label abs coords, card resize) | ✅ zero errors, zero warnings | 0xA8500 (34% free) |

---

## Sensor Thresholds (Default, stored in NVS `ivf_cfg`)

| Parameter | Warning | Alarm |
|-----------|---------|-------|
| TVOC | 300 ppb | 500 ppb |
| Temperature | 26 °C | 28 °C |
| Humidity low | — | 35 % |
| Humidity high | 65 % | — |
