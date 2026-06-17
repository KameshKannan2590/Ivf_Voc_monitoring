# IVF VOC Monitor — Architecture & Developer Guide

**Project:** IVF Environment Monitoring System (EMS) — VOC / Temperature / Humidity / CO₂  
**Hardware:** Elecrow CrowPanel ESP32-S3 4.3" HMI (SKU: DIS06043H, v2.1)  
**Framework:** ESP-IDF 5.3.1 (pure — no Arduino layer)  
**UI:** LVGL 8.4.0 (managed component, `idf_component.yml` pins `>=8.3.0, <9.0.0`)  
**Version:** 1.0.0 (pre-production / sensor stub phase)

---

## Table of Contents

1. [Hardware Overview](#1-hardware-overview)
2. [Software Stack](#2-software-stack)
3. [Project File Structure](#3-project-file-structure)
4. [Module Descriptions](#4-module-descriptions)
5. [Screen Flow & UI](#5-screen-flow--ui)
6. [SDK Configuration](#6-sdk-configuration)
7. [Build, Flash & Monitor](#7-build-flash--monitor)
8. [What Is Done](#8-what-is-done)
9. [What Needs to Be Modified / Completed](#9-what-needs-to-be-modified--completed)
10. [Known Limitations & Notes](#10-known-limitations--notes)

---

## 1. Hardware Overview

| Feature | Detail |
|---------|--------|
| MCU | ESP32-S3 (dual-core Xtensa LX7, 240 MHz) |
| Display | 480 × 272 px, 16-bit RGB565, parallel RGB bus |
| Touch | XPT2046 resistive, SPI interface |
| Flash | 4 MB minimum (set `CONFIG_ESPTOOLPY_FLASHSIZE_*` to match your module) |
| PSRAM | **2 MB OPI PSRAM present** (N4R2 variant). Framebuffer (`fb_in_psram=1`, 261 KB) and LVGL draw buffer allocated from PSRAM. |
| USB | USB Serial/JTAG built-in |
| Board doc | `IVF_EMS_Architecture_v2.docx` (in repo root) |

### GPIO Pin Map

#### RGB Display (16-bit parallel)

| Signal | GPIO | Signal | GPIO |
|--------|------|--------|------|
| DE | 40 | PCLK | 42 |
| HSYNC | 39 | VSYNC | 41 |
| R0 | 45 | R1 | 48 |
| R2 | 47 | R3 | 21 |
| R4 | 14 | G0 | 5 |
| G1 | 6 | G2 | 7 |
| G3 | 15 | G4 | 16 |
| G5 | 4 | B0 | 8 |
| B1 | 3 | B2 | 46 |
| B3 | 9 | B4 | 1 |
| Backlight | 2 (HIGH = on) | Aux GPIO | 38 (must be HIGH) |

#### Touch Controller (XPT2046, SPI2)

| Signal | GPIO |
|--------|------|
| SCK | 12 |
| MISO | 13 |
| MOSI | 11 |
| CS | 0 |
| INT | 36 |

**Touch calibration (factory):**  
Raw X: `4000` → screen X=0 (left), `200` → screen X=480 (right) — X axis is inverted  
Raw Y: `200` → screen Y=0 (top), `3600` → screen Y=272 (bottom)

---

## 2. Software Stack

```
┌─────────────────────────────────────────────────┐
│                Application Layer                 │
│   app_main.c   →   ui/   →   screens/           │
├──────────────────┬──────────────────────────────┤
│  Sensor Manager  │  Alarm Manager               │
│  (sensors/)      │  (data/)                     │
├──────────────────┴──────────────────────────────┤
│              LVGL Port (lvgl_port/)              │
│     FreeRTOS task · mutex · flush · touch read  │
├─────────────────────┬───────────────────────────┤
│   Display Driver    │   Touch Driver             │
│   (display/)        │   (touch/)                 │
│   esp_lcd_panel_rgb │   Custom SPI (XPT2046)     │
├─────────────────────┴───────────────────────────┤
│              ESP-IDF 5.3.1 (Native)             │
│   esp_lcd · SPI Master · FreeRTOS · NVS         │
├─────────────────────────────────────────────────┤
│        ESP32-S3 Hardware (CrowPanel v2.1)       │
└─────────────────────────────────────────────────┘
```

### Managed Components (via `main/idf_component.yml`)

| Component | Version | Purpose |
|-----------|---------|---------|
| `lvgl/lvgl` | `>=8.3.0, <9.0.0` (resolves to 8.4.0) | LVGL UI framework |

> **Note:** `espressif/esp_lcd_touch_xpt2046` does **not** exist in the ESP-IDF component
> registry. XPT2046 touch is handled by a custom driver in `main/touch/touch_driver.c`
> using the native `spi_master` driver — no external component required.

---

## 3. Project File Structure

```
Ivf_Voc_monitoring/
├── CMakeLists.txt                  ← Root build file (unchanged)
├── sdkconfig.defaults              ← Hardware-specific config overrides (NEW)
├── IVF_EMS_Architecture_v2.docx   ← Original architecture document
├── ARCHITECTURE.md                 ← This file
│
└── main/
    ├── CMakeLists.txt              ← Sources + include dirs + LVGL define
    ├── idf_component.yml           ← Managed component dependencies
    ├── app_main.c                  ← Entry point, init sequence
    │
    ├── display/
    │   ├── display_driver.h        ← GPIO defs, timing constants, API
    │   └── display_driver.c        ← esp_lcd_panel_rgb init + backlight
    │
    ├── touch/
    │   ├── touch_driver.h          ← SPI pin defs, calibration constants, API
    │   └── touch_driver.c          ← SPI2 bus + XPT2046 init via esp_lcd_touch
    │
    ├── lvgl_port/
    │   ├── lv_conf.h               ← LVGL 8.3 config (fonts, widgets, tick source)
    │   ├── lvgl_port.h             ← lvgl_port_init / lock / unlock API
    │   └── lvgl_port.c             ← LVGL task (core 1), flush_cb, touch_read_cb
    │
    ├── ui/
    │   ├── ui.h                    ← Color palette, typography, layout constants, screen IDs
    │   ├── ui.c                    ← Screen manager, shared styles, navigation
    │   └── screens/
    │       ├── screen_splash.h/.c       ← Boot logo + progress bar → auto-advance (complete)
    │       ├── screen_dashboard.h/.c    ← STUB — Phase 3 (arc gauge, sparklines, readings)
    │       ├── screen_chart.h/.c        ← STUB — Phase 4 (lv_chart TVOC history)
    │       ├── screen_logs.h/.c         ← STUB — Phase 5 (lv_table data log)
    │       └── screen_settings.h/.c     ← STUB — Phase 6 (brightness slider, thresholds)
    │
    ├── sensors/
    │   ├── sensor_manager.h        ← sensor_data_t, sensor_level_t, API
    │   └── sensor_manager.c        ← 1 Hz FreeRTOS task; currently SIMULATION STUB
    │
    └── data/
        ├── alarm_manager.h         ← alarm_entry_t, alarm_type_t, API
        └── alarm_manager.c         ← Debounced threshold checks, 32-entry ring buffer
```

---

## 4. Module Descriptions

### `app_main.c`
Entry point and boot sequence:
1. NVS flash init (erase and reinit if corrupt)
2. `display_driver_init()` → RGB panel up
3. `touch_driver_init()` → XPT2046 SPI ready
4. `lvgl_port_init()` → LVGL registered, FreeRTOS task running
5. `alarm_manager_init()` → alarm ring buffer ready
6. `sensor_manager_init()` → 1 Hz sensor task running (simulation)
7. `ui_init()` → all screens built, splash loaded
8. `ui_refresh_task` created → feeds sensor data to dashboard every 1 s

---

### `display/display_driver`
Wraps `esp_lcd_panel_rgb` (native ESP-IDF 5.x).

**Key constants** (`display_driver.h`):
```c
DISPLAY_WIDTH  = 480    /* physical landscape width  */
DISPLAY_HEIGHT = 272    /* physical landscape height */
LCD_PCLK_HZ   = 7_000_000   // 7 MHz — safe for flex cable
```
Framebuffer (480 × 272 × 2 = 261 KB) is allocated in **PSRAM** (`fb_in_psram = 1`, `psram_trans_align = 64`). The N4R2 module has 2 MB OPI PSRAM which is detected and enabled in `sdkconfig.defaults`.

Hardware portrait rotation is applied immediately after `esp_lcd_panel_init()`:
```c
esp_lcd_panel_swap_xy(*out_panel, true);        // ROTATE_MASK_SWAP_XY
esp_lcd_panel_mirror(*out_panel, false, true);  // ROTATE_MASK_MIRROR_Y
```
See **Display Rotation** section for the full explanation.

`LCD_AUX_GPIO` (GPIO 38) must be driven HIGH — hardware requirement of CrowPanel v2.1.

---

### `touch/touch_driver`
Custom SPI driver using `spi_master` directly — no external component required.
(`espressif/esp_lcd_touch_xpt2046` does not exist in the component registry.)

- Commands: `0xD0` (X), `0x90` (Y), `0xB0` (Z1) — matches PaulStoffregen XPT2046 library
- 5-sample averaging; Z1 threshold 50 for touch detection
- X axis inverted in software: raw 4000 → screen X=0, raw 200 → screen X=479
- SPI clock: 2 MHz (conservative; XPT2046 max 2.5 MHz reliable)

---

### `lvgl_port/lvgl_port`
- Draw buffer: **full-frame PSRAM** — 272 × 480 × 2 = 261 120 bytes from PSRAM. Required because `full_refresh=1` sends the entire frame per flush cycle.
- `disp_drv.rotated = LV_DISP_ROT_NONE` — no LVGL software rotation. Hardware rotation is applied by the RGB panel driver (see Display Rotation section).
- `disp_drv.full_refresh = 1` — entire portrait frame sent to `draw_bitmap` every flush.
- Flush callback: `esp_lcd_panel_draw_bitmap(panel, 0, 0, 272, 480, buf)` — with `SWAP_XY|MIRROR_Y` active this correctly writes all 272×480 pixels.
- Touch callback: `touch_driver_read()` from custom XPT2046 driver.
- LVGL FreeRTOS task: core 1, priority 2, 5 ms tick period.
- Thread safety: callers from other tasks must use `lvgl_port_lock() / lvgl_port_unlock()`.

> **`LV_COLOR_16_SWAP = 0`** — correct for `esp_lcd_panel_rgb` (native DMA, little-endian).

> **Why `full_refresh=1` is mandatory:** The RGB panel DMA reads the PSRAM framebuffer continuously. Dirty-rect partial updates are not safe — writing a partial region while DMA is mid-scan causes tearing. Full-frame flush avoids this entirely.

---

### Display Rotation — How It Works

**Problem:** The physical panel is landscape (480 × 272). The device is mounted with its left edge
up (90° CW), giving a 272 × 480 portrait view. LVGL must render portrait content.

**Why LVGL software rotation cannot be used:**
LVGL 8.4.0 `lv_refr.c` line 1181 explicitly blocks `sw_rotate` when `full_refresh = 1`:
```c
if (disp_refr->driver->full_refresh && drv->sw_rotate) {
    LV_LOG_ERROR("cannot rotate a full refreshed display!");
    return;
}
```
And the rotation branch at line 1293 only fires when `sw_rotate = 1`. Since `sw_rotate` is never
set in `lvgl_port.c`, `disp_drv.rotated = LV_DISP_ROT_270` is silently ignored — no LVGL
rotation happens regardless of the `LVGL_ROTATION` constant.

**Solution: RGB panel hardware rotation (`SWAP_XY | MIRROR_Y`):**
```c
// display_driver.c — after esp_lcd_panel_init():
esp_lcd_panel_swap_xy(*out_panel, true);        // sets ROTATE_MASK_SWAP_XY
esp_lcd_panel_mirror(*out_panel, false, true);  // sets ROTATE_MASK_MIRROR_Y
```
Combined mask `SWAP_XY | MIRROR_Y` in `esp_lcd_panel_rgb.c` maps:
```
LVGL logical (lx, ly) → physical fb position ((v_res−1−lx) × h_res + ly)
= physical (px = ly, py = 271 − lx)
```
When the device is held left-edge-up (90° CW), viewer's portrait `(lx, ly)` equals LVGL `(lx, ly)` — no
mirror, no flip. Header at `ly = 0..43` → physical columns `px = 0..43` → portrait top ✓.

**Touch mapping** (`touch_driver.c`):
- `map_x(raw_x)` → LVGL `ly` (direct): landscape X = portrait vertical axis
- `map_y(raw_y)` → LVGL `lx` (inverted): landscape Y = portrait horizontal axis, inverted

**Pixel path:**
```
LVGL renders portrait 272×480 into PSRAM draw buffer
flush_cb: esp_lcd_panel_draw_bitmap(panel, 0, 0, 272, 480, buf)
draw_bitmap SWAP_XY|MIRROR_Y: (lx,ly) → fb[(271-lx)*480 + ly]
ST7262 DMA → 480×272 physical panel
Device mounted left-edge-up → user sees 272×480 portrait ✓
```

---

### `sensors/sensor_manager`
Abstraction layer between hardware sensors and the UI.

**Currently: simulation stub** — outputs sine-wave VOC/Temp/Hum/CO₂ data at 1 Hz.  
Replace `sim_sample()` with a real sensor driver (see section 9).

Public API:
```c
sensor_manager_init();                  // start task
sensor_manager_get_data(&data);         // thread-safe snapshot
sensor_get_voc_level(ppb);              // → GOOD / WARNING / DANGER / ERROR
sensor_get_temp_level(c);
sensor_get_hum_level(pct);
```

Thresholds loaded from NVS on boot; defaults:

| Parameter | Warning | Alarm |
|-----------|---------|-------|
| VOC | 300 ppb | 500 ppb |
| Temperature | 26 °C | 28 °C |
| Humidity | < 35 % or > 65 % | — |

---

### `data/alarm_manager`
- 32-entry circular ring buffer (newest-first on read)
- Debounce: 3 consecutive samples above threshold before firing an alarm
- Alarm types: `VOC_HIGH`, `TEMP_HIGH`, `TEMP_LOW`, `HUM_HIGH`, `HUM_LOW`, `CO2_HIGH`, `SENSOR_ERROR`
- Persists active state in-RAM only (NVS persistence is a TODO)

---

### `ui/ui`
**Light consumer theme** — white background, Google Material blue primary, clean typography.

**Colour palette:**

| Constant | Hex | Use |
|----------|-----|-----|
| `IVF_COLOR_BG` | `#FFFFFF` | Screen background |
| `IVF_COLOR_CARD` | `#FFFFFF` | Card/panel fill |
| `IVF_COLOR_BORDER` | `#E0E0E0` | Card borders, header/tab dividers |
| `IVF_COLOR_PRIMARY` | `#1A73E8` | Accent / active tab indicator |
| `IVF_COLOR_TEXT` | `#212121` | Primary text |
| `IVF_COLOR_TEXT_MUTED` | `#757575` | Labels/captions |
| `IVF_COLOR_GOOD` | `#43A047` | Safe level |
| `IVF_COLOR_WARNING` | `#FB8C00` | Warning level |
| `IVF_COLOR_DANGER` | `#E53935` | Alarm level |
| `IVF_COLOR_NAV` | `#F8F9FA` | Header and tab bar background |
| `IVF_COLOR_TAB_ACTIVE` | `#1A73E8` | Active tab icon/text |
| `IVF_COLOR_TAB_INACTIVE` | `#9E9E9E` | Inactive tab icon/text |

---

## 5. Screen Flow & UI

```
┌─────────┐  auto (~2.4 s)   ┌───────────┐
│  Splash  ├─────────────────►│ Dashboard │◄─────────┐
└─────────┘                  └─────┬─────┘          │
                                   │ touch           │
                    ┌──────────────┼──────────────┐  │
                    ▼              ▼              ▼  │
              ┌──────────┐  ┌────────┐  ┌──────────┐│
              │VOC Detail│  │ Alarms │  │ Settings ││
              └────┬─────┘  └───┬────┘  └────┬─────┘│
                   │ back       │ back        │ back  │
                   └────────────┴────────────►───────┘
```

### Screen: Splash (480 × 272)
```
┌────────────────────────────────────────────────┐
│                                                │
│         IVF VOC Monitor                       │
│   Environmental Monitoring System             │
│   ────────────────────                        │
│         ████████████░░░░░  75%                │
│         Loading configuration...              │
│                                      v1.0.0   │
└────────────────────────────────────────────────┘
```

### Screen: Dashboard (480 × 272)
```
┌─ Status bar (28px) ─────────────────── time ──┐
│  IVF VOC Monitor     ⚠ 1      --:--:--        │
├───────────────────────────┬───────────────────┤
│  VOC LEVEL                │  TEMPERATURE      │
│  ┌─────────────────────┐  │  22.5 °C          │
│  │       125           │  ├───────────────────┤
│  │       ppb           │  │  HUMIDITY         │
│  │     ● GOOD          │  │  48 %             │
│  └─────────────────────┘  ├───────────────────┤
│                           │  CO₂              │
│                           │  450 ppm          │
├─ Nav bar (42px) ──────────────────────────────┤
│  👁 DETAIL    ⚠ ALARMS    ⚙ SETTINGS          │
└────────────────────────────────────────────────┘
```

### Screen: VOC Detail (480 × 272)
```
┌─ ← back ─── VOC Detail ───────────────────────┐
│  125 ppb  ● GOOD                               │
│  ┌──────────────────────────────────────────┐  │
│  │  Trend Chart — last 60 seconds (1Hz)     │  │
│  │  ~~~~^~~~~~~~~~~~~~~~~~~~~~              │  │
│  │                              ── WARN     │  │
│  └──────────────────────────────────────────┘  │
│  MIN            AVG              MAX            │
│  98 ppb        124 ppb          185 ppb         │
└────────────────────────────────────────────────┘
```

### Screen: Alarms (480 × 272)
```
┌─ ← back ─── Alarms ───────────────────────────┐
│  ● VOC HIGH  520.0 / 500.0  00:14:25           │
│  ─ TEMP HIGH  28.5 / 28.0   00:13:10 (acked)  │
│  ─ (no more alarms)                            │
│                                                │
├─ ✔ ACK ALL ──────────── 🗑 CLEAR ─────────────┤
└────────────────────────────────────────────────┘
```

### Screen: Settings (480 × 272)
```
┌─ ← back ─── Settings ─────────────────────────┐
│  VOC Thresholds                                │
│  VOC Warning (ppb)       [─] [ 300 ] [+]       │
│  VOC Alarm (ppb)         [─] [ 500 ] [+]       │
│  Temperature Thresholds                        │
│  Temp Warning (°C)       [─] [  26 ] [+]       │
│  Temp Alarm (°C)         [─] [  28 ] [+]       │
│  Humidity Thresholds                           │
│  Humidity Low (%)        [─] [  35 ] [+]       │
│  Humidity High (%)       [─] [  65 ] [+]       │
├─ 💾 SAVE ──────────────────────── ✔ Saved ────┤
└────────────────────────────────────────────────┘
```

---

## 6. SDK Configuration

The `sdkconfig` is **auto-generated** and must NOT be committed to version control in its current state.  
All hardware-specific overrides live in **`sdkconfig.defaults`** and are merged on first build.

### Critical settings in `sdkconfig.defaults`

| Setting | Value | Reason |
|---------|-------|--------|
| `CONFIG_IDF_TARGET` | `esp32s3` | Board MCU |
| `CONFIG_ESP32S3_DEFAULT_CPU_FREQ_240` | `y` | 240 MHz — more headroom for LVGL vs CrowPanel's 160 MHz default |
| `CONFIG_ESPTOOLPY_FLASHSIZE_4MB` | `y` | Safe default; change to match your actual module flash size |
| `CONFIG_LCD_RGB_ISR_IRAM_SAFE` | `y` | Prevents display glitches during flash cache misses |
| `CONFIG_FREERTOS_HZ` | `1000` | 1 ms tick granularity required by LVGL |
| `CONFIG_ESP_MAIN_TASK_STACK_SIZE` | `8192` | app_main needs more than the hello-world default (3584) |

> **PSRAM IS enabled.** This project targets the N4R2 module variant (2 MB OPI PSRAM).
> `CONFIG_ESP32S3_SPIRAM_SUPPORT=y`, `CONFIG_SPIRAM_MODE_OCT=y`, `fb_in_psram=1`.
> The 261 KB framebuffer and 261 KB LVGL draw buffer both live in PSRAM, freeing internal SRAM
> for FreeRTOS stacks and application data.

> **IMPORTANT:** The `sdkconfig` shipped with the hello world template had wrong values
> (2 MB flash, PSRAM off, 160 MHz CPU, 100 Hz tick). That file has been **deleted**.  
> A fresh `sdkconfig` will be generated from `sdkconfig.defaults` on the first `idf.py build`.

### `sdkconfig` and version control

Add to `.gitignore` (if not already):
```
sdkconfig
build/
```
Commit only `sdkconfig.defaults`. Developers regenerate `sdkconfig` locally.

---

## 7. Build, Flash & Monitor

### Prerequisites

- ESP-IDF 5.3.1 installed and sourced (`idf.py` on PATH)
- Internet access for first build (idf-component-manager fetches LVGL + XPT2046)
- CrowPanel connected via USB (drivers: CP210x or built-in USB-Serial/JTAG on ESP32-S3)

### First-time build

```powershell
# 1. Set target (only needed once per new checkout)
idf.py set-target esp32s3

# 2. Build  — idf-component-manager downloads lvgl/lvgl ~8.3.x on first run
idf.py build
```

The build will create a fresh `sdkconfig` from `sdkconfig.defaults`, then compile.  
Expect ~3-5 minutes on first build (LVGL has many source files).

### Flash

```powershell
# Replace COMX with your actual port  (Windows: COM3, COM4, etc.)
idf.py -p COM3 flash
```

Or flash + open monitor in one command:
```powershell
idf.py -p COM3 flash monitor
```

### Monitor (serial debug output)

```powershell
idf.py -p COM3 monitor
```

Baud rate: **115200**. Press `Ctrl+]` to exit.

Expected boot log:
```
I (xxx) main: ========================================
I (xxx) main: IVF VOC Monitor  v1.0.0
I (xxx) main: CrowPanel ESP32-S3 4.3" 480x272
I (xxx) main: ========================================
I (xxx) display: Initializing RGB LCD 480x272
I (xxx) display: RGB LCD ready
I (xxx) touch: Initializing XPT2046 touch ...
I (xxx) touch: XPT2046 ready
I (xxx) lvgl_port: LVGL port initialized (draw buf 27 lines, task period 5 ms)
I (xxx) alarm_mgr: Alarm manager initialized
I (xxx) sensor_mgr: Sensor manager initialized
I (xxx) ui: Building UI
I (xxx) ui: UI ready
I (xxx) sensor_mgr: Sensor task running (stub/simulation mode)
I (xxx) main: System running
```

### Menuconfig (interactive settings editor)

```powershell
idf.py menuconfig
```

Navigate to **Component config → LVGL** to tune fonts/widgets.

### Clean build

```powershell
idf.py fullclean
# or just remove build/
Remove-Item -Recurse -Force build
```

---

## 8. What Is Done

| # | Module | Status | Notes |
|---|--------|--------|-------|
| 1 | Hardware pin map | ✅ Complete | All GPIOs verified against CrowPanel v2.1 schematic |
| 2 | Display driver | ✅ Complete | `esp_lcd_panel_rgb`, PSRAM fb, hardware portrait rotation (SWAP_XY+MIRROR_Y) |
| 3 | Touch driver | ✅ Complete | Custom XPT2046 SPI driver, portrait axis remapping (map_x direct, map_y inverted) |
| 4 | LVGL 8.4.0 port | ✅ Complete | Full-frame PSRAM draw buffer, `full_refresh=1`, `LV_DISP_ROT_NONE`, FreeRTOS task, mutex |
| 5 | LVGL config | ✅ Complete | `CONFIG_LV_CONF_SKIP=y` — config via `sdkconfig.defaults`; light theme; Montserrat 12–48pt |
| 6 | `sdkconfig.defaults` | ✅ Complete | PSRAM enabled (N4R2), 240 MHz, 4 MB flash, RGB IRAM-safe, 1 kHz tick |
| 7 | Stale files cleaned | ✅ Done | `hello_world_main.c` and stale `sdkconfig` deleted |
| 8 | `app_main.c` | ✅ Complete | Init sequence, UI refresh task |
| 9 | Sensor manager | ⚠ Stub | API complete; simulation only — real I2C driver Phase 7 |
| 10 | Alarm manager | ✅ Complete | Debounce (3 samples), 50-entry ring buffer, NVS ack |
| 11 | NVS thresholds | ✅ Complete | Load on boot from `ivf_cfg`; save from Settings (Phase 6) |
| 12 | Screen: Splash | ✅ Complete | Progress bar, 6-step timer, auto-advance to Dashboard |
| 13 | UI framework | ✅ Complete | Light theme, header/tab-bar builders, fade navigation, screen registry |
| 14 | Screen: Dashboard | ⬜ Stub | Phase 3 — arc gauge, sparklines, readings not yet implemented |
| 15 | Screen: Chart | ⬜ Stub | Phase 4 — TVOC history chart not yet implemented |
| 16 | Screen: Logs | ⬜ Stub | Phase 5 — data log table not yet implemented |
| 17 | Screen: Settings | ⬜ Stub | Phase 6 — brightness/threshold controls not yet implemented |

---

## 9. What Needs to Be Modified / Completed

### CRITICAL — Must be done before hardware validation

#### 9.1 Replace sensor simulation with real driver

**File:** `main/sensors/sensor_manager.c`  
**What to do:** Replace the `sim_sample()` function with a real I2C/SPI sensor driver.

Recommended sensors for IVF VOC monitoring:

| Sensor | VOC | Temp/Hum | CO₂ | Interface | Notes |
|--------|-----|----------|-----|-----------|-------|
| **ENS160** (ScioSense) | ✅ ppb | ❌ (pair with AHT21) | ❌ eCO₂ | I2C | Best VOC accuracy |
| **BME688** (Bosch) | ✅ index | ✅ | ❌ eCO₂ | I2C / SPI | All-in-one |
| **SGP30** (Sensirion) | ✅ ppb | ❌ | ❌ eCO₂ | I2C | Simpler, less accurate |

**How to integrate:**
```c
// In sensor_manager.c — replace sim_sample() body:
#include "ens160.h"          // your I2C driver
static void real_sample(sensor_data_t *d) {
    ens160_read_voc(&d->voc_ppb);
    aht21_read(&d->temperature_c, &d->humidity_pct);
    d->co2_ppm  = 0.0f;      // set if sensor supports
    d->sensor_ok = true;      // set false on I2C error
    d->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
}
```

#### 9.2 PSRAM — already enabled for N4R2 variant

PSRAM is **enabled** in `sdkconfig.defaults` for the N4R2 (2 MB OPI PSRAM) module.
Both the RGB framebuffer (`fb_in_psram=1`, 261 KB) and the LVGL draw buffer (261 KB) are
allocated from PSRAM. If you are building for a variant without PSRAM (e.g. N4 — 4 MB flash,
no PSRAM), you must:

1. Remove all `CONFIG_ESP32S3_SPIRAM_SUPPORT`, `CONFIG_SPIRAM_*` lines from `sdkconfig.defaults`.
2. In `display/display_driver.c`, change `fb_in_psram = 1` → `0` and `psram_trans_align = 64` → `0`.
3. Reduce the LVGL draw buffer from full-frame (261 KB) to a line buffer in `lvgl_port.c` — internal SRAM may not fit 261 KB.
4. Run `idf.py fullclean && idf.py build`.

Symptom of PSRAM misconfiguration: boot panic / heap corruption at startup.

#### 9.3 Verify touch calibration on your unit

Factory values in `touch_driver.h`:
```c
#define TOUCH_RAW_X_MIN 200
#define TOUCH_RAW_X_MAX 4000
#define TOUCH_RAW_Y_MIN 200
#define TOUCH_RAW_Y_MAX 3600
```
If taps register in wrong positions, run a calibration routine and update these constants.  
The mapping is applied in `touch_driver.c`:`touch_driver_read()` — edit the linear interpolation there.

---

### HIGH PRIORITY — Before production

#### 9.4 Add real-time clock (RTC) to status bar
**File:** `main/ui/screens/screen_dashboard.c`, `s_lbl_time`  
Currently shows `"--:--:--"`. Options:
- **PCF8563** external I2C RTC (best for medical device, keeps time on power loss)
- `esp_sntp` over Wi-Fi if network connectivity is added
- `esp_timer_get_time()` relative time (seconds since boot) as a fallback

#### 9.5 Chart screen data feed
**File:** `main/ui/screens/screen_chart.c`  
`screen_chart_refresh()` is called on tab navigation (`ui.c:ui_goto_screen`) but there is
no background data feed. Wire a data source (history_manager, Phase 4) and call
`screen_chart_refresh()` from `ui_dashboard_refresh()` when the chart screen is active.

#### 9.6 Settings: reload thresholds in sensor_manager after NVS save
**File:** `main/ui/screens/screen_settings.c` → `save_cb()`  
After saving to NVS, add a call to reload thresholds into `sensor_manager` and `alarm_manager`
at runtime (currently requires reboot to take effect).

```c
// Add to save_cb() after nvs_commit():
sensor_manager_reload_thresholds();   // implement this function
alarm_manager_reload_thresholds();    // implement this function
```

#### 9.7 Alarm persistence across reboot
**File:** `main/data/alarm_manager.c`  
The alarm ring buffer lives in RAM — all history is lost on power cycle.  
Implement NVS serialization in `alarm_manager_init()` (load) and `push_alarm()` (save).

#### 9.8 Dashboard status bar time update
**File:** `main/ui/screens/screen_dashboard.c`  
`s_lbl_time` widget exists but is updated with `"--:--:--"` only. Add a 1-second LVGL timer
inside `screen_dashboard_create()` that calls `snprintf` with the RTC/SNTP time.

---

### LOW PRIORITY / Future features

| # | Task | File(s) |
|---|------|---------|
| 9.9 | Add Wi-Fi / MQTT data push | `main/comms/` (new module) |
| 9.10 | Add BLE status broadcast | `main/comms/ble.c` (new) |
| 9.11 | Add SD card data logging (CSV) | `main/data/logger.c` (new) |
| 9.12 | Add system info screen (about) | `main/ui/screens/screen_about.c` (new) |
| 9.13 | Display brightness control (LEDC PWM on GPIO 2) | `display_driver.c` |
| 9.14 | Multi-device support (device ID in NVS) | `app_main.c`, settings screen |
| 9.15 | Watchdog supervision for sensor task | `sensors/sensor_manager.c` |
| 9.16 | OTA firmware update | `main/ota/` (new module) |

---

## 10. Known Limitations & Notes

### `hello_world_main.c`
Deleted. The file was the ESP-IDF hello world template — it is no longer present or compiled.

### `sdkconfig` regeneration
The `sdkconfig` generated from the hello world template had incorrect values
(2 MB flash, PSRAM disabled, 160 MHz CPU, 100 Hz FreeRTOS tick). It has been deleted.
Run `idf.py build` once to regenerate it from `sdkconfig.defaults`.

### First build requires internet
`idf-component-manager` fetches `lvgl/lvgl` (~8.3.x) from the component registry on first build.
Subsequent builds use the local cache at `managed_components/`.
(`espressif/esp_lcd_touch_xpt2046` is NOT used — it does not exist in the registry.
 XPT2046 touch is handled by the custom driver in `main/touch/touch_driver.c`.)

### `LV_COLOR_16_SWAP` decision
Set to `0`. The CrowPanel reference example (Arduino GFX) uses `1` because it calls
`draw16bitBeRGBBitmap` (big-endian). Our `esp_lcd_panel_draw_bitmap` passes data directly
to the DMA engine in native little-endian order. If colours appear inverted, flip this
value in `sdkconfig.defaults` (not `lv_conf.h` — that file is inactive due to `CONFIG_LV_CONF_SKIP=y`).

### Display rotation uses hardware, not LVGL software
`LVGL_ROTATION = LV_DISP_ROT_NONE`. LVGL 8.4.0 software rotation is incompatible with
`full_refresh = 1` (blocked at `lv_refr.c:1181`). Hardware rotation is applied via
`esp_lcd_panel_swap_xy` + `esp_lcd_panel_mirror` in `display_driver.c`. See **Display Rotation**
section in Module Descriptions for the full pixel-path derivation.

### Sensor data is simulated
All readings on the dashboard are generated by a sine-wave simulation in
`sensor_manager.c`. The device will appear fully functional but sensor values are not real
until section 9.1 is completed.

### Thread safety
All LVGL API calls from outside the LVGL task (e.g., `ui_refresh_task`) must be wrapped
with `lvgl_port_lock()` / `lvgl_port_unlock()`. The helper `ui_dashboard_refresh()` does
this internally. Direct `lv_*` calls from other tasks without the lock will cause crashes.

---

### Cross-check against CrowPanel official repository

All driver decisions were verified against:  
`https://github.com/Elecrow-RD/CrowPanel-4.3-HMI-ESP32-Display-480x272/tree/master/example/ESP_IDF`

| Item | CrowPanel Official | Our Implementation | Status |
|------|-------------------|-------------------|--------|
| All 16 RGB data GPIOs | Verified | Matches exactly | ✅ |
| DE/VSYNC/HSYNC/PCLK/BL/AUX GPIOs | Verified | Matches exactly | ✅ |
| Timing (7 MHz pclk, hsync 43/8/4, vsync 12/8/4) | Verified | Matches exactly | ✅ |
| `pclk_active_neg = 1` | `1` | `1` | ✅ |
| `LV_COLOR_16_SWAP` | `0` | `0` | ✅ |
| XPT2046 SPI pins (SCK=12, MISO=13, MOSI=11, CS=0, INT=36) | Verified | Matches exactly | ✅ |
| XPT2046 calibration (X: 4000→0, 200→479; Y: 200→0, 3600→271) | Verified | Matches | ✅ |
| XPT2046 command bytes (0xD0=X, 0x90=Y, 0xB0=Z1) | Verified | Matches | ✅ |
| PSRAM for framebuffer | Disabled in official template | `fb_in_psram = 1`, PSRAM enabled (N4R2) | ✅ |
| LVGL memory allocator | `heap_caps_malloc(MALLOC_CAP_8BIT)` | `heap_caps_malloc(MALLOC_CAP_8BIT)` | ✅ Fixed |
| `LV_DISP_DEF_REFR_PERIOD` | `30` ms | `30` ms | ✅ Fixed |
| Flash size in sdkconfig.defaults | 2 MB (template default) | 4 MB (safe default, adjust per module) | ℹ️ |
| CPU frequency | 160 MHz (template default) | 240 MHz (more headroom for LVGL) | ℹ️ |
