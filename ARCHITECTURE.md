# IVF VOC Monitor — Architecture & Developer Guide

**Project:** IVF Environment Monitoring System (EMS) — VOC / Temperature / Humidity  
**Hardware:** Elecrow CrowPanel ESP32-S3 4.3" HMI (SKU: DIS06043H, v2.1)  
**Framework:** ESP-IDF 5.3.1 (pure — no Arduino layer)  
**UI:** LVGL 8.4.0 (managed component, `idf_component.yml` pins `>=8.3.0, <9.0.0`)  
**Version:** 1.0.0 — Phase 4B (Nav Drawer) complete · Phase 4.1 (Shared UI Framework) complete · Phase 4.2.6 (Hardware Validation Polish) complete · Dashboard FROZEN · UI freeze resolved — LVGL-timer dashboard refresh · Phase 5.1 (Chart UI Migration) complete · Phase 5.2 (Chart Visual Polish) complete · Phase 5.3 (History Manager Backend) complete · Phase 5.4 (Chart Mode Integration & History Binding) complete · Phase 5.4.1 (Real Bitmap Icons) complete · Phase 5.5 (Real Calendar Date Picker) complete · Phase 5.6 (Picker Simplification + Axis Label Fix) complete · Chart screen FROZEN · Phase 5.8 (Logs Screen) complete · Logs screen FROZEN (Phase 5.9) · Phase 6 (Settings Screen + Brightness/Timeout) complete · Phase 6.1 (Font Size, Brightness Floor, Light/Dark Theme) complete · Settings screen FROZEN (Phase 6.2) · Phase 6.3 (Navigation Drawer & Burger Button Responsiveness) complete · **Phase 6.4 (Burger Width Tuning, Instant Drawer, Touch-Passthrough Fix) complete**

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
11. [Technical Debt](#11-technical-debt)
12. [Risk Register](#12-risk-register)

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
├─────────────────────────────────────────────────┤
│           Shared UI Component Layer             │
│  nav_drawer · header · circular_gauge · card    │
│  status_badge · icon_button · assets            │
│  (ui/components/ · ui/assets/)                  │
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
    │   └── display_driver.c        ← esp_lcd_panel_rgb init + backlight + hardware portrait rotation
    │
    ├── touch/
    │   ├── touch_driver.h          ← SPI pin defs, calibration constants, API
    │   └── touch_driver.c          ← SPI2 bus + XPT2046 init via esp_lcd_touch
    │
    ├── lvgl_port/
    │   ├── lv_conf.h               ← LVGL 8.4 config (fonts, widgets, tick source)
    │   ├── lvgl_port.h             ← lvgl_port_init / lock / unlock API
    │   └── lvgl_port.c             ← LVGL task (core 1), flush_cb, touch_read_cb
    │
    ├── ui/
    │   ├── ui.h                    ← Color palette, typography, layout constants, screen IDs
    │   ├── ui.c                    ← Screen manager, shared styles, navigation
    │   ├── nav_drawer.h            ← Legacy navigation drawer API (Phase 4B)
    │   ├── nav_drawer.c            ← Floating menu button + slide-in drawer (Phase 4B)
    │   ├── assets/                 ← Phase 4.1: centralized drawn icon library
    │   │   ├── assets.h            ← assets_draw_wifi / leaf / sd_card / thermometer / clock / chart_icon
    │   │   └── assets.c
    │   ├── components/             ← Phase 4.1: reusable UI component layer
    │   │   ├── voc_gauge/          ← Product-specific TVOC gauge (arc zones, badge, animation)
    │   │   │   ├── voc_gauge.h
    │   │   │   └── voc_gauge.c
    │   │   ├── navigation_drawer/  ← Generic slide-in drawer (decoupled from screen_id_t)
    │   │   │   ├── navigation_drawer.h
    │   │   │   └── navigation_drawer.c
    │   │   ├── header/             ← 272×50 screen header (WiFi/SD/time/title)
    │   │   │   ├── header.h
    │   │   │   └── header.c
    │   │   ├── circular_gauge/     ← Progressive segmented arc gauge (standalone)
    │   │   │   ├── circular_gauge.h
    │   │   │   └── circular_gauge.c
    │   │   ├── card/               ← Rounded card container with optional title
    │   │   │   ├── card.h
    │   │   │   └── card.c
    │   │   ├── status_badge/       ← Pill-shaped GOOD/WARN/ALARM status badge
    │   │   │   ├── status_badge.h
    │   │   │   └── status_badge.c
    │   │   └── icon_button/        ← Circular FAB-style button with symbol
    │   │       ├── icon_button.h
    │   │       └── icon_button.c
    │   └── screens/
    │       ├── screen_splash.h/.c       ← Boot logo + progress bar → auto-advance (complete)
    │       ├── screen_dashboard.h/.c    ← Phase 4.2.3 complete — header_t, card_t, assets; gauge untouched
    │       ├── screen_chart.h/.c        ← Phase 5.2 complete (lv_chart TVOC history — see Section 5)
    │       ├── screen_logs.h/.c         ← STUB — Phase 5 (lv_table data log)
    │       └── screen_settings.h/.c     ← STUB — Phase 6 (brightness slider, thresholds)
    │
    ├── sensors/
    │   ├── sensor_manager.h        ← sensor_data_t, sensor_level_t, public API
    │   ├── sensor_manager.c        ← framework: 1 Hz task, mutex, NVS thresholds, public API
    │   ├── sensor_backend.h        ← backend interface: init() + sample()
    │   ├── sensor_backend_sim.c    ← ACTIVE: sine-wave simulation (swap out in Phase 7)
    │   └── sensor_backend_hw.c     ← STUB: real ENS160+AHT21 (fill TODOs in Phase 7)
    │
    └── data/
        ├── alarm_manager.h         ← alarm_entry_t, alarm_type_t, API
        ├── alarm_manager.c         ← Debounced threshold checks, 32-entry ring buffer
        └── history_manager.c/.h    ← Phase 5.3 complete — single circular buffer, sliced per period
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
7. `ui_init()` → all screens built, nav drawer initialized on `lv_layer_top()`, splash loaded, LVGL 1 Hz dashboard timer started
8. `app_main` returns — FreeRTOS idle task keeps the scheduler alive

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
- Touch callback: `touch_driver_read()` returns `*x = portrait_Y (0–479)` and `*y = portrait_X (0–271)`. `lvgl_touch_read_cb` swaps them — `point.x = y` (portrait_X) and `point.y = x` (portrait_Y) — to match LVGL ROT_NONE 272×480 logical space where x is the 0–271 horizontal axis and y is the 0–479 vertical axis.
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

**Touch mapping** (`touch_driver.c` + `lvgl_port.c`):
- `map_x(raw_x)` → portrait Y (0–479): landscape X maps directly to the portrait vertical axis
- `map_y(raw_y)` → portrait X (0–271): landscape Y maps to the portrait horizontal axis, inverted
- `lvgl_touch_read_cb` (lvgl_port.c) assigns `point.x = map_y()` (portrait_X) and `point.y = map_x()` (portrait_Y), swapping the raw driver outputs to match LVGL's expected (x=horizontal, y=vertical) convention.

**Pixel path:**
```
LVGL renders portrait 272×480 into PSRAM draw buffer
flush_cb: esp_lcd_panel_draw_bitmap(panel, 0, 0, 272, 480, buf)
draw_bitmap SWAP_XY|MIRROR_Y: (lx,ly) → fb[(271-lx)*480 + ly]
ST7262 DMA → 480×272 physical panel
Device mounted left-edge-up → user sees 272×480 portrait ✓
```

---

### `sensors/sensor_manager` + `sensor_backend_*`

`sensor_manager.c` is the **pure framework** — task, mutex, NVS threshold loading, public API.  
It knows nothing about simulation or real hardware; it calls the backend interface:

```
sensor_backend_init()    ← called once at sensor_manager_init()
sensor_backend_sample()  ← called at 1 Hz inside sensor_task()
```

**Active backend:** `sensor_backend_sim.c` (sine-wave simulation).  
**Phase 7 backend:** `sensor_backend_hw.c` (stub — fill ENS160+AHT21 TODOs).  
**To swap:** change one line in `main/CMakeLists.txt` — nothing else changes.

Public API (never changes regardless of backend):
```c
sensor_manager_init();                  // start task + backend init
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
- Alarm types: `VOC_HIGH`, `TEMP_HIGH`, `TEMP_LOW`, `HUM_HIGH`, `HUM_LOW`, `SENSOR_ERROR`
- Persists active state in-RAM only (NVS serialization is Phase 8)

---

### `data/history_manager` — Phase 5.3 (COMPLETE)

The single source of truth for TVOC / temperature / humidity history. Superseded the original
Phase 4C sketch (separate hourly/daily ring buffers, `history_push()`/`history_get_hourly()`/
`history_get_daily()`) with a simpler single-buffer design — see the full write-up under
**Phase 5.3 — History Manager Backend** in Section 9 for the architecture diagram, API
reference, memory estimate, and data-flow diagram.

- One PSRAM-backed circular buffer of hourly records (2160 slots = 90 days); a "period"
  (7D/30D/90D) is just a window size into that same buffer — no separate storage per period
- Fed by `sensor_task()` (`sensor_manager.c`) via `history_manager_add_sample()`, decimated
  from its 1 Hz loop to ~1 call/minute — the only write path
- To be queried by `screen_chart.c` / `screen_logs.c` in Phase 5.4+ via
  `history_manager_get_samples()` / `_get_latest()` / `_get_range()` / `_get_sample_count()` —
  the only read path; **no screen queries it yet**
- Zero dependency on LVGL, any screen, `sensor_manager.h`, or `alarm_manager.h` — callers pass
  plain scalar readings in; `alarm_state` is a reserved field, always 0 until Phase 8
- Boot-relative timestamps (`esp_timer_get_time()`) until an RTC/SNTP source exists (Phase 7+)
- RAM-only — history is lost on reboot until Phase 9 adds NVS/SD persistence (TD-8)

---

### `ui/ui`
**Light consumer theme** — white background, Google Material blue primary, clean typography.

#### Dashboard refresh — LVGL timer (no mutex contention)

Dashboard sensor data and the elapsed-time clock are updated by an LVGL timer, not a FreeRTOS task:

```c
/* ui.c — created once inside ui_init(), after lvgl_port_lock() */
static lv_timer_t *s_dash_timer = NULL;

static void dash_timer_cb(lv_timer_t *t)
{
    (void)t;
    screen_dashboard_update();           /* reads sensor_manager snapshot */

    int64_t us   = esp_timer_get_time();
    int32_t secs = (int32_t)(us / 1000000LL);
    int32_t h    = (secs / 3600) % 24;
    int32_t m    = (secs / 60)  % 60;
    int32_t h12  = h % 12; if (h12 == 0) h12 = 12;
    char buf[12];
    snprintf(buf, sizeof(buf), "%02"PRId32":%02"PRId32" %s", h12, m, h >= 12 ? "PM" : "AM");
    dashboard_set_time(buf);
}

/* In ui_init() — runs inside the existing lvgl_port_lock() block */
s_dash_timer = lv_timer_create(dash_timer_cb, 1000, NULL);
```

`dash_timer_cb` fires inside `lv_timer_handler()` on the LVGL task (Core 1). It runs with the LVGL mutex already held — no additional lock/unlock needed. This eliminates the priority-inversion / mutex starvation that caused the previous `ui_refresh_task` to freeze the UI (see TD-2 resolved, Section 11).

`sensor_manager_get_data()` inside `screen_dashboard_update()` acquires its own separate mutex (sensor manager internal); it does **not** hold the LVGL mutex while waiting, so there is no deadlock risk.

**Colour palette:**

| Constant | Hex | Use |
|----------|-----|-----|
| `IVF_COLOR_BG` | `#FFFFFF` | Screen background |
| `IVF_COLOR_CARD` | `#FFFFFF` | Card/panel fill |
| `IVF_COLOR_BORDER` | `#E0E0E0` | Card borders, header dividers |
| `IVF_COLOR_PRIMARY` | `#1A73E8` | Accent / active nav indicator |
| `IVF_COLOR_TEXT` | `#212121` | Primary text |
| `IVF_COLOR_TEXT_MUTED` | `#757575` | Labels/captions |
| `IVF_COLOR_GOOD` | `#43A047` | Safe level |
| `IVF_COLOR_WARNING` | `#FB8C00` | Warning level |
| `IVF_COLOR_DANGER` | `#E53935` | Alarm level |
| `IVF_COLOR_NAV` | `#F8F9FA` | Header and navigation drawer background |
| `IVF_COLOR_NAV_ACTIVE` | `#1A73E8` | Active nav drawer item (renamed from TAB_ACTIVE in Phase 4B) |
| `IVF_COLOR_NAV_INACTIVE` | `#9E9E9E` | Inactive nav drawer item (renamed from TAB_INACTIVE in Phase 4B) |

**Layout constants (updated in Phase 4B):**

| Constant | Phase 2 value | Phase 4B value | Notes |
|----------|---------------|----------------|-------|
| `IVF_SCREEN_W` | 272 | 272 | unchanged |
| `IVF_SCREEN_H` | 480 | 480 | unchanged |
| `IVF_HEADER_H` | 44 | **50** | Corrected — code always used 50; Architecture.md previously documented 44 incorrectly |
| `IVF_TAB_H` | 50 | — | **removed** — no tab bar |
| `IVF_CONTENT_H` | 386 | **430** | `480 − 50 = 430`; previously documented as 436 (error) |
| `IVF_NAV_BTN_SIZE` | — | 44 | new — floating menu button diameter |
| `IVF_DRAWER_W` | — | 200 | new — navigation drawer panel width |

---

### `ui/components/` — Phase 4.1 Shared UI Component Layer (COMPLETE)

Seven reusable LVGL components, each with an opaque handle struct and malloc/destroy lifecycle:

| Component | Key API | Used by |
|-----------|---------|---------|
| `assets` | `assets_draw_wifi/leaf/sd_card/thermometer/humidity/clock/chart_icon/shield()` | header.c, navigation_drawer.c, all screens |
| `status_badge` | `status_badge_create()`, `status_badge_set_state(GOOD/MODERATE/POOR/DANGER/ERROR)` | Dashboard (Phase 4.2), Logs (Phase 5) |
| `icon_button` | `icon_button_create(&cfg)`, `icon_button_set_click_cb()` | navigation_drawer internally |
| `card` | `card_create(&cfg)`, `card_get_content()`, `card_get_obj()` | Dashboard tiles (Phase 4.2), Settings rows (Phase 6) |
| `circular_gauge` | `circular_gauge_create(&cfg)`, `circular_gauge_set_value_animated(value, ms)` | Dashboard (Phase 4.2) |
| `header` | `header_create(parent)`, `header_set_wifi_strength()`, `header_set_sd_status()`, `header_enable_menu()` | All screens (Phase 4.2) |
| `navigation_drawer` | `navigation_drawer_create(&cfg)`, `navigation_drawer_set_active(id)` | `ui.c` adapter (Phase 4.2) |
| `voc_gauge` | `voc_gauge_create(parent)`, `voc_gauge_set_value(ppb)`, `voc_gauge_set_animation(bool)` | Dashboard (Phase 4.2.4) |

The `navigation_drawer` component is **decoupled from `screen_id_t`** — items carry `uint8_t id`, and the caller's `nav_drawer_cb_t` maps IDs to screen transitions. The integration adapter lives in `ui.c`.

---

### `ui/nav_drawer` — Phase 4B (COMPLETE)

The navigation drawer is a **reusable overlay component** that replaces the bottom tab bar. It is
parented to `lv_layer_top()` so it floats above every screen without being rebuilt per screen.

**Behaviour:**
- **Floating menu button** (`[≡]`): 44×44 px, lower-left corner of the visible area (x=8, y=424),
  always visible on every screen.
- **Open**: tap the menu button → drawer panel slides in from the left over 200 ms
  (`lv_anim_t` on `lv_obj_set_x`, from −200 to 0). A semi-transparent dark overlay
  (`lv_obj` covering x=200..271, full height) blocks taps to the screen below.
- **Close triggers** (any one closes):
  - User selects a menu item (navigation completes, then drawer closes)
  - User taps the overlay (outside the drawer)
  - User taps the menu button again (toggle)
- **Active page highlight**: the current screen's item is styled with `IVF_COLOR_NAV_ACTIVE`
  background and text; all others use `IVF_COLOR_NAV_INACTIVE`.

**Widget hierarchy:**
```
lv_layer_top()
├── nav_btn  (lv_btn, 44×44, x=8, y=424)     ← always visible
│   └── lbl_menu  (lv_label "≡")
├── overlay  (lv_obj, 72×480, x=200, y=0)    ← tap-outside trap, hidden when closed
│   opacity=128, bg=black
└── drawer   (lv_obj, 200×480, x=−200→0)     ← slides from left
    ├── item_dashboard  (lv_btn, 192×48)
    ├── item_chart      (lv_btn, 192×48)
    ├── item_logs       (lv_btn, 192×48)
    └── item_settings   (lv_btn, 192×48)
```

**Public API (`nav_drawer.h`):**
```c
void nav_drawer_init(void);                    // called once from ui_init(); creates lv_layer_top() objects
void nav_drawer_set_active(screen_id_t id);    // called from ui_goto_screen() to update active highlight
void nav_drawer_open(void);                    // optional: open programmatically
void nav_drawer_close(void);                   // optional: close programmatically
```

**Thread safety:** All calls must be made from within `lvgl_port_lock() / lvgl_port_unlock()`.

---

## 5. Screen Flow & UI

Navigation model: **floating menu button + slide-in drawer** (Dashboard / Chart / Logs / Settings).
All screens are created once at `ui_init()` and held in LVGL memory simultaneously. The navigation
drawer is created on `lv_layer_top()` — it persists across screen switches without rebuilding.
Navigation is a `lv_scr_load_anim(LV_SCR_LOAD_ANIM_NONE, 0 ms)` call — instant switch.

**Navigation invariants:**
- **No hierarchical stack** — there is no parent/child screen relationship
- **No tab bar** — the floating menu button and drawer are the sole navigation mechanism
- All four content screens are peer-level (Dashboard, Chart, Logs, Settings)
- Screen transitions: instant (`LV_SCR_LOAD_ANIM_NONE`); no fade, no slide
- `ui_goto_screen(id)` in `ui.c` is the sole entry point for all navigation; it calls
  `nav_drawer_set_active(id)` and `nav_drawer_close()` after the screen switch
- No screen owns the nav drawer — it lives on `lv_layer_top()` independently

```
┌─────────┐  auto (~2.4 s)   ┌───────────┐
│  Splash  ├─────────────────►│ Dashboard │
└─────────┘                  └───────────┘
                                   ▲ ▼  [≡] floating menu button (always visible)
                              ┌────────────────────────────────────┐
                              │  Navigation Drawer (lv_layer_top)  │
                              │  ┌────────────┐                    │
                              │  │  Dashboard │ ← active highlight │
                              │  │  Chart     │                    │
                              │  │  Logs      │                    │
                              │  │  Settings  │                    │
                              │  └────────────┘                    │
                              └────────────────────────────────────┘
                                tap item → instant screen switch + drawer closes
```

### Screen: Splash (272 × 480 portrait)
```
┌────────────────────────┐  ← 272 px wide
│                        │
│    IVF VOC Monitor     │
│  Environmental Monitor │
│   ──────────────────   │
│   ████████████░░░░░    │  progress bar
│   Loading sensors...   │
│                 v1.0.0 │
└────────────────────────┘
```
No nav drawer visible on splash — `nav_drawer_init()` is called after splash auto-advances.

### Screen: Dashboard (272 × 480 portrait) — Phase 4.2.6 complete · **FROZEN**

```
┌── Header 272×50 ─────────────────────────────┐
│ [≡] DASHBOARD              08:25 AM  ≋       │  menu btn x=0; title NORMAL font; time+date
│                           May 24, 2026       │  right-aligned (offset -32 from right edge)
│                                         ≋   │  WiFi icon at far right x=244; no SD icon
├── Content 272×430 ───────────────────────────┤
│          TVOC (ppb)  ← y=4                   │
│    500                                       │  ← pixel-exact label at (136, 40)
│    ╔════════════════════════════╗             │
│ 250║ ●green ●yel ●org ●red     ║ 750         │  210×210 arc, width 18px, ARC_CY=160
│    ║        245                 ║             │  gauge centre: flex stack at (71,103)
│    ║         ppb                ║             │
│    ║     ╔ GOOD ✓ ╗             ║             │
│    ╚════════════════════════════╝             │
│  0                                1000       │  ← pixel-exact at (48,245) and (220,245)
│ ┌──────────────┐  ┌──────────────────┐       │
│ │ 🌡 TEMP      │  │ 💧 HUMIDITY      │       │  124×90 each, CARD_Y=272
│ │ TEMPERATURE  │  │ HUMIDITY         │       │  no sparkline — icon + label + value only
│ │ 28.4 °C      │  │ 63 %             │       │
│ └──────────────┘  └──────────────────┘       │
│                                              │
└──────────────────────────────────────────────┘
```

**Arc gauge geometry (frozen pending Phase 4B layout review):**
| Constant | Value |
|----------|-------|
| `ARC_SIZE` | 210 px |
| `ARC_WIDTH` | 18 px |
| `ARC_CX / ARC_CY` | 136 / 160 (content-relative) |
| `ARC_TOP_X / ARC_TOP_Y` | 31 / 55 |

**Arc gauge zones:**
| Zone | ppb range | Angle | Colour |
|------|-----------|-------|--------|
| Green | 0 – 250 | 135° → 202° | `#43A047` |
| Yellow | 250 – 500 | 202° → 270° | `#FDD835` |
| Orange | 500 – 750 | 270° → 338° | `#FB8C00` |
| Red | 750 – 1000 | 338° → 45° | `#E53935` |

Scale labels: 0, 250, 500, 750, 1000 — **pixel-exact absolute positions** via `make_scale_label_abs(content, text, x, y)`.  
Positions tuned on device (no runtime `cosf/sinf`). Centres: (48,245), (20,125), (136,40), (253,125), (220,245).  
All zone arcs always fully visible (static). Value shown by centre label only — no moving indicator arm.  
`DASH_COLOR_YELLOW = #FDD835` defined locally in `screen_dashboard.c` (not in `ui.h`).

**Dashboard API (screen_dashboard.h):**
```c
lv_obj_t *screen_dashboard_create(void);
void      screen_dashboard_update(void);      /* live — sensor_manager_get_data() at 1 Hz */
void dashboard_set_time(const char *time_str); /* e.g. "08:25 AM" */
void dashboard_set_date(const char *date_str); /* e.g. "May 24, 2025" */
```

### Screen: Chart (272 × 480 portrait) — Phase 5.6 complete: Last 7 Days / Today, live `history_manager` data · **FROZEN**

#### Architecture

The chart screen is a **pure display layer** — it renders only. All aggregation, storage, and
retrieval of sensor history is the sole responsibility of `history_manager.c` (Phase 5.3);
`screen_chart.c` reads it through `apply_chart_mode()` (Phase 5.4) and never touches sensor
state directly.

```
screen_chart.c: apply_chart_mode() ─┬─ history_manager_get_daily_aggregates() ─┐
                                     └─ history_manager_get_range()             ├──► history_manager.c
                                                                                 │    90-day hourly ring buffer
                     sensor_task() → history_manager_add_sample(voc, temp, hum) ┘    (Phase 5.3, wired Phase 5.4)
```

`screen_chart_refresh()` and `screen_chart_create()` both call `apply_chart_mode()` — the
chart/stat cards now show live (simulated-backend) history, not static sample data. See
**Phase 5.4 — Chart Mode Integration & History Binding** in Section 9 for the current widget
layout, mode state diagram, and full API/behaviour reference; the Phase 5.1/5.2 write-up below
is kept as a historical record of what those phases specifically delivered (the period-selector
UI they describe was replaced by Phase 5.4 — the widget hierarchy and screen-layout ASCII
diagrams immediately below are **out of date** for that reason and should not be used as the
current reference).

#### Phase 5.1 / 5.2 — Chart UI Migration & Visual Polish (COMPLETE, superseded by Phase 5.4)

The chart screen shares the same header, card, and drawn-icon components as Dashboard.

**Widget hierarchy (current, post-5.2):**
```
screen_chart (lv_obj, 272×480)
├── header_t   (header_create, title "CHART")          y=0,  h=50   [FROZEN — see 5.2]
└── content    (lv_obj, 272×430)                        y=50
    ├── period_bar  (lv_btnmatrix, 216×34, pill items)  y=8,  x=8
    │   ├── "90 Days"  (active by default)
    │   ├── "30 Days"
    │   └── "07 Days"
    ├── cal_btn     (lv_obj, 34×34, non-clickable)       y=8,  x=230 — assets_draw_calendar() (16×16)
    ├── lbl_title "TVOC (ppb)" (IVF_FONT_NORMAL)         y=50
    ├── legend: [icon] Daily Average  [icon] Maximum     y=50 (right side, same row)
    │   assets_draw_chart_average() / assets_draw_chart_max() — no LV_SYMBOLs
    ├── chart  (lv_chart, 232×160, line mode)            y=84, x=32
    │   ├── bg IVF_COLOR_NAV (light grey), grid softened (LV_OPA_60)
    │   ├── y-axis: 1000/750/500/250/0 (IVF_FONT_SMALL, IVF_COLOR_TEXT_MUTED)
    │   ├── x-axis: "Feb".."Jul" (illustrative — see TD-15)
    │   ├── series s_ser_avg  (IVF_COLOR_GOOD)    — filled via chart_draw_part_cb()
    │   └── series s_ser_max  (IVF_COLOR_WARNING) — line only, no fill
    └── stat cards (2×2 grid, card_t, shadow=false — matches Dashboard, y=260 / y=338)
        ├── AVERAGE (assets_draw_chart_average, 16×16)
        ├── MAX     (assets_draw_chart_max, 16×16)
        ├── MIN     (assets_draw_chart_min, 16×16)
        └── >150 ppb Days (assets_draw_date_range, 16×16)

[≡] menu button — inside header_t (header_enable_menu), not a separate lv_layer_top() overlay
```

**Screen layout (current, post-5.2):**
```
┌── Header 272×50 (header_t, FROZEN) ──────┐
│ [≡] CHART                   08:25 AM  ≋  │
│                             May 24, 2026 │
├── Content 272×430 ────────────────────────┤
│ [ 90 Days ][ 30 Days ][ 07 Days ]  [📅]  │  rounded pill buttons + calendar button
│ TVOC (ppb)         [icn]Daily Average [icn]Maximum │
│ ┌────────────────────────────────────┐   │  232×160, light-grey bg, rounded
│ │ 1000                               │   │  border, softened grid
│ │  750    (green fill under avg)     │   │
│ │  500        ╱‾‾╲___╱‾‾             │   │
│ │  250   orange line, no fill        │   │
│ │    0  Feb  Mar  Apr  May  Jun  Jul │   │
│ └────────────────────────────────────┘   │
│ ┌─────────────┐  ┌─────────────┐         │
│ │ AVERAGE  [i]│  │ MAX      [i]│         │  card_t, shadow=false (Dashboard match)
│ │ 245  ppb    │  │ 820  ppb    │         │
│ └─────────────┘  └─────────────┘         │
│ ┌─────────────┐  ┌─────────────┐         │
│ │ MIN      [i]│  │ >150 ppb [i]│         │
│ │ 82  ppb     │  │ 26  Days    │         │
│ └─────────────┘  └─────────────┘         │
└────────────────────────────────────────────┘
```

**Public API (`screen_chart.h`) — unchanged since Phase 5.1 (signatures only; bodies updated Phase 5.4):**
```c
lv_obj_t *screen_chart_create(void);   // build all widgets; now calls apply_chart_mode() to load real data
void      screen_chart_refresh(void);  // now calls apply_chart_mode(CHART_MODE_LAST_7_DAYS) — see Phase 5.4
```

**Phase 5.1 deliverables:**
- Header migrated to shared `header_t` (pixel identical to Dashboard); title "CHART"
- Period selector re-skinned as rounded pill buttons, relabelled "90 Days/30 Days/07 Days",
  90 Days active by default — `period_cb()` / `apply_period()` logic unchanged
- Calendar button added next to the period selector (decorative — no functionality yet)
- Stat cards migrated to shared `card_t`; `LV_SYMBOL_*` icons replaced with 5 new drawn icons
- Light-green area fill under the average line only, via a `LV_EVENT_DRAW_PART_BEGIN` hook
  (`lv_chart` has no native per-series area fill in LVGL 8.4 — see write-up below)
- Illustrative static sample data so the screen is not empty pending Phase 5.4 (TD-14)
- **Zero dependency on `history_manager`** — still builds and flashes without any data module

**Phase 5.2 deliverables (on top of 5.1, header untouched):**
- Legend rebuilt with drawn icons (`assets_draw_chart_average/_max`) instead of colour dots
- Chart top spacing increased, height reduced, background changed to `IVF_COLOR_NAV`, grid
  softened, more internal padding, tighter/cleaner axis tick marks
- X-axis relabelled to illustrative month names ("Feb".."Jul") matching Figma (TD-15)
- Card shadow flipped to `false` to exactly match Dashboard's sensor tiles; value/unit spacing
  widened; all 4 card icons unified to one 16×16 size (calendar/date-range icons redrawn smaller)

#### Phase 4C — History Manager — ✅ superseded by Phase 5.3

The original Phase 4C sketch on this page (separate hourly/daily ring buffers,
`history_push()`/`history_get_hourly()`/`history_get_daily()`) was superseded during design
review by a simpler single-buffer implementation. See **Phase 5.3 — History Manager Backend**
in Section 9 for the as-built architecture diagram, full API reference
(`history_manager_add_sample()` / `_get_samples()` / `_get_latest()` / `_get_range()` /
`_get_sample_count()` / `_clear()`), memory estimate, and data-flow diagram.

#### Phase 5.4 — Chart Data Binding — ⚠️ redesigned, see Section 9 (PLANNED, not yet implemented)

The period-based plan originally written here (map `period_t` → `history_period_t`, one
`get_samples()` call per period) is **superseded** by a functional design change: the
90D/30D/7D period selector is being removed from the Chart screen entirely, replaced by a
"Last 7 Days" default view and a "Selected Day" calendar view. See **Phase 5.4 — Architecture
Review: Chart Mode Redesign** in Section 9 for the full review (updated API, schema change,
data flow, mode-switching design, statistics-card behaviour, and open questions) — design
approved pending sign-off, no source files changed yet. The chart still shows Phase 5.1's
static illustrative sample data (TD-14) until this is implemented.

**Files to change in Phase 5.4:**
- `main/ui/screens/screen_chart.c` — implement `screen_chart_refresh()`; add the
  `period_t` → `history_period_t` mapping
- No `sensor_manager.c` or `CMakeLists.txt` changes needed — Phase 5.3 already wired the write
  path and build

### Screen: Logs (272 × 480 portrait) — Phase 5.9 complete · **FROZEN**

#### Architecture

Same "pure display layer" principle as Chart: `screen_logs.c` reads `history_manager.c`
through two calls and never touches sensor state or storage directly.

```
screen_logs.c: logs_load_page() ─┬─ history_manager_get_latest_n(skip, 10, buf)  ──► history_manager.c
                                  └─ history_manager_get_count_in_range(0, now)      90-day hourly ring buffer
                                                                                     (Phase 5.3, unchanged)

screen_logs.c: calendar_util_format_datetime() ──► data/calendar_util.c (Phase 5.8, new, shared)
```

**Widget hierarchy:**
```
screen_logs (lv_obj, 272×480)
├── header_t (header_create, title "LOGS")                        y=0,  h=50
└── content  (lv_obj, 272×430)                                    y=50
    ├── top bar (card_t, 256×40)                                  y=8,  x=8
    │   ├── assets_draw_datalog_icon() (18×18) + "Total record: N"
    │   └── "⬆ Export CSV" button (blue, placeholder — see below)
    ├── table card (card_t, 256×330, pad=0)                       y=58, x=8
    │   ├── header row: "DATE & TIME" / "TVOC(ppb)" / "TEMP(C)" / "HUM(%)"
    │   └── rows container (scrollable, flex column, 256×304)
    │       └── one row per history_record_t: coloured dot (green/orange vs
    │           VOC_WARNING_THRESHOLD_PPB) + date/time + tvoc + temp + hum,
    │           bottom-border divider
    └── "Load More ⌄" (centred, blue) — hidden once all loaded rows are
        shown or LOGS_MAX_LOADED_ROWS (100) is hit

[≡] menu button — inside header_t, same as every other screen
```

**Pagination model:** `history_manager_get_latest_n(skip, count, out)` (new API, Phase
5.8) returns the `count` most recent records *newest-first*, skipping the newest `skip`
first — an O(count) backward walk from the ring buffer's write head, not a scan of the
whole buffer. The screen fetches 10 at a time: `screen_logs_create()`/
`screen_logs_refresh()` both reset to `skip=0` and load page 1 fresh (same "always reset
on screen entry" policy as Chart's default mode); "Load More" fetches the next 10 with
the running `skip` and appends them to the scrollable rows container (flex-column layout
— each new row just becomes the next flex item, no manual y-bookkeeping).

**Row resolution:** each row is one **hourly** `history_record_t`, not a raw per-minute
sample. `history_manager` only stores hourly aggregates (TD-16) — storing 90 days at
per-minute resolution would need ~2.5 MB (129,600 records), more than this board's total
2 MB PSRAM, so hourly rows are the only resolution that fits in memory. This is a
deliberate, load-bearing design constraint, not an oversight — see TD-16 (updated).

**Date/time formatting:** boot-relative `timestamp_s` → "24 May, 8:25 AM" via
`calendar_util.c` (Phase 5.8), a new shared module extracted specifically so Logs would
not need to duplicate Chart's private calendar math. **Chart's own copy is deliberately
left untouched** (Chart is FROZEN, Phase 5.7) — see TD-20 for the follow-up to
de-duplicate once Chart is unfrozen.

**Export CSV — placeholder only.** No SD/flash export infrastructure exists yet (Phase 9:
`data/sd_export.c/.h`, still `⬜ PLANNED`), so the button matches the design visually but
its click handler only logs a debug message. See TD-21.

#### What's done

- Header (shared `header_t`, unchanged), top bar (icon + live total-record count + Export CSV
  placeholder), table (header row + scrollable rows), "Load More" pagination, status dots,
  90-day/oldest-dropped retention (free, from the existing `history_manager` ring buffer),
  column layout/margin/overflow fixes (see WORKLOG.md follow-ups).
- Reads exclusively through `history_manager`'s public API — no direct sensor or storage access.
- Visually verified column math is now computed from real label widths (`lv_obj_align_to` +
  `lv_obj_get_x`), not guessed offsets — the class of bug that caused the last two rounds of
  overlap/clipping fixes should not recur for these four columns.

#### What's pending — open questions (not yet decided, tracked here rather than guessed at)

These aren't bugs or TODOs with an obvious answer — they're product/behaviour questions this
implementation deliberately did **not** resolve on its own, because the answer depends on
requirements not yet given:

- **Live refresh while the screen is open.** `screen_logs_refresh()` only runs when the user
  *navigates to* the Logs screen (`ui_goto_screen()`'s existing hook) — there is no periodic
  tick while the user stays on it. If a new hourly record lands while they're looking at the
  list, it will not appear until they leave and come back. Is that acceptable, or does Logs
  need a live/auto-refresh (and if so, on what cadence, and should it preserve scroll
  position / loaded page count, or reset like re-entering does today)?
- **What event should produce a log row.** Today a row is produced once per hour purely because
  that's `history_manager`'s storage granularity (TD-16) — there is no concept of an
  event-driven row (e.g., "log immediately when VOC crosses the alarm threshold" or "log on
  every alarm state change"). `history_record_t.alarm_state` already exists as a field but is
  hardcoded to 0 everywhere (reserved for Phase 8 `alarm_manager` integration) — whether
  alarm events should ever produce their *own* out-of-band log rows, distinct from the regular
  hourly cadence, hasn't been decided.
- **When real data starts.** The active sensor backend is still `sensor_backend_sim.c`
  (simulated), not real hardware (`sensor_backend_hw.c` is a Phase 7 stub) — every row shown
  today, and for however long Phase 7 remains undone, is simulated, not measured, TVOC/temp/
  humidity. Nothing about the Logs screen changes when real hardware lands (it just reads
  `history_manager`, which is backend-agnostic), but it's worth being explicit that "the Logs
  screen works" today does not mean "the numbers in it are real."
- **Whether 90-day/hourly is the right retention/resolution forever**, or a future requirement
  (e.g., regulatory record-keeping) demands something else — flagged, not answered, by TD-16.

### Screen: Settings (272 × 480 portrait) — Phase 6.2 complete · **FROZEN**

#### Architecture

```
screen_settings.c ──┬── data/config_manager.c  (persists every setting to NVS "ivf_cfg")
                     ├── display/display_driver.c  (display_set_brightness() — live preview)
                     ├── display/display_power.c   (reload_settings() — live timeout/brightness apply)
                     ├── sensors/sensor_manager.c  (reload_thresholds() — VOC warn/alarm)
                     └── data/alarm_manager.c      (reload_thresholds() — VOC alarm, real trigger point)
```

**Widget hierarchy** (unlike Dashboard/Chart/Logs, this screen's content scrolls — there are
more rows than fit in 430 px with Alert Settings expanded, its default state):
```
screen_settings (lv_obj, 272×480)
├── header_t (title "SETTINGS")                                   y=0,  h=50
└── content (lv_obj, 272×430, scrollable, flex-column, pad=8/gap=8) y=50
    ├── Brightness (card_t): "Brightness" + live "%" label, lv_slider below
    ├── rows card (card_t, pad=0, flex-column):
    │   ├── Theme — "Light  >" — display-only placeholder, not clickable
    │   ├── Screen Timeout — opens picker {15 sec, 30 sec, 45 sec, 1 min, None}
    │   ├── Threshold (ppb) — opens picker {0, 250, 500, 750, 1000}
    │   └── TVOC High Threshold — opens picker {0, 250, 500, 750, 1000}
    └── Alert Settings card (collapsible, default expanded):
        ├── header (pink bg, bell icon, tap toggles s_alert_content HIDDEN)
        └── s_alert_content (flex-column, reflows automatically on collapse):
            ├── TVOC Alert Threshold + subtitle — picker {0,250,500,750,1000}
            └── High Alert Threshold + subtitle — picker {0,250,500,750,1000}

+ one shared value-picker overlay (backdrop + panel), reused by every
  dropdown-style row above — same "Today/7-Days" overlay pattern Chart
  established, generalized to N labelled options.
```

**Why a single generic picker, not `lv_dropdown`:** no dropdown/slider/roller component
existed anywhere in this codebase before this phase (confirmed by search). Rather than
introduce `lv_dropdown` — a different interaction/visual style from anything else in the
app — the picker reuses Chart's existing "tap a row → centered overlay list of options"
pattern, so this screen looks and behaves consistently with the rest of the app.

#### Two independent VOC threshold pairs — a pre-existing architecture quirk, now unified

Before this phase, `sensor_manager.c` and `alarm_manager.c` each had their **own**,
disconnected copy of "the VOC alarm threshold": `sensor_manager`'s `s_voc_warn_ppb`/
`s_voc_alarm_ppb` (loaded from NVS keys `"voc_warn"`/`"voc_alarm"`, used only for
`sensor_get_voc_level()` — Dashboard/Chart gauge color classification) and
`alarm_manager`'s `VOC_ALARM_PPB` (a hardcoded `#define`, used only to actually raise
`ALARM_VOC_HIGH`). They happened to share the same default (500) but were never wired
together — changing one could never have affected the other, with or without a Settings
screen. This phase fixes that for the critical tier:

- **"TVOC Alert Threshold"** (warning) → `config_manager_set_voc_warn_ppb()` →
  `sensor_manager_reload_thresholds()`. Affects gauge/level color classification only —
  there is no independent "warning" concept in `alarm_manager` (it has exactly one VOC
  alarm type, `ALARM_VOC_HIGH`), so this does not by itself raise or clear an alarm.
- **"High Alert Threshold"** (critical) → `config_manager_set_voc_alarm_ppb()` →
  **both** `sensor_manager_reload_thresholds()` **and** `alarm_manager_reload_thresholds()`.
  This is the real, end-to-end critical-alarm trigger point — `alarm_manager.c`'s
  `VOC_ALARM_PPB` `#define` was replaced with a runtime `s_voc_alarm_ppb`, initialized
  from `config_manager` at boot and reloadable live.

Both reload functions resolve TD-11 ("Settings save requires a reboot to take effect")
for VOC specifically — temperature/humidity thresholds have no Settings UI yet and still
require a reboot.

**Default value change:** the pre-Settings warning default was 150 ppb, which doesn't fall
on the `{0, 250, 500, 750, 1000}` dropdown this screen exposes. `config_manager`'s default
for a **fresh/erased** NVS is 250 (the nearest allowed value) — anyone with an existing
`"voc_warn"=150` in NVS keeps that value; only a clean install sees the new default.

#### "Threshold (ppb)" / "TVOC High Threshold" — persisted, not yet consumed

These two values (display-range reference marker and max-scale) are saved via
`config_manager` and shown correctly in the UI, but **nothing reads them yet** — their
only plausible consumers are Dashboard's gauge and Chart's Y-axis, both of which are
FROZEN (Phase 4.2.6 / Phase 5.7). Wiring them in requires an explicit decision to unfreeze
one or both screens; until then they're inert. See TD-25.

#### Screen dim / wake / timeout (`display/display_power.c`, new)

- **Brightness**: real backlight control via LEDC PWM on `LCD_BL_GPIO` (was a plain GPIO
  on/off toggle before this phase — `display_set_backlight(bool)` is gone, replaced by
  `display_set_brightness(uint8_t percent)`). The slider applies brightness live on every
  drag tick; it's only persisted to NVS on release.
- **Timeout**: a 500 ms `lv_timer` (`ui.c`) calls `display_power_tick()`, which dims the
  backlight to `CONFIG_DIM_BRIGHTNESS_PCT` (15% — see Phase 6.1 below) once `config_manager`'s
  configured timeout elapses with no touch activity. `CONFIG_TIMEOUT_NONE` (0, "None")
  disables dimming outright.
- **Alarm gate**: `display_power_tick()` checks `alarm_manager_active_count()` every tick —
  while it's non-zero the screen never dims (and un-dims immediately if it already had),
  and the idle clock keeps resetting so the timeout counts from when the alarm clears, not
  from whenever the user last touched the screen before it fired.
- **Wake-on-touch, consumed**: `lvgl_port.c`'s touch read callback checks
  `display_power_is_dimmed()` before reporting a press to LVGL. If dimmed, that touch only
  calls `display_power_wake()` (restores brightness, resets the idle clock) and reports
  `LV_INDEV_STATE_RELEASED` for that cycle — the press never reaches any widget. The user
  taps once to wake, again to actually interact (phone-lock-screen pattern), avoiding
  accidental actions the instant the screen lights back up.

#### Phase 6.1 — Font Size, Brightness Floor, Light/Dark Theme

Three follow-up refinements to Phase 6:

**Uniform 12 px body text.** Every label on the Settings screen (Brightness, Theme, Screen
Timeout, the four ppb-threshold rows, the picker overlay) now uses `IVF_FONT_SMALL` — was a
mix of `IVF_FONT_NORMAL` (16 px) and `IVF_FONT_SMALL` (12 px). The header (`header_t`) is
unaffected — it's a separate, unmodified shared component.

**Brightness floor raised to 15%** (`CONFIG_DIM_BRIGHTNESS_PCT`, was 5%) — at 5% the backlight
risked being dark enough that the user couldn't see where to tap to wake it, defeating the
point of a wake-on-touch mechanism. The floor is enforced in three places so it can't be
bypassed: the auto-dim level itself, the brightness slider's minimum (`lv_slider_set_range()`
floors at `CONFIG_DIM_BRIGHTNESS_PCT`, not 0 — a manual setting can't go dimmer than the
auto-dim level either), and a clamp applied to whatever value loads from NVS at boot (in case
an older save predates this floor).

**Light/Dark theme.** `config_manager` gained a persisted `dark_mode` bool. The interesting
part is how the theme actually gets applied without touching any FROZEN screen's source:
`ui.h`'s surface-color macros (`IVF_COLOR_BG/CARD/BORDER/TEXT/TEXT_MUTED/NAV/NAV_ACTIVE/
NAV_INACTIVE`) were redefined from literal `lv_color_hex(...)` values into function-call
macros (`ivf_color_bg()` etc., implemented in `ui.c`, resolving a `s_dark_mode` flag loaded
once at the top of `ui_init()`). Every screen already sets its colors via
`lv_obj_set_style_*(obj, IVF_COLOR_X, 0)` calls using these exact macro names — since the
macro now expands to a function call instead of a literal, **every screen becomes
theme-aware with zero bytes changed in its own source file**, Dashboard/Chart/Logs included.
Semantic status colors (`IVF_COLOR_PRIMARY/GOOD/WARNING/DANGER`) stay literal — brand/status
colors, deliberately identical in both themes.

This does **not** apply live: colors are set once, when each screen is built, and all screens
are built once at boot (`ui_init()`). Making a theme switch instant would mean re-styling
every already-built widget on every screen — a much larger refactor than this feature
warrants. Instead, `screen_settings.c`'s Theme picker calls `esp_restart()` immediately after
persisting the new choice, so the device comes back up already in the new theme — a
deliberate scope decision, not a limitation to fix later.

Dark palette: `BG #121212`, `CARD #1E1E1E`, `BORDER #333333`, `TEXT #ECECEC`,
`TEXT_MUTED #9E9E9E` (same as light's `NAV_INACTIVE`), `NAV #1A1A1A`,
`NAV_ACTIVE #0D3D73`, `NAV_INACTIVE #707070` — standard Material-dark-style values, not
derived from the light palette by any formula.

**Files modified:** `main/ui/ui.h` (macros), `main/ui/ui.c` (resolver functions +
`s_dark_mode` load in `ui_init()`), `main/data/config_manager.h/.c` (`dark_mode` field),
`main/ui/screens/screen_settings.c` (real Theme row + reboot-on-change), plus the two Phase 6
fixes above (`screen_settings.c` font changes, `config_manager.h/.c` + `screen_settings.c`
brightness-floor clamps). No Dashboard/Chart/Logs source files touched — see TD-28.

**Follow-up:** the four simple nav rows (Theme/Screen Timeout/Threshold (ppb)/TVOC High
Threshold) shrunk from 44 px to 18 px each (`NAV_ROW_H`, new — split out from the old shared
`ROW_H`) to fit the whole screen inside 430 px without scrolling, even with Alert Settings
expanded. The Alert Settings header keeps its own `ALERT_HEADER_H` (44 px, unchanged) since it
wasn't part of the ask and would have been too cramped for its icon+title+chevron at 18 px;
the two-line Alert Settings rows (`ALERT_ROW_H`, 56 px) were left alone too. The brightness
slider was also replaced with a dropdown (`BRIGHTNESS_OPTIONS`: 15/25/50/75/100%), matching
every other field's picker pattern — the slider implementation is kept, not deleted, wrapped
in `#if 0`/`#endif`.

**Follow-up — one real fix inside the FROZEN `header_t` component:** the SD-card icon's
default "absent" state (every screen uses this) recolored at `LV_OPA_30`, which *blends with*
rather than replaces the bitmap's native dark pixels — the original dark pixels dominated
regardless of theme, reading as a plausible faint grey by accident against Light's white
header but indistinguishable from Dark's dark header background. Fixed to `LV_OPA_COVER` so
the theme-aware color fully replaces the bitmap's color in both themes. This is a genuine,
narrow exception to the Phase 5.2 header freeze, made only because it was pointed at directly
and asked for — not a unilateral call. `ui/components/header/header.c`, one `case` block.

#### Phase 6.2 — Settings Screen Freeze

The Settings screen (`screen_settings.c/.h`) is now **FROZEN** — same standing as Dashboard,
Chart, and Logs. No further changes without explicit approval.

**What's done:** brightness (dropdown, 15/25/50/75/100%, 15% floor), Light/Dark theme
(reboots to apply), Screen Timeout, Threshold (ppb), TVOC High Threshold, and the two Alert
Settings thresholds — all through one shared value-picker overlay; collapsible Alert Settings
section; uniform 12px body text; fits inside 430px with no scrolling. Real, working
end-to-end changes: VOC warn/alarm actually reach both `sensor_manager` (gauge color
classification) and `alarm_manager` (the real critical-alarm trigger point) with live reload,
no reboot required. Real backlight PWM control replaces the old on/off-only GPIO toggle. One
narrow, explicitly-requested exception was made to the Phase 5.2 header freeze (SD icon
opacity fix, see above) — everything else about Dashboard/Chart/Logs remains untouched.

**What's pending — open questions, not bugs** (carried over from Phase 6/6.1, not resolved by
freezing, listed here so freezing doesn't bury them):
- **Display-range values persisted, not consumed** (TD-25) — "Threshold (ppb)" and "TVOC High
  Threshold" have no effect on Dashboard's gauge or Chart's Y-axis; both screens are frozen,
  so wiring them in needs an explicit unfreeze decision.
- **Temp/humidity thresholds have no Settings UI or live-reload** (TD-26) — only VOC got that
  treatment in Phase 6; temp/humidity still require a reboot to change, same as before.
- **Not flashed/verified on real hardware** (TD-27, TD-28) — the LEDC brightness curve, the
  wake-touch consumption, and dark-mode contrast/legibility across every screen all need a
  real device to confirm. This is the single biggest open item before calling any of Phase 6
  truly done, not just code-complete.

**Files affected:** none — status declaration only.

#### Phase 6.3 — Navigation Drawer & Burger Button Responsiveness

Reported: the nav drawer feels laggy when opened, and the burger button seems to need a
harder press. Investigated three candidate causes for the button before changing anything —
touch-driver pressure threshold (ruled out, negligibly low), the Phase 6 wake-touch-swallow
logic (ruled out, only fires after idle-dim, not on an already-awake screen), and the
button's actual hit area — which turned out to be the real bug: `header.c`'s `HDR_BTN_W` was
`20`, contradicting the file's own header comment, which has always documented a 44px button.
A 20px target combined with LVGL's default 10px scroll-tolerance (touches that drift further
than that before release are treated as drags, not clicks) fits "needs a harder/more precise
press" far better than any hardware theory.

**Changes:** `navigation_drawer.c`'s open/close animation cut from 220ms to 120ms (kept, not
removed — the actual lag source is that this display does a full-screen redraw every
animation frame, `full_refresh=1`, so per-frame cost is throughput-bound regardless of the
nominal duration). `header.c`'s `HDR_BTN_W` restored from 20 to the documented 44 — the
second narrow, explicitly-requested exception to the Phase 5.2 header freeze (first was the
SD-icon opacity fix). `navigation_drawer.c` itself was never frozen.

**Disclosed trade-off:** widening the button shrinks the title's available width from 101px
to 77px (`LV_LABEL_LONG_CLIP`, truncates silently). "DASHBOARD" (9 chars, the longest title
in use) may not fit at 77px by rough estimate — unverifiable without hardware. Made anyway
per explicit instruction to try 44px and revert if it visibly breaks anything; reverting is
a single-constant change (`HDR_BTN_W` back to 20).

**Files modified:** `ui/components/navigation_drawer/navigation_drawer.c`,
`ui/components/header/header.c`.

**Known limitation:** not flashed/verified — whether "DASHBOARD" clips at the new title
width, and whether 120ms actually reads as smooth given the redraw-cost floor is unchanged,
both need a real device to confirm.

---

#### Phase 6.4 — Burger Width Tuning, Instant Drawer, Touch-Passthrough Fix

Reported on real hardware, after Phase 6.3: (1) "DASHBOARD" clips its last letter — the exact
risk disclosed in Phase 6.3/TD-29 at 44px, now confirmed; (2) the 120ms drawer animation still
reads as slow; (3) a new bug — while the drawer is open, the screen behind it still responds to
touch in the dimmed area.

**Changes:**
- `header.c`'s `HDR_BTN_W` 44 → 30 — a middle ground between the original too-small 20px and
  the too-wide 44px that clipped. Gives the title 91px (up from 77px at 44px), enough for
  "DASHBOARD" (9 chars) to render without `LV_LABEL_LONG_CLIP` truncating it. Third narrow,
  explicitly-requested exception to the Phase 5.2 header freeze.
- `navigation_drawer.c`'s `slide()` no longer animates — it sets the drawer's x-position
  directly and shows/hides the backdrop in the same call, per request to try instant
  appearance instead of the 120ms animation. The animated version (and its
  `drawer_anim_done_cb` ready-callback) is preserved in an `#if 0` block, not deleted; the
  backdrop-hide-on-close logic that callback used to do now runs inline in `slide()`.
- Touch passthrough: `lv_layer_top()`'s full-screen clickable backdrop is the standard LVGL
  pattern for blocking clicks to whatever is beneath a modal overlay, and LVGL's indev
  hit-testing is documented to check the top layer ahead of the active screen for exactly this
  reason — so this shouldn't have been reachable by design. This project has no vendored LVGL
  source available in this environment to trace the actual dispatch order and confirm why it
  was, so the fix does not depend on that explanation: `lvgl_touch_read_cb` (`lvgl_port.c`) now
  inspects the raw touch coordinate before LVGL's own object search ever runs. While the drawer
  is open, a touch at `x >= IVF_DRAWER_W` (the dimmed area outside the drawer's own column) is
  swallowed and closes the drawer directly via the new `ui_nav_drawer_close_from_touch()`
  (`ui.c`/`ui.h`) — the press is reported released, so LVGL never dispatches it to anything on
  the active screen. Taps within the drawer's own column are untouched, still handled by
  normal LVGL dispatch.

**Files modified:** `ui/components/header/header.c`,
`ui/components/navigation_drawer/navigation_drawer.c`, `ui/ui.h`, `ui/ui.c`,
`lvgl_port/lvgl_port.c`.

**Known limitations:** not flashed/verified — whether "DASHBOARD" now fits at 30px, whether the
instant drawer feels right versus jarring, and whether the touch-passthrough fix actually
resolves the reported symptom all need a real device. The fix only covers touches in the dimmed
area outside the drawer; taps landing directly on the drawer's own 200px column still rely on
LVGL's normal top-layer dispatch, whose exact precedence on this build was not independently
confirmed — see TD-30.

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
| 2 | Display driver | ✅ Complete, brightness added Phase 6 | `esp_lcd_panel_rgb`, PSRAM fb, hardware portrait rotation (SWAP_XY+MIRROR_Y); `display_set_brightness(0-100)` via LEDC PWM (was on/off-only `display_set_backlight(bool)` before Phase 6) |
| 3 | Touch driver | ✅ Complete | Custom XPT2046 SPI driver, portrait axis remapping (map_x direct, map_y inverted) |
| 4 | LVGL 8.4.0 port | ✅ Complete | Full-frame PSRAM draw buffer, `full_refresh=1`, `LV_DISP_ROT_NONE`, FreeRTOS task, mutex. Touch read callback swaps x↔y to correct axis convention (see Display Rotation). |
| 5 | LVGL config | ✅ Complete | `CONFIG_LV_CONF_SKIP=y` — config via `sdkconfig.defaults`; light theme; Montserrat 12–48pt |
| 6 | `sdkconfig.defaults` | ✅ Complete | PSRAM enabled (N4R2), 240 MHz, 4 MB flash, RGB IRAM-safe, 1 kHz tick |
| 7 | Stale files cleaned | ✅ Done | `hello_world_main.c` and stale `sdkconfig` deleted |
| 8 | `app_main.c` | ✅ Complete | Init sequence; `app_main` returns after `ui_init()` — no refresh task |
| 9 | Sensor manager + backend | ✅ Phase 3B complete | Framework separated from backend. Active: `sensor_backend_sim.c`. Real driver goes in `sensor_backend_hw.c` (Phase 7). |
| 10 | Alarm manager | ✅ Complete | Debounce (3 samples), 50-entry ring buffer, NVS ack |
| 11 | NVS thresholds | ✅ Complete, TD-11 resolved for VOC | Load on boot from `ivf_cfg` via `config_manager`; VOC warn/alarm now saved live from Settings (Phase 6) with no-reboot reload (`sensor_manager_reload_thresholds()`, `alarm_manager_reload_thresholds()`); temp/humidity thresholds still boot-only |
| 12 | Screen: Splash | ✅ Complete | Progress bar, 6-step timer, auto-advance to Dashboard |
| 13 | UI framework | ✅ Phase 4B complete | Light theme, header builder, instant navigation. Phase 4B: nav drawer on `lv_layer_top()` replaces tab bar. Phase 4.1: reusable component layer added. |
| 14 | Screen: Dashboard | ✅ Phase 4.2.6 complete · **FROZEN** | `header_t` + `card_t` + `voc_gauge_t`; title "DASHBOARD"; WiFi far right (x=244); SD removed; sparklines removed; `CARD_H=90`; full-screen drawer with new top section. |
| 15 | Screen: Chart | ✅ Phase 5.6 complete · **FROZEN** | `header_t` (frozen) + `card_t` + real bitmap icon assets (Phase 5.4.1); Last 7 Days (default) / Today (simple 2-row dropdown, Phase 5.6) modes via `chart_mode_t` + `apply_chart_mode()`; live `history_manager` data (daily aggregates / hourly range); title + calendar button share Row A, dynamic title text ("Last 7 Days" / "Today"), mode-aware 4th stat card ("Days"/"Hours"), real-date X-axis labels (day-of-month for Last 7 Days, 4-hour boundaries for Today); axis tick labels visible (Phase 5.6 `clip_corner` fix); no return chip — the dropdown's "7 Days" option covers that |
| 15.1 | History Manager backend | ✅ Phase 5.3 complete, API revised Phase 5.4, extended Phase 5.8 | `data/history_manager.c/.h` — 90-day hourly ring buffer (PSRAM, ~59 KB with min/max), fed from `sensor_manager.c`'s `sensor_task()`; read by Chart (`get_range`/`get_daily_aggregates`/`compute_stats`) since Phase 5.4, and by Logs (`get_latest_n`, new Phase 5.8) for newest-first pagination |
| 15.2 | Calendar utility (`data/calendar_util.c/.h`) | ✅ Phase 5.8 complete | Shared, LVGL-independent boot-ts → calendar-date/time conversion (Hinnant algorithm), used by Logs; Chart keeps its own private pre-existing copy (frozen, see TD-20) |
| 16 | Navigation Drawer (`nav_drawer`) | ✅ Phase 4B complete | Floating `[≡]` button + slide-in drawer on `lv_layer_top()`; replaces bottom tab bar |
| 16.1 | Shared UI components (`components/`) | ✅ Phase 4.1 complete | 7 reusable components: `navigation_drawer`, `header`, `circular_gauge`, `card`, `status_badge`, `icon_button`, `assets` |
| 16.2 | `header_enable_menu()` | ✅ Phase 4.2.1 complete | Hamburger `[≡]` button in header; leaf icon hidden; title repositioned; callback-based — header does not own the drawer |
| 16.3 | Navigation Drawer Wiring (`navigation_drawer` + `ui.c`) | ✅ Phase 4.2.2 complete | `navigation_drawer_t` integrated into `ui.c`; FAB visible via `create_fab=true`; `ui_nav_drawer_toggle()` added to `ui.h`; drawer items with NAVIGATE header, active highlight (bg+icon+text), pressed state |
| 16.4 | Dashboard Migration | ✅ Phase 4.2.3 complete | `screen_dashboard.c` migrated to `header_t` + `card_t` + `assets_draw_*()`; `create_fab=false`; gauge code untouched |
| 16.5 | VOC Gauge Component | ✅ Phase 4.2.4 complete | `voc_gauge_t` — product-specific TVOC gauge; progressive arc zones, badge (GOOD/MODERATE/POOR/UNHEALTHY), 500 ms animation; TD-13 (`circular_gauge.c` font fix) resolved |
| 16.6 | Dashboard Final Polish | ✅ Phase 4.2.5 complete | Header 80 px right column (SD x=160, WiFi x=136); time/date right-aligned via `LV_ALIGN_TOP_RIGHT`; title fixed-width + `LV_LABEL_LONG_CLIP`; humidity `lbl_name` x 18→22; VOC gauge initialises to `NO_READING`; MODERATE badge dark text; `nav_drawer.c` removed from build |
| 16.7 | Hardware Validation Polish | ✅ Phase 4.2.6 complete | WiFi far right (`HDR_WIFI_X=244`); SD icon removed; `HDR_TIME_ROFS=32`; title font `IVF_FONT_NORMAL`; title "DASHBOARD"; sparklines removed (`CARD_H=90`); drawer full-screen (480 px, y=0); `DRAWER_HEADER_H=148`; new top section (blue circle + shield + badge + title + pill); "TVOC Chart" / "Data Logs" nav item labels; version footer; `assets_draw_shield()` added; `assets_draw_humidity()` updated (16×22 teardrop) |
| 16.8 | UI Freeze Fix | ✅ Phase 4.2.7 complete | `ui_refresh_task` removed from `app_main.c`; `ui_dashboard_refresh()` removed from `ui.c`/`ui.h`; dashboard refresh moved to `lv_timer_create(dash_timer_cb, 1000)` inside LVGL task; root cause: same-priority mutex starvation between tasks caused `lv_timer_handler()` skips, freezing animations and touch. TD-2 resolved. |
| 17 | Screen: Logs | ✅ Phase 5.9 complete · **FROZEN** | `header_t` + `card_t` + `datalog_icon.c` bitmap; table card (header row + scrollable flex-column rows) reading `history_manager_get_latest_n()`; 10 rows/page, "Load More" pagination (capped at `LOGS_MAX_LOADED_ROWS`=100); Export CSV button is a visual placeholder (no SD storage yet, Phase 9); open questions on refresh cadence/event-driven rows tracked, not yet answered |
| 18 | Screen: Settings | ✅ Phase 6.2 complete · **FROZEN** | `header_t` + `card_t`; scrollable-if-needed content (flex-column, currently fits without scrolling); uniform 12px body text; Brightness/Theme/Screen Timeout/Threshold (ppb)/TVOC High Threshold/TVOC Alert Threshold/High Alert Threshold — all dropdowns via one shared value-picker overlay (brightness slider kept, `#if 0`'d, not deleted); collapsible Alert Settings section |
| 18.1 | Settings persistence (`data/config_manager.c/.h`) | ✅ Phase 6 complete, `dark_mode` added Phase 6.1 | NVS namespace `ivf_cfg` (shared with sensor_manager's pre-existing keys); brightness (15% floor)/timeout/display-range/VOC-warn/VOC-alarm/dark_mode; deliberately passive — never calls into display/sensor/alarm managers itself |
| 18.2 | Screen dim/wake/timeout (`display/display_power.c/.h`) | ✅ Phase 6 complete, 15% floor Phase 6.1 | 500 ms tick (`ui.c`); dims to 15% (was 5%) after configured idle timeout; never dims during an active unacknowledged alarm (`alarm_manager_active_count()` gate); wake-on-touch consumes that touch (`lvgl_port.c`) rather than passing it through to a widget |
| 18.3 | Light/Dark theme (`ui.h`/`ui.c`) | ✅ Phase 6.1 complete, new | `IVF_COLOR_*` surface macros redefined as function calls resolving a boot-time `s_dark_mode` flag — every screen (including frozen ones) becomes theme-aware with no source changes of its own; applies on next boot only (`esp_restart()` from Settings), not live — see TD-28 |

---

## 9. What Needs to Be Modified / Completed

### Phase Roadmap

| Phase | Title | Status | Key files |
|-------|-------|--------|-----------|
| 4A | Chart UI Layout | ✅ COMPLETE | `screen_chart.c/.h` |
| 4B | Navigation Drawer | ✅ COMPLETE | `ui/nav_drawer.c/.h`, `ui/ui.h`, all `screen_*.c` |
| 4.1 | Shared UI Framework | ✅ COMPLETE | `ui/components/`, `ui/assets/` — 7 new components |
| 4.2 | Dashboard Component Integration | ✅ COMPLETE (4.2.6 Hardware Validation Polish · Dashboard FROZEN) | `header.c`, `navigation_drawer.c/.h`, `assets.c/.h`, `screen_dashboard.c`, `ui.c` — WiFi far right, SD removed, full-screen drawer, new top section, sparklines removed |
| 4.2.7 | UI Freeze Fix | ✅ COMPLETE | `app_main.c`, `ui.c`, `ui.h` — `ui_refresh_task` removed; LVGL timer `dash_timer_cb` replaces cross-task dashboard refresh; mutex starvation root cause eliminated |
| 5.1 | Chart UI Migration | ✅ COMPLETE | `screen_chart.c`, `assets.c/.h` — header_t + card_t + assets migration, Figma-matched styling, no data-layer change |
| 5.2 | Chart Visual Polish | ✅ COMPLETE | `screen_chart.c`, `assets.c/.h` — legend icons, chart spacing/background/grid/axis polish, card shadow/spacing match Dashboard |
| 5.3 | History Manager Backend | ✅ COMPLETE | `data/history_manager.c/.h` (new), `sensor_manager.c`, `app_main.c`, `CMakeLists.txt` — supersedes the old Phase 4C sketch |
| ~~4C~~ | ~~History Manager~~ | ✅ done via 5.3 | see Phase 5.3 |
| 5.4 | Chart Mode Integration & History Binding (Last 7 Days / Selected Day) | ✅ COMPLETE | `data/history_manager.c/.h`, `screen_chart.c` — mode enum, central controller, real history_manager data, day picker, dynamic title/legend/stats |
| 5.4.1 | Real Bitmap Icons for Chart | ✅ COMPLETE | 5 `ui/assets/*_icon.c` bitmap files fixed (hyphen→underscore identifiers) and wired in; `assets.c/.h`, `screen_chart.c`, `CMakeLists.txt` |
| 5.5 | Real Calendar Date Picker | ✅ COMPLETE, superseded by 5.6 | `screen_chart.c` — `lv_calendar` grid picker bounded to the 90-day retention window, dependency-free Hinnant calendar math |
| 5.6 | Picker Simplification + Axis Label Fix | ✅ COMPLETE | `screen_chart.c` — calendar grid replaced with plain Today/7-Days dropdown; `clip_corner` removed (was hiding both axes' tick labels); title moved onto the calendar-button row; return chip removed (dropdown covers it); chart enlarged into reclaimed space (`CHART_H` 150→180) |
| 5.7 | Chart Screen Freeze | ✅ COMPLETE | `screen_chart.c` — Chart screen declared **FROZEN**; no further changes without explicit approval, same standing as Dashboard |
| 5.8 | Logs Screen | ✅ COMPLETE | `screen_logs.c` (rewritten), `data/history_manager.h/.c` (new `get_latest_n()`), `data/calendar_util.c/.h` (new, shared), `ui/assets/datalog_icon.c` (fixed + wired), `assets.c/.h`, `CMakeLists.txt` |
| 5.9 | Logs Screen Freeze | ✅ COMPLETE | `screen_logs.c` — Logs screen declared **FROZEN**; no further changes without explicit approval, same standing as Dashboard/Chart. Open questions (refresh cadence, event-driven rows, real-data timing) documented, not resolved. |
| 6 | Settings Screen | ✅ COMPLETE | `screen_settings.c` (rewritten), `data/config_manager.c/.h` (new), `display/display_power.c/.h` (new), `display_driver.c/.h` (brightness PWM), `sensor_manager.c/.h` + `alarm_manager.c/.h` (reload_thresholds), `lvgl_port.c` (wake-touch), `ui.c` (power timer) |
| 6.1 | Font Size, Brightness Floor, Light/Dark Theme | ✅ COMPLETE | `screen_settings.c` (12px body text, real Theme row), `config_manager.h/.c` (15% brightness floor, `dark_mode`), `ui.h`/`ui.c` (theme-aware color macros, `esp_restart()`-on-change) |
| 6.2 | Settings Screen Freeze | ✅ COMPLETE | `screen_settings.c` — Settings screen declared **FROZEN**; no further changes without explicit approval, same standing as Dashboard/Chart/Logs |
| 6.3 | Navigation Drawer & Burger Button Responsiveness | ✅ COMPLETE | `navigation_drawer.c` (slide animation 220ms→120ms), `header.c` (`HDR_BTN_W` 20→44, restoring the file's own documented-but-unimplemented design — see TD-29 for the title-width trade-off) |
| 6.4 | Burger Width Tuning, Instant Drawer, Touch-Passthrough Fix | ✅ COMPLETE | `header.c` (`HDR_BTN_W` 44→30, confirmed clipping fixed), `navigation_drawer.c` (animation replaced with instant jump, `#if 0`'d not deleted), `ui.h`/`ui.c` (new drawer-state accessors), `lvgl_port.c` (raw-touch gate closes drawer directly instead of relying on LVGL top-layer dispatch — see TD-30) |
| 7 | Sensor Framework | ⬜ PLANNED | `sensors/sensor_backend_hw.c`, ENS160 + AHT21 driver files |
| 8 | Alarm Framework | ⬜ PLANNED | `data/alarm_manager.c/.h`, alarm UI |
| 9 | Storage Framework | ⬜ PLANNED | `data/history_manager.c` (NVS/SD persistence), `data/sd_export.c/.h`, SNTP |
| 10 | Production Hardening | ⬜ PLANNED, display-dim done via Phase 6 | OTA, watchdog, memory audit — screen backlight dim-on-timeout now exists (`display_power.c`); true display/CPU sleep still not implemented |

---

### Phase 4A — Chart UI Layout (COMPLETE)

**File:** `main/ui/screens/screen_chart.c`  
See **Screen: Chart** in Section 5 for the complete widget hierarchy, layout spec, and API.

**Delivered:**
- Period selector toggle group (7D / 30D / 90D) with mutual-exclusion `lv_btnmatrix`
- `lv_chart` in line mode, full content width, y-axis 0–1000 ppb
- Three series: TVOC (`IVF_COLOR_PRIMARY`), warn threshold flat at 300 ppb, alarm threshold flat at 500 ppb
- `lbl_no_data` placeholder centred over chart, visible until Phase 4D data arrives
- `screen_chart_create()` and `screen_chart_refresh()` (no-op skeleton until Phase 4D)
- Zero dependency on `history_manager` (added in Phase 4C)

**Also fixed in this phase (pre-existing bugs surfaced during Phase 4A testing):**
- **Touch coordinate axis swap** (`lvgl_port.c`): `touch_driver_read()` returns portrait_Y in `*x` and portrait_X in `*y`. `lvgl_touch_read_cb` was assigning them straight to `point.x` and `point.y`, so LVGL received the axes inverted — all tab-bar taps (portrait Y ≥ 430) exceeded the 0–271 LVGL x-range and were silently rejected. Fixed by swapping: `point.x = y` (portrait_X), `point.y = x` (portrait_Y).
- **Screen transition performance** (`ui.c`): `LV_SCR_LOAD_ANIM_FADE_IN` (200 ms) requires per-pixel alpha blending of two full 272×480 PSRAM frames per animation frame — very slow with `full_refresh=1`. Changed to `LV_SCR_LOAD_ANIM_NONE` for instant, single-flush screen switches.

---

### Phase 4B — Navigation Drawer (COMPLETE)

**Delivered:**
- `main/ui/nav_drawer.h` / `nav_drawer.c` — floating `[≡]` button (44×44) + 200 px panel on `lv_layer_top()`
- Slide animation via `lv_anim_t`; mid-animation reversal handled by reading `lv_obj_get_x()` as from-value
- Backdrop: semi-transparent `lv_obj` on `lv_layer_top()`, click-to-close
- `IVF_HEADER_H = 50` (corrected from 44), `IVF_CONTENT_H = 430` (corrected from 436)
- `IVF_TAB_H` removed; `IVF_NAV_BTN_SIZE=44`, `IVF_DRAWER_W=200` added
- `ui_build_tab_bar()` removed from `ui.c` and all four `screen_*.c` files
- Dashboard: progressive gauge (INDICATOR-fill zones), drawn icons (no Unicode symbols), badge "GOOD/WARN/ALARM"
- `drawer_x_exec_cb(void *var, int32_t val)` wrapper avoids `lv_coord_t` / `int32_t` type mismatch

**Files created:** `main/ui/nav_drawer.h`, `main/ui/nav_drawer.c`  
**Files modified:** `main/ui/ui.h`, `main/ui/ui.c`, `main/ui/screens/screen_dashboard.c`, `screen_chart.c`, `screen_logs.c`, `screen_settings.c`, `main/CMakeLists.txt`

---

### Phase 4.1 — Shared UI Framework (COMPLETE)

See the full description in Section 4 under **`ui/components/`**.

**Delivered:** 7 reusable UI components with headers + implementation stubs.

**Files created (14 files):**
- `main/ui/assets/assets.h` / `assets.c`
- `main/ui/components/status_badge/status_badge.h` / `status_badge.c`
- `main/ui/components/icon_button/icon_button.h` / `icon_button.c`
- `main/ui/components/card/card.h` / `card.c`
- `main/ui/components/circular_gauge/circular_gauge.h` / `circular_gauge.c`
- `main/ui/components/header/header.h` / `header.c`
- `main/ui/components/navigation_drawer/navigation_drawer.h` / `navigation_drawer.c`

**Files modified:** `main/CMakeLists.txt` — 7 new SRCS, 8 new INCLUDE_DIRS

---

### Phase 4.2 — Dashboard Component Integration (COMPLETE)

**Goal:** Migrate `screen_dashboard.c` to use all Phase 4.1 components. Navigation drawer moved to `ui.c` ownership. Header gains hamburger button. Status strip added. Incremental refactor — the working gauge animation is preserved at each step.

#### Phase 4.2.1 — Header Extension (COMPLETE)

Added `header_enable_menu(hdr, cb, user_data)` to the `header_t` component.

- `build_leaf_icon()` refactored to return an `lv_obj_t *` container (`leaf_cont`) — the two leaf primitives are hidden in a single flag call.
- `header_enable_menu()` hides `leaf_cont`, creates a 44×50 px `lv_btn` (`LV_SYMBOL_LIST`) at x=0, shifts the title label from x=34 to x=48, and registers `menu_btn_event_cb` on `LV_EVENT_CLICKED`.
- Idempotent — safe to call more than once (button created only on first call).
- The header has zero knowledge of the drawer or `screen_id_t`.

**Files modified:** `main/ui/components/header/header.h`, `main/ui/components/header/header.c`

#### Phase 4.2.2 — Navigation Drawer Wiring (COMPLETE)

`navigation_drawer.c` and `ui.c` migrated to the new component API. `nav_drawer.c` kept in CMakeLists until Phase 4.2.5 dashboard migration is verified.

**Changes:**
- `navigation_drawer.h`: added `bool create_fab` to `nav_drawer_cfg_t` — `true` = FAB owned by drawer, `false` = header owns the trigger (Phase 4.2.5+).
- `navigation_drawer.c`:
  - Drawer panel gains a "NAVIGATE" label + 1 px divider before items (`DRAWER_HEADER_H = 54`).
  - Item rows positioned at `DRAWER_HEADER_H + i × ITEM_H` (was `i × ITEM_H`).
  - Item rows gain `IVF_COLOR_BORDER` pressed-state background (suppresses default LVGL blue).
  - Separator lines repositioned to match new item y-coordinates.
  - FAB creation wrapped in `if (d->cfg.create_fab)`.
  - `navigation_drawer_set_active()` now updates icon label, text label, and background color for active/inactive states (was background-only).
- `ui.c`: migrated from `nav_drawer_init/close/set_active()` to `navigation_drawer_create/close/set_active/toggle()`; `APP_NAV_ITEMS[]` static array + `on_nav_item_selected()` callback; `create_fab = true` for this phase.
- `ui.h`: added `ui_nav_drawer_toggle()` declaration.
- `CMakeLists.txt`: added legacy comment on `nav_drawer.c` line.

**Files modified (4):** `navigation_drawer.h`, `navigation_drawer.c`, `ui.c`, `ui.h`, `CMakeLists.txt`

#### Phase 4.2.3 — Dashboard Migration (COMPLETE)

Refactored `screen_dashboard.c` to consume `header_t`, `card_t`, and `assets_draw_*()`. No redesign, no business logic change — only duplication removed.

**Changes:**
- Replaced `ui_build_header()` call + `s_lbl_time` / `s_lbl_date` statics with `header_create()` + `header_set_*()` + `header_enable_menu(hdr, on_menu_btn, NULL)`.
- `on_menu_btn()` callback calls `ui_nav_drawer_toggle()` — drawer coupling is one function call, no handle.
- `dashboard_set_time()` / `dashboard_set_date()` now forward to `header_set_time()` / `header_set_date()` via `s_hdr`.
- Replaced `sty_card` static style + inline `lv_obj_create` in `build_sensor_card()` with `card_create(&ccfg)` + `card_get_obj()` for positioning.
- `assets_draw_thermometer()` / `assets_draw_humidity()` replace the five removed local icon functions (`make_leaf_icon`, `make_wifi_icon`, `make_sd_icon`, `make_therm_icon`, `make_drop_icon`).
- Header title font changed from `IVF_FONT_NORMAL` (16 pt) to `IVF_FONT_SMALL` (12 pt) in `header.c` — "AIR QUALITY MONITOR" at 16 pt (~170 px) overflows available space (~114 px with menu button enabled).
- `ui.c`: `create_fab = false` — FAB removed; header button is the sole nav drawer trigger.
- All gauge code (`make_arc_zone`, `gauge_set_value`, `gauge_anim_exec_cb`, `gauge_update_animated`, `make_scale_label_abs`, `build_gauge`) copied verbatim — geometry unchanged.

**Files modified (3):**

| File | Change |
|------|--------|
| `main/ui/screens/screen_dashboard.c` | Full rewrite — header_t, card_t, assets; gauge code untouched |
| `main/ui/components/header/header.c` | Title font: `IVF_FONT_NORMAL` → `IVF_FONT_SMALL` |
| `main/ui/ui.c` | `create_fab = false` |

#### Phase 4.2.4 — VOC Gauge Component (COMPLETE)

Created `ui/components/voc_gauge/` — a product-specific TVOC gauge component. All gauge logic removed from `screen_dashboard.c` and encapsulated in the component.

**`voc_gauge_t` internals:**
- 272 × 268 px root container at `(0,0)` in parent (full content width, gauge section height)
- Grey background arc (full 135°→45° sweep), four progressive zone arcs (green 135°→202°, yellow 202°→270°, orange 270°→338°, red 338°→45°)
- Scale labels at pixel-exact positions: 0→(48,245), 250→(20,125), 500→(136,40), 750→(253,125), 1000→(220,245)
- Centre flex-column stack: value label (`IVF_FONT_HUGE`) + "ppb" unit + quality badge
- 500 ms ease-out animation on arc fills and value label; badge updates instantly
- `VOC_GAUGE_NO_READING` sentinel (0xFFFF): shows "--", clears arcs, sets grey badge

**Quality badge thresholds:**

| Badge | Range | Colour |
|-------|-------|--------|
| GOOD | 0–249 ppb | `IVF_COLOR_GOOD` |
| MODERATE | 250–499 ppb | `#FDD835` |
| POOR | 500–749 ppb | `IVF_COLOR_WARNING` |
| UNHEALTHY | 750–1000 ppb | `IVF_COLOR_DANGER` |
| --- | `VOC_GAUGE_NO_READING` | `#9E9E9E` |

**Also resolved in this phase — TD-13:** `circular_gauge.c` hardcoded `&lv_font_montserrat_48/16/12` replaced with `IVF_FONT_HUGE/NORMAL/SMALL`. Added `#include "ui/ui.h"`. Latent linker failure eliminated.

**Files created (2):** `voc_gauge.h`, `voc_gauge.c`  
**Files modified (3):** `screen_dashboard.c`, `circular_gauge.c`, `CMakeLists.txt`

**Public API:**
```c
#define VOC_GAUGE_NO_READING  ((uint16_t)0xFFFFu)

voc_gauge_t *voc_gauge_create(lv_obj_t *parent);
void         voc_gauge_set_value(voc_gauge_t *gauge, uint16_t ppb);
void         voc_gauge_set_animation(voc_gauge_t *gauge, bool enable);
```

---

#### Phase 4.2.5 — Dashboard Final Polish (COMPLETE) · Dashboard FROZEN

Final geometry and visual correctness pass against the approved Figma. No new features — only
fixes to spacing, alignment, contrast, and initial state identified in the Phase 4.2.4 review.

**Changes:**

1. **Header geometry — corrected right-to-left derivation** (`header.c`)  
   `HDR_SD_X` was calculated as `IVF_SCREEN_W - 8 - HDR_ICON_SIZE - 4 - HDR_ICON_SIZE = 220`,
   but `HDR_TIME_X = IVF_SCREEN_W - 8 - 50 = 214`. The SD icon was positioned inside the
   time label column. Fixed by reordering the `#define` chain so each constant derives from
   its right neighbour: `HDR_TIME_X=214`, `HDR_SD_X=190`, `HDR_WIFI_X=166`.

2. **Time/date labels right-aligned** (`header.c`)  
   Replaced `lv_obj_set_pos()` with `lv_obj_align(LV_ALIGN_TOP_RIGHT, -8, y)` at creation.
   `header_set_time()` and `header_set_date()` now call `lv_obj_align()` after every text update
   so the labels always anchor from the right margin regardless of string length.

3. **Title fixed width + `LV_LABEL_LONG_CLIP`** (`header.c`)  
   Title label width clamped to `HDR_WIFI_X - 4 - title_x` (128 px without menu, 114 px with
   menu button). `LV_LABEL_LONG_CLIP` prevents overflow into the WiFi icon area.
   `header_enable_menu()` updated to recompute the clipping width from the new start position.

4. **Humidity icon / label overlap fixed** (`screen_dashboard.c`)  
   Thermometer icon is 14 px wide; humidity icon is 20 px wide. `lbl_name` was at content
   x=18 (14 + 4 px gap) — correct for the thermometer but 2 px short for humidity.
   Changed to x=22 (20 + 2 px gap) for both cards.

5. **VOC gauge initialised to `NO_READING`** (`screen_dashboard.c`)  
   `voc_gauge_create()` initialises badge as green "GOOD". Between screen creation and the
   first `screen_dashboard_update()` (~1 s), the gauge showed "--" value but "GOOD" badge —
   contradictory. Added `voc_gauge_set_value(s_gauge, VOC_GAUGE_NO_READING)` immediately
   after `voc_gauge_create()`.

6. **MODERATE badge — dark text for contrast** (`voc_gauge.c`)  
   White (#FFFFFF) on yellow (#FDD835) gives ~1.2:1 contrast — effectively invisible.
   `update_badge()` refactored to use a `txt_color` variable that defaults to white but
   overrides to `IVF_COLOR_TEXT` (#212121) for the MODERATE range only.

7. **Legacy `nav_drawer.c` removed from build** (`CMakeLists.txt`)  
   The Phase 4B `nav_drawer.c` stub was superseded by `navigation_drawer.c` in Phase 4.2.2.
   The SRCS line is now commented out. `nav_drawer.h` is only included by `nav_drawer.c` itself;
   `ui.c` uses the `navigation_drawer_t` API throughout — removal is safe.

**Header right column:** 80 px, sized to fit the Figma placeholder "May 24, 2026" (~76 px)
with a ~12 px gap to the SD icon body. Phase 7 RTC formats fit within this budget. No
overlap limitation — placeholder matches the approved Figma and displays without overlap.

**Files modified (4):**

| File | Change |
|------|--------|
| `main/ui/components/header/header.c` | Geometry constants, right-aligned time/date, title clip, `header_enable_menu()` width update |
| `main/ui/components/voc_gauge/voc_gauge.c` | `update_badge()` dark text for MODERATE |
| `main/ui/screens/screen_dashboard.c` | `voc_gauge_set_value(NO_READING)` at init; `lbl_name` x 18→22 |
| `main/CMakeLists.txt` | `nav_drawer.c` SRCS line commented out |

#### Phase 4.2.6 — Hardware Validation Polish (COMPLETE) · Dashboard FROZEN

Visual fixes found when Phase 4.2.5 firmware was compared against the approved Figma on the physical CrowPanel device. No architecture changes, no new components, no CMakeLists.txt modification.

**Changes:**

1. **WiFi moved far right, SD icon removed** (`header.c`)  
   `HDR_WIFI_X = IVF_SCREEN_W - 8 - HDR_ICON_SIZE = 244`. SD card not built — `sd_body` stays NULL; existing guard in `header_set_sd_status()` makes all calls safe no-ops. `HDR_TITLE_MAX_X = 156`, `HDR_TIME_ROFS = 32`.

2. **Title font restored, title text updated** (`header.c`, `screen_dashboard.c`)  
   "DASHBOARD" fits in `IVF_FONT_NORMAL` (16 pt); `IVF_FONT_SMALL` was only needed for "AIR QUALITY MONITOR".

3. **Time/date alignment offset updated** (`header.c`)  
   `lv_obj_align(LV_ALIGN_TOP_RIGHT, -32, y)` — was `-8`; now clears the WiFi icon at x=244.

4. **Sparklines removed, card height reduced** (`screen_dashboard.c`)  
   `CARD_H` 110 → 90 px. `build_sensor_card()` returns `void` (no chart handle). `s_chart_*`, `s_ser_*`, `sty_chart` removed. `screen_dashboard_update()` no longer calls any `lv_chart_*` API.

5. **Navigation drawer full-screen height** (`navigation_drawer.c`)  
   `dh = IVF_SCREEN_H` — drawer is now 200×480 positioned at `(-200, 0)`, covering the header when open. Backdrop width check in `drawer_anim_done_cb` (272 ≠ 200) still correctly identifies backdrop vs drawer.

6. **New drawer top section** (`navigation_drawer.c`, `assets.h/.c`)  
   `DRAWER_HEADER_H` 54 → 148. Blue circle (56×56, `IVF_COLOR_PRIMARY`) at y=12, white shield icon (28×32 `assets_draw_shield`) centred inside, "36 × 36" badge (68×18 grey pill) at y=74, "Environmental Monitor" label at y=97, green "Normal" pill (64×20) at y=123, 1 px divider at y=144.

7. **Drawer version footer** (`navigation_drawer.c`)  
   `if (d->cfg.footer_version)` → centred `IVF_FONT_SMALL` muted label anchored to `LV_ALIGN_BOTTOM_MID, 0, -IVF_PAD`.

8. **Config struct extended** (`navigation_drawer.h`)  
   `const char *header_title`, `*header_status`, `*footer_version` added to `nav_drawer_cfg_t`. All three non-NULL activates the top section.

9. **Nav item labels** (`ui.c`)  
   `"Chart"` → `"TVOC Chart"`, `"Logs"` → `"Data Logs"`.

10. **`assets_draw_shield()` added** (`assets.h/.c`)  
    28×32 geometric primitive: rounded arch (28×22, r=8) + four tapering rows converging to a 2 px point. White fill, used inside the blue circle in the drawer.

11. **`assets_draw_humidity()` improved** (`assets.c`)  
    16×22 teardrop: 6×5 tip (r=3) → 12×5 widening mid (r=3) → 16×14 round bulb (r=8). Better matches Material Design water-drop reference.

**Files modified (7):**

| File | Change |
|------|--------|
| `main/ui/assets/assets.h` | `assets_draw_shield()` declaration |
| `main/ui/assets/assets.c` | `assets_draw_humidity()` updated; `assets_draw_shield()` added |
| `main/ui/components/header/header.c` | `HDR_WIFI_X=244`, SD removed, `HDR_TIME_ROFS=32`, `HDR_TITLE_MAX_X=156`, title font `IVF_FONT_NORMAL`, setters updated |
| `main/ui/components/navigation_drawer/navigation_drawer.h` | `header_title`, `header_status`, `footer_version` cfg fields |
| `main/ui/components/navigation_drawer/navigation_drawer.c` | Full-screen height, `DRAWER_HEADER_H=148`, new top section, version footer, `#include "assets.h"` |
| `main/ui/ui.c` | "TVOC Chart", "Data Logs" labels; cfg with `header_title/status/footer_version` |
| `main/ui/screens/screen_dashboard.c` | Title "DASHBOARD"; sparklines removed; `CARD_H=90`; `build_sensor_card()` simplified |

---

### Phase 5.1 — Chart UI Migration (COMPLETE)

**Goal:** Migrate `screen_chart.c` to the approved Figma design — UI/styling only. The chart's
data layer (period switching, `screen_chart_refresh()`, `history_manager` integration points)
is untouched; Phase 5.4 still owns live data binding (the `history_manager` backend itself
was completed in Phase 5.3).

**Changes made:**

1. **Header — shared `header_t` component** (`screen_chart.c`)
   Replaced the legacy `ui_build_header(s_scr, "TVOC HISTORY")` + inline `LV_SYMBOL_LIST` icon
   with the same `header_t` component Dashboard uses: `header_create()`, `header_set_title()`,
   `header_set_wifi_strength()`, `header_set_sd_status()`, `header_set_time()`,
   `header_set_date()`, `header_enable_menu()`. Title changed to `"CHART"`. The header is now
   pixel identical to Dashboard's (same menu button, WiFi position, time/date alignment).

2. **Period selector — rounded pill buttons** (`screen_chart.c`)
   Same `lv_btnmatrix` widget and `period_cb()` callback as before — only the map and styling
   changed. Labels are now `"90 Days" / "30 Days" / "07 Days"` (was `"7D"/"30D"/"90D"`) in that
   left-to-right order, matching Figma with 90 Days active by default. Because the visual order
   flipped, `period_t` and `PERIOD_POINTS[]` were reordered to `{PERIOD_90D, PERIOD_30D,
   PERIOD_7D}` — the button-index-to-period mapping is otherwise unchanged. Styling: transparent
   matrix background (no more bottom-border underline), each button independently rounded
   (`IVF_CARD_RADIUS`) with an `IVF_COLOR_BORDER` outline; active = `IVF_COLOR_PRIMARY` fill +
   white text, inactive = white fill + `IVF_COLOR_TEXT`.

3. **Calendar button** (`screen_chart.c`, `assets.c/.h`)
   The old in-header `LV_SYMBOL_LIST` icon was removed. A new plain (non-clickable — "no
   functionality yet") square button sits to the right of the period selector, styled like a
   period button (white, bordered, rounded) and hosting the new `assets_draw_calendar()` icon.

4. **TVOC title + legend row** (`screen_chart.c`)
   `"TVOC (ppb)"` font raised from `IVF_FONT_SMALL` to `IVF_FONT_NORMAL` (same font as the
   header title) per spec. Legend rebuilt as small colour-coded dots (`IVF_COLOR_GOOD` /
   `IVF_COLOR_WARNING`) followed by muted-grey text — `"Daily Average"` and `"Max"` — replacing
   the previous colour-on-text `LV_SYMBOL_MINUS` legend.

5. **Chart appearance** (`screen_chart.c`) — `lv_chart` widget unchanged, styling only:
   - White background, `IVF_CARD_RADIUS` rounded border, `IVF_COLOR_BORDER` grid lines
     (`lv_chart_set_div_line_count(chart, 4, 6)` — light grid, both axes).
   - Point markers: small circular dots (`LV_PART_INDICATOR`, `LV_RADIUS_CIRCLE`).
   - **Area fill under the average line only, no fill under max:** LVGL 8.4's `lv_chart` has
     no built-in per-series area-fill for `LV_CHART_TYPE_LINE` (`LV_PART_ITEMS` bg-opacity is a
     no-op for line series in this version — confirmed against the vendored `lv_chart.c`; it
     only affects bar charts). `chart_draw_part_cb()` hooks `LV_EVENT_DRAW_PART_BEGIN` for
     `LV_CHART_DRAW_PART_LINE_AND_POINT`, identifies the average series via
     `dsc->sub_part_ptr == s_ser_avg`, and paints a light green quad (`lv_draw_polygon()`, the
     same public primitive LVGL uses internally for line joins) between each line segment and
     the chart's bottom edge, using the real `dsc->p1`/`dsc->p2` draw coordinates. The max
     series is left as a clean line with no fill, matching Figma. This is a draw-event hook
     (the same extension point already used for X-axis tick-label formatting), not a widget
     replacement — `lv_chart` itself is untouched.
   - "Smooth lines": LVGL 8.4 draws straight anti-aliased segments between points (no built-in
     spline/bezier for `lv_chart`); true curve smoothing would require replacing the widget,
     which is out of scope for this phase.

6. **Statistics cards — migrated to shared `card_t`** (`screen_chart.c`)
   `make_stat_card()` rebuilt on `card_create()` (same component Dashboard's sensor tiles use)
   instead of raw `lv_obj_create()` — picks up the card's built-in light drop-shadow
   (`shadow=true`) for free. `LV_SYMBOL_UP/DOWN/LIST` icons replaced with drawn icons
   (`assets_draw_chart_average/_max/_min`, `assets_draw_date_range`). The `>150 ppb Days` card's
   hardcoded `lv_color_hex(0x7B1FA2)` was removed in favour of `IVF_COLOR_TEXT`.

7. **New drawn icons** (`assets.h/.c`) — geometric primitives, same style as the existing icon
   set (no bitmap assets, no CMakeLists.txt change):
   - `assets_draw_calendar()` (18×18) — body + hanging rings + 3 date dots.
   - `assets_draw_date_range()` (18×18) — same calendar body with a highlighted range bar.
   - `assets_draw_chart_average()` (16×16) — 3-point rising `lv_line` + end-point dot.
   - `assets_draw_chart_max()` (16×16) — upward chevron (`lv_line`).
   - `assets_draw_chart_min()` (16×16) — downward chevron (`lv_line`).

8. **Illustrative sample data** (`screen_chart.c`) — `SAMPLE_AVG[12]` / `SAMPLE_MAX[12]` are
   static hand-picked values baked into the UI layer so the chart and stat cards are not empty
   during visual review. `load_sample_series()` resamples them to the active period's point
   count and is called from `apply_period()`. This is UI-layer decoration only — no data module
   was added, `screen_chart_refresh()` remains the Phase 5.4 integration point, and the stat
   card labels (`AVERAGE 245 ppb`, `MAX 820 ppb`, `MIN 82 ppb`, `>150 ppb Days 26`) are static
   text matching the approved Figma. See TD-14.

**Files modified (2):**

| File | Change |
|------|--------|
| `main/ui/screens/screen_chart.c` | Full visual rewrite: `header_t`, `card_t`, drawn icons, pill period selector, calendar button, chart draw-part area-fill hook, illustrative sample data |
| `main/ui/assets/assets.h` / `assets.c` | 5 new icon functions: `assets_draw_calendar`, `assets_draw_date_range`, `assets_draw_chart_average`, `assets_draw_chart_max`, `assets_draw_chart_min` |

**No files created. No CMakeLists.txt change required** (new icons live in the existing
`assets.c`; no new source files).

**Explicitly out of scope for this phase** (per Phase 5.1 spec): `history_manager`, CSV export,
real sensor data, RTC, NVS, chart animations/zoom/scroll/gestures.

---

### Phase 5.2 — Chart Visual Polish (COMPLETE)

**Goal:** Bring the chart screen closer to the approved Figma. Pure visual refinement — no
architecture, data-model, `history_manager`, statistics-engine, or RTC changes. The shared
`header_t` component is **frozen**: this phase does not touch it.

**Changes made:**

1. **Legend — icons instead of dots** (`screen_chart.c`)
   The two colour-dot + muted-text legend items were replaced with the same drawn icons used
   on the stat cards: `assets_draw_chart_average()` (green) + `"Daily Average"`, and
   `assets_draw_chart_max()` (orange) + `"Maximum"` (was `"Max"`). No `LV_SYMBOL_*` used.
   Icons are vertically offset by `LEGEND_ICON_Y = INFO_Y + 2` to optically centre the 16 px
   icon against the `IVF_FONT_NORMAL` title row's taller line height.

2. **Chart container spacing** (`screen_chart.c`)
   `CHART_Y` 72→84 (more breathing room below the TVOC title), `CHART_H` 180→160 (slightly
   shorter, per spec), `CHART_X` 36→32 (tighter Y-axis label gutter — see item 5), bottom gap
   to the stat-card row raised to a named `CHART_BOTTOM_GAP = 16` (was an inline `+14`).
   `STATS_Y` derives from these so the 2×2 card grid still fits inside `IVF_CONTENT_H` (430)
   with ~22 px clearance at the bottom.

3. **Chart style** (`screen_chart.c`) — `lv_chart` widget and `apply_period()`/`period_cb()`
   logic untouched, styling only:
   - Background changed from `IVF_COLOR_BG` (white) to `IVF_COLOR_NAV` (`#F8F9FA`, the same
     light-grey already used for the header/nav — no new hardcoded colour introduced).
   - Grid lines softened: `lv_obj_set_style_line_opa(chart, LV_OPA_60, LV_PART_MAIN)` blends
     the existing `IVF_COLOR_BORDER` grid lines into the grey background.
   - Internal padding increased `pad_all` 4→6 px for more breathing room around the plot.
   - Border, radius, point markers (circular, `LV_PART_INDICATOR`), and the average-line area
     fill (`chart_draw_part_cb()`, added in Phase 5.1) are unchanged.

4. **Chart axes — typography and readability** (`screen_chart.c`)
   - Y-axis: font/colour already matched the Dashboard gauge's scale labels
     (`voc_gauge.c`: `IVF_FONT_SMALL` + `IVF_COLOR_TEXT_MUTED`) since Phase 5.1 — confirmed,
     no change needed there. Tick marks shortened (`major_len` 6→2) and minor sub-ticks
     removed (`minor_cnt` 4→1) for a cleaner look; `pad_left`/`pad_right` on `LV_PART_TICKS`
     set to 2 px for consistent label spacing.
   - X-axis: relabelled from relative day-offsets (`"-90D"`, `"Now"`) to illustrative month
     abbreviations (`"Feb"`…`"Jul"`), matching the Figma reference. `lv_chart_set_axis_tick()`
     major_cnt raised 5→`MONTH_COUNT` (6) so there are exactly 6 evenly spaced labelled ticks;
     `chart_draw_part_cb()` now indexes a static `MONTHS[6]` array directly by the tick's major
     index (`dsc->value`, confirmed against `lv_chart.c`'s `draw_x_ticks()` to be `i /
     minor_cnt`, i.e. a plain 0..5 counter for a `minor_cnt` of 1 — not a data-point index).
     This is a label-formatting change only; point count / period-switching logic is untouched.
     **The month labels are illustrative placeholders** (same spirit as the Phase 5.1 sample
     data) — see TD-15.

5. **Y-axis label gutter tightened** (`screen_chart.c`)
   LVGL 8.4's `lv_chart` always draws Y-axis tick labels in the extended area *outside* the
   widget's own bounding box (confirmed against `lv_chart.c`'s `draw_y_ticks()` — the label
   area is computed from `obj->coords.x1` going left, via `lv_obj_refresh_ext_draw_size()`,
   not from an inset padding *inside* the box). This means a Figma-style single card with the
   numbers embedded inside its left edge is not achievable without a custom draw hook —
   out of scope for a "styling only" phase. Instead, `CHART_X` (the external gutter reserved
   for the labels) was tightened from 36 to 32 px, the minimum comfortable width for a 4-digit
   value at `IVF_FONT_SMALL`, and `draw_size` was matched to it (32) so LVGL invalidates the
   full label area on redraw.

6. **Statistics cards — Dashboard-identical recipe** (`screen_chart.c`)
   `card_cfg_t.shadow` changed `true`→`false` to exactly match Dashboard's sensor tiles
   (`build_sensor_card()` in `screen_dashboard.c` uses `shadow = false`) — radius
   (`IVF_CARD_RADIUS`), border (`1px IVF_COLOR_BORDER`), and background were already identical
   since Phase 5.1. Value/unit spacing increased (`lv_obj_align_to` x-offset 3→6 px) so units
   read as `"245  ppb"` rather than `"245ppb"`.

7. **Card icons — unified size** (`screen_chart.c`, `assets.c/.h`)
   All four stat-card icons (`assets_draw_chart_average/_max/_min`, `assets_draw_date_range`)
   now share one `STAT_ICON_SIZE` (16 px) instead of two different sizes (16 / 18). To support
   this, `assets_draw_calendar()` and `assets_draw_date_range()` were redrawn at 16×16 (were
   18×18) — same geometry, proportionally tightened. This also shrinks the period-bar calendar
   button's icon slightly (cosmetic only, still centred in its 34×34 button). Icons remain
   right-aligned within each card's content area (`content_w - icon_w`, unchanged formula).

8. **Spacing audit vs. Dashboard** (`screen_chart.c`)
   Screen margin (8 px), inter-card gap (8 px), card radius/border, and typography
   (`IVF_FONT_SMALL`/`NORMAL`/`LARGE`, `IVF_COLOR_TEXT`/`TEXT_MUTED`) all reuse the same
   `ui.h` constants Dashboard uses — no new hardcoded colours, fonts, or radii were introduced
   in this phase.

**Files modified (2) — no files created, no CMakeLists.txt change:**

| File | Change |
|------|--------|
| `main/ui/screens/screen_chart.c` | Legend rebuilt with icons; chart spacing/background/grid/axis/label polish; card shadow + value/unit spacing to match Dashboard; unified icon size constant |
| `main/ui/assets/assets.h` / `assets.c` | `assets_draw_calendar()` / `assets_draw_date_range()` resized 18×18 → 16×16 |

**Known limitations carried forward:**
- X-axis month labels (`Feb`…`Jul`) are static illustrative text, not derived from real dates —
  see TD-15. Replaced with period-aware relative-date or calendar-date labels in Phase 5.4.
- The Y-axis numbers sit in an external gutter to the left of the chart's bordered box (an
  `lv_chart` 8.4 constraint, not a styling choice) rather than visually "inside" a single
  full-width card as in the Figma mock.
- Chart line rendering remains straight-segment (no bezier/spline) — `lv_chart` 8.4 has no
  built-in curve smoothing; unchanged from Phase 5.1.

**Next phase (completed below):** Phase 5.3 — History Manager Backend.

---

### Phase 5.3 — History Manager Backend (COMPLETE)

**Goal:** Backend architecture only. Implement the storage layer that will eventually drive
Chart, Logs, statistics, and CSV export — a "single source of truth" for historical sensor
data, independent of LVGL and every screen. No UI was touched (Dashboard and Chart UI are both
frozen); `screen_chart_refresh()` still shows Phase 5.1's static sample data — wiring it up is
Phase 5.4.

**Pre-implementation architecture review (as requested):** before writing any code, the
existing `sensor_manager`/`alarm_manager` pair and the previously-documented Phase 4C sketch
were reviewed for two risks:
1. *Duplicate responsibility* — a separate `record_manager.c` was already planned (Phase 5,
   Logs screen) for "1-minute snapshots, 24h ring buffer." Since `history_manager`'s brief
   explicitly covers Logs too, keeping both would create two ring buffers of the same sensor
   readings that could drift apart. **Resolution:** `record_manager.c` is retired; Logs will
   read `history_manager` instead (see the note under Phase 5 above, including the one open
   gap: `history_manager` today only retains hourly resolution, not per-minute).
2. *Tight coupling* — `alarm_manager.h` depends on `sensor_manager.h`'s `sensor_data_t` type
   directly. `history_manager.h` deliberately does **not** — `history_manager_add_sample()`
   takes plain `float` scalars, so the module has zero compile-time knowledge of
   `sensor_data_t`. `sensor_manager.c` is the only file that includes `history_manager.h`; the
   dependency arrow points one way.

#### 1. Architecture diagram

```
                    ┌─────────────────────────┐
                    │   sensor_backend_sim.c    │  (simulated readings)
                    └────────────┬────────────┘
                                 │ sensor_backend_sample()
                                 ▼
                    ┌─────────────────────────┐
                    │   sensor_manager.c        │  1 Hz task, Core 0
                    │   sensor_task()           │
                    └───┬─────────────────┬────┘
                        │                 │
          alarm_manager_check()   history_manager_add_sample()
          (every call, 1 Hz)      (decimated to every 60th call, ~1/min)
                        │                 │
                        ▼                 ▼
              ┌────────────────┐   ┌─────────────────────────────┐
              │ alarm_manager.c │   │ history_manager.c (Phase 5.3) │
              │ (unrelated —    │   │  - latest-reading cache        │
              │  not modified)  │   │  - in-progress hour accumulator│
              └────────────────┘   │  - 2160-slot hourly ring buffer│
                                    │    (PSRAM, 90-day horizon)      │
                                    └───────────────┬────────────────┘
                                                     │ history_manager_get_samples()
                                                     │ _get_latest() / _get_range() / _get_sample_count()
                                                     ▼
                                    ┌─────────────────────────────┐
                                    │  Chart / Logs / Statistics /   │
                                    │  CSV export (Phase 5.4+)       │
                                    │  — NOT wired up yet             │
                                    └─────────────────────────────┘
```

`history_manager.c` has no `#include` of `lvgl.h`, `sensor_manager.h`, `alarm_manager.h`, or any
`ui/` header — confirmed by inspection of its `#include` list (`freertos/*`, `esp_heap_caps.h`,
`esp_timer.h`, `esp_log.h`, `<string.h>` only).

#### 2. API summary

All functions are declared in `main/data/history_manager.h`. Names use the project's existing
`module_verb_noun()` C convention rather than the spec's `Module_Verb()` shorthand; the mapping
is 1:1:

| Spec name | Implemented as | Behaviour |
|---|---|---|
| `History_Init()` | `esp_err_t history_manager_init(void)` | Allocates the ring buffer (PSRAM, SRAM fallback) and creates the mutex. Call once at boot. |
| `History_AddSample()` | `void history_manager_add_sample(float voc_ppb, float temperature_c, float humidity_pct)` | Records one reading, internally timestamped. Call at ~1/minute. |
| `History_GetSamples()` | `uint16_t history_manager_get_samples(history_period_t period, history_record_t *out, uint16_t max_count)` | Copies up to `max_count` records for the period, oldest→newest. |
| `History_GetLatest()` | `bool history_manager_get_latest(history_record_t *out)` | Copies the single most recent raw reading. |
| `History_Clear()` | `void history_manager_clear(void)` | Empties the buffer (keeps the allocation). |
| `History_GetSampleCount()` | `uint16_t history_manager_get_sample_count(history_period_t period)` | How many records currently exist for the period (handles partial-history right after boot). |
| `History_GetRange()` | `uint16_t history_manager_get_range(history_period_t period, uint32_t from_ts, uint32_t to_ts, history_record_t *out, uint16_t max_count)` | Records within an explicit `[from_ts, to_ts]` window — for a future zoomed chart view or CSV export slice. |

`history_period_t` is `HISTORY_PERIOD_7D` / `_30D` / `_90D`. `history_record_t` is:
```c
typedef struct {
    uint32_t timestamp_s;      /* seconds since boot — see timestamp note below */
    float    voc_ppb;
    float    temperature_c;
    float    humidity_pct;
    uint8_t  alarm_state;      /* reserved for Phase 8; always 0 today */
} history_record_t;
```
New fields belong at the end of this struct (e.g. a second gas channel) so existing callers are
unaffected — matches the extensibility note in the Phase 5.3 brief.

**Timestamps** are `esp_timer_get_time() / 1e6` (seconds since boot) — the same boot-relative
convention already used by `alarm_manager.c` and `ui.c`'s `dash_timer_cb`. Real wall-clock
timestamps arrive with an RTC/SNTP source (Phase 7+), which is explicitly out of scope here.

#### 3. Memory estimate

`sizeof(history_record_t)` is 20 bytes on the Xtensa target (4-byte alignment: `timestamp_s` +
3 floats = 16 bytes, + `alarm_state` (1 byte) + 3 bytes padding).

| Buffer | Capacity | Size |
|---|---|---|
| Hourly ring buffer | 2160 records (90 days × 24 h) | 2160 × 20 B = **43,200 B ≈ 42.2 KB** |
| In-progress hour accumulator | 1 (running sums, not individual records) | ~24 B |
| Latest-reading cache | 1 record | ~20 B |
| FreeRTOS mutex | 1 | ~80 B (typical `SemaphoreHandle_t` overhead) |
| **Total** | | **≈ 42.3 KB** |

The 42.2 KB buffer is allocated with `heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM)` — from the
same 2 MB PSRAM pool as the 261 KB framebuffer and 261 KB LVGL draw buffer, with a fallback to
internal SRAM (`MALLOC_CAP_8BIT`) if the PSRAM allocation ever fails (logged as an error but
non-fatal — `history_manager_init()` still succeeds on SRAM). This comfortably fits alongside
existing allocations; see R-1 in the Risk Register for the project's general heap-pressure
posture.

**Why not store raw per-minute samples for 90 days?** 90 days × 1440 min/day = 129,600 samples
× 20 B ≈ 2.6 MB — larger than the entire PSRAM budget once the framebuffer and draw buffer are
accounted for. Storing only the *hourly average* of each 60 one-minute readings (see below)
keeps 90 days of history to 42 KB, a ~60× reduction, at the cost of losing per-minute
resolution beyond the single "latest reading" cache.

#### 4. Circular buffer strategy

One buffer, sliced by period — not three separate buffers:

```
s_hourly[2160]  (fixed-size PSRAM array)
                s_head = index of the NEXT write slot
                s_count = valid entries so far, capped at 2160

  overwrite-oldest write:
      s_hourly[s_head] = new_record
      s_head = (s_head + 1) % 2160
      if (s_count < 2160) s_count++

  read "last N" (oldest → newest), used by every query API:
      start = (s_head - n + 2160) % 2160        // oldest of the last n
      out[i] = s_hourly[(start + i) % 2160]     // for i in 0..n-1
```

A "period" is nothing more than a window size passed to that same read function:
`HISTORY_PERIOD_7D → n ≤ 168`, `_30D → n ≤ 720`, `_90D → n ≤ 2160` (the full buffer). There is
no separate 7-day or 30-day storage to keep synchronized — asking for a longer period just
returns more of the one buffer. This is why `history_manager_clear()` only needs to reset
`s_head`/`s_count` to 0 (an O(1) logical clear) rather than manage three independent resets.

**Feeding the ring buffer — two-level accumulation, not per-minute storage:**
1. `sensor_task()` (1 Hz) calls `history_manager_add_sample()` once every 60 iterations
   (~once/minute) — the decimation counter lives in `sensor_manager.c`, not inside
   `history_manager`, so the module's own API contract stays simple ("every call is one
   minute-tier sample").
2. Each call accumulates into four running values (`sum_voc`, `sum_temp`, `sum_hum`, `count`)
   for the *current, in-progress* hour — no per-minute record is stored.
3. On the 60th call (`HISTORY_SAMPLES_PER_HOUR`), the running sums are divided into one
   averaged `history_record_t` and pushed into the ring buffer via the overwrite-oldest write
   above; the accumulator then resets to zero for the next hour.

All ring-buffer and accumulator state is protected by a single `SemaphoreHandle_t` mutex
(`xSemaphoreTake`/`Give` around every read and write), the same pattern already used by
`sensor_manager.c` and `alarm_manager.c` — `history_manager_add_sample()` runs on the sensor
task (Core 0); the `get_*` query functions will run on the LVGL task (Core 1) once Phase 5.4
calls them from `screen_chart_refresh()`.

#### 5. Data flow: Sensor → History Manager → Chart/Logs

```
sensor_backend_sim.c          (1 Hz simulated VOC/temp/humidity)
        │  sensor_backend_sample()
        ▼
sensor_manager.c: sensor_task()   (1 Hz, Core 0, priority 3)
        │  every call:            alarm_manager_check(&fresh)          [unchanged]
        │  every 60th call:       history_manager_add_sample(voc, temp, hum)   [Phase 5.3, new]
        ▼
history_manager.c
        │  accumulate → every 60th minute-sample → average → push to hourly ring buffer
        ▼
   [ 2160-slot PSRAM ring buffer — 90-day horizon, mutex-protected ]
        │
        │  history_manager_get_samples() / _get_latest() / _get_range() / _get_sample_count()
        ▼
Chart (screen_chart_refresh(), Phase 5.4) ─┐
Logs  (screen_logs.c, Phase 5)             ├── NOT WIRED UP YET — history_manager is
Statistics engine (future phase)           │   populated and ready, but has zero readers
CSV export (future phase)                 ─┘   today. This phase only builds the pipe.
```

**Files created (2):**
- `main/data/history_manager.h` — public API, `history_record_t` / `history_period_t`
- `main/data/history_manager.c` — ring buffer, accumulator, mutex-protected implementation

**Files modified (3):**

| File | Change |
|------|--------|
| `main/sensors/sensor_manager.c` | `#include "data/history_manager.h"`; `sensor_task()` calls `history_manager_add_sample()` every 60th iteration (skipped on a sensor fault) |
| `main/app_main.c` | `#include "data/history_manager.h"`; `history_manager_init()` called after `alarm_manager_init()`, before `sensor_manager_init()` |
| `main/CMakeLists.txt` | `"data/history_manager.c"` added to SRCS (INCLUDE_DIRS already had `"data"`; no new REQUIRES — `esp_timer` was already listed, `heap_caps_malloc` needs no explicit component per the existing `lvgl_port.c` precedent) |

**Dashboard and Chart UI were not touched** — confirmed by diff: no changes to `screen_dashboard.c`, `screen_chart.c`, `header.c`, or any other `ui/` file.

**Known limitations:**
- No screen queries `history_manager` yet — Phase 5.4 wires `screen_chart_refresh()` to it.
- Hourly resolution only; no standing per-minute buffer beyond the single latest-reading cache
  (see the Phase 5 architecture-review note above — relevant if Logs needs finer granularity).
- RAM-only: a reboot clears all history (TD-8, unchanged — NVS/SD persistence is Phase 9).
- Timestamps are boot-relative (`esp_timer`), not wall-clock — unchanged until RTC/SNTP exists.
- Alarm state is reserved (`alarm_state` always 0) — no dependency on `alarm_manager.h` was
  introduced to keep the module decoupled; wiring it up is Phase 8.

**Next phase:** Phase 5.4 — Chart Data Binding (`screen_chart_refresh()` reads
`history_manager`, replacing the Phase 5.1 static sample data).

---

### Phase 5.4 — Chart Mode Integration & History Binding
**Status: ✅ COMPLETE — architecture review approved, then implemented**

**Functional design change (supersedes the Phase 5.4 plan written in the Phase 5.3 section
above):** the Chart screen's 90D/30D/7D period selector is removed entirely. The Chart now has
exactly two modes:
- **Mode 1 — Last 7 Days (default, shown whenever the screen is opened):** one point per
  calendar day, 7 points total; stat cards computed from those 7 days.
- **Mode 2 — Selected Day (via the calendar icon):** one point per hour for a single chosen
  day (≤24 points); stat cards computed from that day only.

This was reviewed against the existing `history_manager` (Phase 5.3) and `screen_chart.c`
(Phase 5.1/5.2) before any code was written, per request.

#### Is the Phase 5.3 storage architecture still appropriate?

**Yes, unchanged.** The single 90-day hourly ring buffer already contains everything both
modes need — Mode 1's 7 daily points are computable by grouping the most recent 168 hourly
records into 7 buckets of 24 **at query time**; Mode 2's day view is just a 24-hour-wide
`get_range()`. No new ring buffer, no new storage tier, no duplicate storage.

**Two gaps found in the Phase 5.3 API/schema layer, independent of the mode change:**
1. `history_period_t` (`HISTORY_PERIOD_7D/30D/90D`) modeled the now-deleted period-selector
   buttons directly — a UI concept leaking into a module whose brief is UI independence.
   **Removed.** Replaced with plain `(from_ts, to_ts)` timestamp ranges everywhere.
2. `history_record_t` only ever tracked a running *sum* per hour (for the average) — no
   min/max. A "Maximum" chart series or a "Minimum" stat card has no real data to plot once
   wired to live data. **Fixed by widening the struct**, not by adding storage (see below).
   Safe to change now: `screen_chart_refresh()` was never wired up, so there are zero real
   callers of `history_record_t` to migrate.

#### Recommended API (differs from the `Get Last 7 Days` / `Get Day History` / `Get Statistics`
names suggested for review — see rationale below)

```c
/* unchanged from Phase 5.3 */
esp_err_t history_manager_init(void);
void      history_manager_add_sample(float voc_ppb, float temperature_c, float humidity_pct);
bool      history_manager_get_latest(history_record_t *out);
void      history_manager_clear(void);

/* generic range queries — no "period" parameter anywhere */
uint16_t  history_manager_get_range(uint32_t from_ts, uint32_t to_ts,
                                     history_record_t *out, uint16_t max_count);
uint16_t  history_manager_get_daily_aggregates(uint32_t from_ts, uint32_t to_ts,
                                                history_record_t *out, uint16_t max_days);
uint16_t  history_manager_get_count_in_range(uint32_t from_ts, uint32_t to_ts);

/* one generic reducer — not two — because hourly and daily records share one struct shape */
void      history_manager_compute_stats(const history_record_t *records, uint16_t count,
                                         float threshold_ppb, history_stats_t *out);
```

Widened record struct (the **same** shape is reused for hourly records from `get_range()` and
day-bucket rollups from `get_daily_aggregates()` — only the bucket width differs):
```c
typedef struct {
    uint32_t timestamp_s;      /* bucket start: hour-start or day-start (boot-relative)  */
    float    avg_voc_ppb;
    float    min_voc_ppb;      /* NEW — tracked by the hourly accumulator                 */
    float    max_voc_ppb;      /* NEW — tracked by the hourly accumulator                 */
    float    temperature_c;    /* bucket average, unchanged                               */
    float    humidity_pct;     /* bucket average, unchanged                               */
    uint8_t  alarm_state;      /* reserved for Phase 8                                    */
} history_record_t;

typedef struct {
    float    avg_voc_ppb, min_voc_ppb, max_voc_ppb;
    uint16_t over_threshold_count;   /* buckets whose max_voc_ppb exceeds the threshold   */
    uint16_t sample_count;
} history_stats_t;
```
Memory impact: 20 B → 28 B per record × 2160 slots ≈ **59 KB** (was ~42 KB, still trivial
against the 2 MB PSRAM budget).

**Rationale for diverging from the three suggested names:**
- *"Get Last 7 Days"* → generalized to `get_daily_aggregates(from_ts, to_ts, ...)`, which
  doesn't encode "7" anywhere. Chart calls it with `from_ts = now - 7×86400`; a future 30-day
  trend view could call the identical function with a different range — genuinely
  Chart-independent, per the brief.
- *"Get Day History"* → not a separate function. It's just `get_range()` bounded to a 24-hour
  window. A dedicated narrower function would duplicate `get_range()`'s logic.
- *"Get Statistics"* → kept, but as **one** reducer instead of two (hourly/daily variants),
  since both inputs are now the same struct type. Avoids duplicate reduction logic.

#### Updated data flow

```
sensor_task() (1 Hz) → history_manager_add_sample()           [unchanged from Phase 5.3]
                              │
                    hourly accumulator — now tracks running min/max too
                              ▼
              90-day hourly ring buffer (single source of truth, unchanged)
                              │
        ┌─────────────────────┴─────────────────────────┐
        ▼                                                 ▼
history_manager_get_range()                  history_manager_get_daily_aggregates()
  (Mode 2: one day, ≤24 hourly records)         (Mode 1: 7 days → 7 daily buckets)
        │                                                 │
        └───────────────────────┬─────────────────────────┘
                                 ▼
                  history_manager_compute_stats()
                    (same reducer, either input type)
                                 ▼
                      screen_chart_refresh()
             (chart series + 4 stat cards, per active mode)
```

#### How the Chart screen switches modes

A `chart_mode_t { CHART_MODE_LAST_7_DAYS, CHART_MODE_SELECTED_DAY }` state in `screen_chart.c`.
**Confirmed:** the screen always resets to `CHART_MODE_LAST_7_DAYS` every time it is opened,
even if the user was previously viewing a selected day (no sticky state). The calendar icon
(`assets_draw_calendar`, currently decorative) becomes functional: tapping it opens a date
picker (`lv_calendar` is already enabled — `CONFIG_LV_USE_CALENDAR=y` in `sdkconfig`);
selecting a day sets `CHART_MODE_SELECTED_DAY` with that day's `[from_ts, to_ts)` bounds and
calls `screen_chart_refresh()`, which branches on mode to pick the query, relabel the X-axis
(day/date ticks vs. hour-of-day ticks), and rebind the 4th stat card. The calendar icon itself
remains available in both modes (picking a different day works directly from Selected-Day mode
too, without first returning to Mode 1).

**Confirmed: return-to-default control.** A persistent "← Last 7 Days" chip, visible only in
`CHART_MODE_SELECTED_DAY`, returns to Mode 1 on tap — agreed as the right mechanism; forcing
the user back through the calendar picker just to deselect would be a real anti-pattern.
**Placement refinement:** anchor it in the row the period-selector buttons used to occupy (now
vacant), not squeezed directly beside the title. The title text length changes with the mode
(see below), so anchoring a tap target next to it risks the target shifting position or
crowding a long date string. The vacated top row gives it a fixed position and a comfortably
large touch target in either mode.

**Confirmed: dynamic chart title, with one honesty caveat and one layout consequence.**
`"TVOC Trend (Last 7 Days)"` / `"TVOC Trend (24 Jun 2026)"` is agreed as the *end-state* format
— but:
- **No-RTC honesty issue:** showing a fabricated absolute date (`"24 Jun 2026"`) when the
  underlying timestamp is boot-relative, not wall-clock, would be misleading — the "selected
  day" is really a relative offset (same limitation already flagged for the calendar picker
  itself). Recommendation: render a relative label pre-RTC (e.g. `"TVOC Trend — 3 Days Ago"`);
  once RTC/SNTP lands (Phase 7), the exact same title code renders real calendar dates with no
  logic change — this is a one-function formatting concern, not an architecture blocker.
- **Layout consequence:** both title strings are meaningfully longer than the current static
  `"TVOC (ppb)"` label, which today shares a row with the legend starting at x=106 — that won't
  fit either title variant. Removing the period bar frees a full row; recommend giving the
  title its own row and moving the legend to a second row beneath it, rather than compressing
  both onto one line as today.

**New finding surfaced while reviewing the above (not explicitly asked about, but affected):**
the legend currently reads **"Daily Average."** That becomes incorrect in Mode 2, where the
green line is an *hourly* average, not a daily one. Recommend generalizing to **"Average"**
(correct in both modes); `"Maximum"` is already granularity-agnostic and needs no change.
Since title, legend, and the 4th stat card are now all mode-dependent text, recommend designing
one `apply_mode_labels()`-style update point for all three when implementing, rather than three
separate ad hoc edits scattered through `screen_chart_refresh()`.

**No-RTC limitation (must be flagged, not silently glossed over):** all timestamps are still
boot-relative (`esp_timer`), so there is no true "calendar day" — no real midnight to align
day buckets to, and no real dates for the picker to offer. `get_daily_aggregates()` will bucket
by fixed 24-hour windows counting backward from *now* ("today," "yesterday," …, up to however
much history exists — 90 days max), not by wall-clock midnight. The date picker can only
meaningfully offer relative-day selection until an RTC/SNTP source exists (Phase 7) — same
caveat already tracked for Dashboard's clock (TD-1) and logs timestamps (R-7).

#### Statistics card review

The Days/Hours split proposed for the 4th card is correct and kept as-is — a fixed "Days"
label would misdescribe an hourly dataset and vice versa. Recommendation: count buckets by
**`max_voc_ppb` exceeding the threshold**, not the bucket average — "was air quality ever
unsafe" is more actionable for a monitoring device than "was the average high," and this only
became possible once min/max tracking was added above.

**Flagged, not changed without confirmation:** the existing configurable thresholds
(`sensor_manager.c`: 300 ppb warn / 500 ppb alarm, editable in Settings per the Phase 6 plan)
don't match the ">150 ppb" figure used here. Since it's been specified twice independently, it
is being treated as an intentional third ("elevated") tier rather than silently changed to
300 ppb — recommend giving it a named constant instead of a magic number embedded in
`screen_chart.c`.

#### Resolved decisions (as of this revision)

1. ✅ Mode-switch-back: persistent `"← Last 7 Days"` chip in the vacated period-bar row,
   visible only in Selected-Day mode.
2. ⬜ Still open: 150 ppb threshold — no further input given; proceeding as a named constant
   (not user-configurable) unless told otherwise.
3. ✅ Always reset to Last-7-Days on every screen entry — confirmed, no sticky state.
4. ✅ Dynamic title — confirmed format, rendered as a relative label pre-RTC (see honesty
   caveat above), upgrading to real dates automatically once RTC/SNTP exists.
5. ✅ Legend generalized to `"Average"` / `"Maximum"` (was `"Daily Average"` / `"Maximum"`) —
   new finding, not in the original ask, needed for Mode 2 correctness.

**Files changed:** `main/data/history_manager.h/.c` (API replacement + schema widening),
`main/ui/screens/screen_chart.c` (removed period selector, added mode state + chip + date
picker + dynamic title/legend/4th-card labels + mode-aware refresh). No files created, no
CMakeLists.txt change (both files already existed and were already registered since Phase 5.3).

#### Implementation summary

**1. Chart mode enum — single source of truth (`screen_chart.c`):**
```c
typedef enum { CHART_MODE_LAST_7_DAYS = 0, CHART_MODE_SELECTED_DAY } chart_mode_t;
static chart_mode_t s_mode                = CHART_MODE_LAST_7_DAYS;
static uint16_t     s_selected_day_offset = 0;   /* 0 = today */
```
No boolean flags anywhere else in the file for mode state.

**2. Central mode controller — `apply_chart_mode(chart_mode_t mode)`:** the only function that
updates the Chart UI on a mode change. In order: queries `history_manager` (daily aggregates
for Mode 1, hourly range for Mode 2), pushes the returned records into the two chart series,
reconfigures the X-axis tick count, calls `history_manager_compute_stats()` on the exact same
buffer just plotted, updates all 4 stat-card values plus the 4th card's "Days"/"Hours" unit,
formats and sets the dynamic title, and shows/hides the "◀ Last 7 Days" chip. Both
`screen_chart_create()` (initial paint) and `screen_chart_refresh()` (called by `ui_goto_screen()`
on every navigation to the Chart screen) call this one function — no other code path mutates
chart/stat/title/chip state.

**3. Default mode:** `screen_chart_refresh()` unconditionally calls
`apply_chart_mode(CHART_MODE_LAST_7_DAYS)` — the screen always resets to Last 7 Days on entry,
regardless of what was last displayed, satisfying the "reset on every open" requirement without
any new call site (`ui_goto_screen()` already invoked `screen_chart_refresh()` since Phase 4A).

**4. Calendar mode:** the calendar icon (top-right, now tinted `IVF_COLOR_PRIMARY` instead of
muted grey to signal interactivity) opens a day-picker overlay — a semi-transparent backdrop
(`s_picker_backdrop`) with a centred panel containing an `lv_list` of 30 relative-day rows
("Today", "Yesterday", "2 Days Ago", …), built fresh each time the picker opens. Tapping a row
closes the picker, stores the offset in `s_selected_day_offset`, and calls
`apply_chart_mode(CHART_MODE_SELECTED_DAY)`. Tapping the backdrop outside the panel dismisses
the picker without changing mode.

**5. Return to default:** the "◀ Last 7 Days" chip (`LV_SYMBOL_LEFT " Last 7 Days"`, a filled
primary-colour pill) lives in the row the period-selector buttons used to occupy — not beside
the title, per the agreed placement refinement. `apply_chart_mode()` shows it only when
`mode == CHART_MODE_SELECTED_DAY` and hides it automatically the instant Last-7-Days mode is
re-applied (by the chip's own click handler, or by the reset-on-entry behaviour in #3) — no
separate "hide" call needed anywhere.

**6. Dynamic title — isolated formatter:** `format_relative_day_label(days_ago, buf, buf_len)`
is the only function that turns a day offset into text ("Today" / "Yesterday" / "N Days Ago");
it is called from both the title-building code in `apply_chart_mode()` and from the day-picker
row builder, so the two always agree. Titles: `"TVOC Trend - Last 7 Days"` (Mode 1) and
`"TVOC Trend - <relative label>"` (Mode 2) — a plain hyphen is used instead of an em dash to
guarantee the glyph exists in the compiled LVGL font (avoids a possible tofu/missing-glyph box).
Once RTC/SNTP lands (Phase 7), only `format_relative_day_label()` needs to change to emit real
calendar dates — no other function in the mode-switching path is aware of how the label is
worded.

**7. Legend:** `"Daily Average"` → `"Average"` (static text, set once at creation — since it's
now mode-invariant, `apply_chart_mode()` does not touch it, matching the Phase 5.4 review's
finding). `"Maximum"` unchanged. Legend and title both moved to their own rows (title's row,
then legend's row) since the dynamic title text is longer than the old static `"TVOC (ppb)"`
label and no longer fits sharing a row with the legend.

**8. Statistics cards:** all 4 values come from one `history_manager_compute_stats()` call per
mode switch — no separate average/max/min/count calculations scattered through the file. The
4th card's title is built once from the shared threshold constant
(`snprintf(buf, ">%.0f ppb", VOC_WARNING_THRESHOLD_PPB)`) rather than a hardcoded "150" string;
its unit label (`s_lbl_days_unit`) swaps between `"Days"` and `"Hours"` inside
`apply_chart_mode()`. When a query returns zero records (e.g. shortly after boot, or a day with
no history yet), all 4 values show `"--"` and the chart series are set to
`LV_CHART_POINT_NONE` rather than stale data from the previous mode.

**9. Threshold constant:** `VOC_WARNING_THRESHOLD_PPB` (150.0f) is defined once in
`history_manager.h` — the single source of truth referenced by `screen_chart.c` today, and
available to Logs/CSV export/Alarm Manager in later phases without redefining it.

**10. History Manager integration:** no new data manager, no duplicate storage — `screen_chart.c`
reads exclusively through `history_manager_get_range()` / `_get_daily_aggregates()` /
`_compute_stats()`. `history_manager.h/.c` themselves gained no new dependency on LVGL or any
screen header.

**Bug caught during implementation, not part of the original plan:** this project has
`CONFIG_LV_SPRINTF_USE_FLOAT` **disabled**, so LVGL's own `lv_snprintf()` cannot format `%f` —
using it for the stat-card values or the 4th card's threshold title would have silently
mis-rendered every number. Fixed by using the standard C `snprintf()` (`<stdio.h>`) for every
float-formatting call site instead, matching the convention `screen_dashboard.c` already uses
for temperature/humidity. Purely-textual/integer formatting (`%s`, `%u`) still uses
`lv_snprintf()` as before.

#### Updated architecture diagram

```
sensor_backend_sim.c → sensor_task() (1 Hz) → history_manager_add_sample()  [Phase 5.3, unchanged]
                                                        │
                                          90-day hourly ring buffer (unchanged)
                                                        │
                          ┌─────────────────────────────┴─────────────────────────────┐
                          ▼                                                             ▼
           history_manager_get_daily_aggregates()                     history_manager_get_range()
             (Mode 1: 7 days → 7 daily buckets)                    (Mode 2: 1 day → ≤24 hourly records)
                          └─────────────────────────────┬─────────────────────────────┘
                                                        ▼
                                     history_manager_compute_stats()
                                        (avg/min/max/over-threshold)
                                                        ▼
                                          apply_chart_mode(mode)
                              (chart series, X-axis, 4 stat cards, title, chip)
                                                        ▼
                                    screen_chart_create() / screen_chart_refresh()
```

#### Chart mode state diagram

```
                         ┌─────────────────────────────┐
                         │      CHART_MODE_LAST_7_DAYS   │◄────────────────────┐
        screen opened /  │   (default; chip hidden)      │                     │
        navigated to  ──►│                                │                     │
                         └───────────────┬────────────────┘                     │
                                         │ calendar icon tapped                 │
                                         ▼                                       │
                         ┌─────────────────────────────┐   "◀ Last 7 Days" tap  │
                         │   day picker overlay open     │                       │
                         └───────────────┬────────────────┘                     │
                                         │ day row tapped                       │
                                         ▼                                       │
                         ┌─────────────────────────────┐                        │
                         │   CHART_MODE_SELECTED_DAY     ├────────────────────────┘
                         │   (chip visible)               │
                         │   calendar icon still tappable │──► (re-opens picker, stays in this mode
                         └─────────────────────────────┘      until a day is picked or chip tapped)
```

#### Modified/new APIs (`history_manager.h`)

| API | Change |
|---|---|
| `history_period_t` / `HISTORY_PERIOD_7D/30D/90D` | **Removed** — modeled the deleted period selector |
| `history_manager_get_samples(period, ...)` | **Removed** — replaced by `get_range()` / `get_daily_aggregates()` |
| `history_manager_get_sample_count(period)` | **Removed** — replaced by `get_count_in_range(from_ts, to_ts)` |
| `history_manager_get_range(from_ts, to_ts, out, max_count)` | **Changed** — dropped the `period` parameter, now a pure timestamp-range query |
| `history_manager_get_daily_aggregates(from_ts, to_ts, out, max_days)` | **New** — on-demand day-bucket rollup from the same hourly buffer |
| `history_manager_get_count_in_range(from_ts, to_ts)` | **New** — replaces the period-based count |
| `history_manager_compute_stats(records, count, threshold_ppb, out)` | **New** — generic reducer, works on hourly or daily arrays |
| `history_record_t.voc_ppb` | **Renamed & widened** — now `avg_voc_ppb`, plus new `min_voc_ppb`/`max_voc_ppb` |
| `history_stats_t` | **New** struct — avg/min/max/over_threshold_count/sample_count |
| `VOC_WARNING_THRESHOLD_PPB` | **New** shared constant (150.0f) |
| `history_manager_init/add_sample/get_latest/clear()` | Unchanged (accumulator internals widened for min/max, API signatures the same) |

#### Memory impact

`sizeof(history_record_t)`: 20 B → 28 B (added 2 floats: `min_voc_ppb`, `max_voc_ppb`). Ring
buffer: 2160 × 28 B = **60,480 B ≈ 59.1 KB** (was ~42.2 KB at Phase 5.3) — still trivial against
the 2 MB PSRAM budget, allocated the same way (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`, SRAM
fallback). `history_manager_get_daily_aggregates()` additionally uses a fixed 32-entry local
bucket array (~900 B) on the caller's stack during the call only — well within the LVGL task's
8 KB stack (`LVGL_TASK_STACK_SIZE`).

#### Known limitations

- **No-RTC:** all timestamps remain boot-relative; "days"/"today" are relative offsets from
  now, not wall-clock-aligned calendar days. Flagged in the Phase 5.4 review, unchanged by this
  implementation — will resolve automatically once RTC/SNTP lands (Phase 7), touching only
  `format_relative_day_label()`.
- **Cold-start empty state:** a freshly booted (or just-cleared) device has zero hourly records
  for up to 60 minutes, and a full 7-day view needs 7 real days of uptime to populate — the
  simulated backend runs in real wall-clock time, so this cannot be verified quickly on-device
  without either waiting or temporarily accelerating `sensor_backend_sim.c`. Handled gracefully
  (`"--"` cards, empty chart) but full visual verification of a populated Last-7-Days view is a
  soak-test item, not something confirmed in this pass.
- **Day picker is a flat 30-row relative list**, not a Gregorian calendar grid — consistent
  with the no-RTC limitation (there's no real month/year to render). Revisit once RTC exists.
- **150 ppb threshold** remains a fixed, non-configurable constant, per the Phase 5.4 review
  (no further input was given on making it configurable).
- **`history_manager` still stores hourly resolution only** — TD-16 (a future Logs screen
  needing true per-minute rows) is unaffected by this phase.

**Next phase:** Phase 5.5 (not yet scoped — awaiting review of this implementation).

---

### Phase 5.4.1 — Real Bitmap Icons for the Chart Screen
**Status: ✅ COMPLETE**

**Context:** while reviewing the source tree during Phase 5.3, five unused files were found
already sitting in `main/ui/assets/` — `calendar_icon.c`, `chart_average_icon.c`,
`chart_maximum_icon.c`, `chart_minimum_icon.c`, `date_range_icon.c`. These are real
LVGL-converted bitmap icons (from the Iconify sets `lets-icons`, `carbon`, `tdesign`, `ic`)
matching the exact names the Phase 5.1 spec referenced, but they were never wired into the
build and contained a compile-breaking bug: the image-conversion tool left hyphens in C
identifiers (e.g. `LV_ATTRIBUTE_IMG_LETS-ICONS_DATE-RANGE-FILL`, `carbon_chart-average`),
which is invalid syntax — hyphens are the subtraction operator in C, not a valid identifier
character. They were flagged but deliberately left untouched during Phase 5.3/5.4 (both
explicitly out of scope for Chart UI changes); this phase replaces the Phase 5.1/5.2 hand-drawn
placeholder icons with these real bitmaps, now that touching Chart UI is in scope again.

**Fix applied:** every hyphen in each file's identifiers (macro guard, array name, struct name,
`.data` field reference) was replaced with an underscore — a purely mechanical, file-scope
find/replace, since hyphens appear nowhere else in these files (the pixel data is entirely
`0x..` hex literals; confirmed by grepping each file for `-` outside hex before editing).
No pixel data was touched. Resulting valid identifiers:

| File | Old (invalid) | Fixed | Size |
|---|---|---|---|
| `calendar_icon.c` | `lets-icons_date-range-fill` | `lets_icons_date_range_fill` | 24×24 |
| `date_range_icon.c` | `ic_outline-date-range` | `ic_outline_date_range` | 16×16 |
| `chart_average_icon.c` | `carbon_chart-average` | `carbon_chart_average` | 16×16 |
| `chart_maximum_icon.c` | `tdesign_chart-maximum` | `tdesign_chart_maximum` | 16×16 |
| `chart_minimum_icon.c` | `tdesign_chart-minimum` | `tdesign_chart_minimum` | 16×16 |

**Wiring:** all 5 files added to `main/CMakeLists.txt` SRCS. `assets.h` gained 5
`LV_IMG_DECLARE()` entries. The 5 corresponding `assets_draw_*()` functions in `assets.c` were
rewritten from hand-drawn `make_icon_cont()`/`make_filled_rect()`/`lv_line` primitives to the
same `lv_img_create()` + `lv_img_set_src()` + `lv_obj_set_style_img_recolor(..., LV_OPA_COVER)`
pattern already used by `assets_draw_thermometer()`/`_humidity()`/`_sd_card()` — confirmed those
existing bitmaps are also `LV_IMG_CF_TRUE_COLOR_ALPHA` format, the same as these 5, so the
identical recolor technique applies. **Function signatures are unchanged**
(`(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color)`), so `screen_chart.c`
needed only one change: the calendar bitmap is 24×24 (not the previous hand-drawn 16×16), so
its centering math in the `cal_btn` button was updated (`CAL_ICON_SIZE` constant added). The
`date_range`/`chart_average`/`chart_max`/`chart_min` bitmaps are all 16×16, matching
`STAT_ICON_SIZE` exactly — no other layout changes needed.

**Files modified (5, no CMake-registered files created — the 5 bitmap `.c` files already
existed, just unregistered):**
- `main/ui/assets/calendar_icon.c`, `date_range_icon.c`, `chart_average_icon.c`,
  `chart_maximum_icon.c`, `chart_minimum_icon.c` — hyphen→underscore identifier fix only
- `main/ui/assets/assets.h` — 5 new `LV_IMG_DECLARE()` entries
- `main/ui/assets/assets.c` — 5 `assets_draw_*()` bodies switched from drawn primitives to bitmaps
- `main/ui/screens/screen_chart.c` — `CAL_ICON_SIZE` (24) added; calendar button centering updated
- `main/CMakeLists.txt` — 5 new SRCS lines

**Known limitation:** visual appearance of the new bitmaps has not been confirmed on real
hardware or a simulator in this pass (no device/simulator available in this environment) —
recommend a flash-and-look pass before considering this fully verified.

**Next phase:** Phase 5.5 (not yet scoped).

---

### Phase 5.5 — Real Calendar Date Picker
**Status: ✅ COMPLETE**

**Context:** the Phase 5.4 Selected-Day picker offered relative rows ("Today", "Yesterday",
"2 Days Ago", …) in an `lv_list`. This does not scale to a 90-day retention window and gives
no way to jump to an arbitrary date, especially one that crosses a month or year boundary
(e.g. picking a day in late December 2025 when "today" is in 2026). This phase replaces it
with a real Gregorian calendar widget (LVGL's built-in `lv_calendar`), bounded to exactly the
90 days `history_manager` actually retains, plus real-calendar-date X-axis labelling.

**Design decision — no `<time.h>`:** this project has no RTC/SNTP yet (deferred to Phase 7),
so `history_manager` timestamps are boot-relative seconds, not wall-clock. Rather than depend
on `mktime()`/`gmtime()`/`timegm()` — whose exact newlib configuration on this ESP-IDF target
could not be verified in this development environment (no toolchain installed) — all calendar
arithmetic is implemented as dependency-free integer math: Howard Hinnant's public-domain
`days_from_civil()` / `civil_from_days()` proleptic-Gregorian algorithms, plus manual
`MONTH_ABBR`/`MONTH_FULL` name tables instead of `strftime()`. A single fixed reference date
(`CHART_REF_YEAR/MONTH/DAY`, currently 2026-07-06) is anchored to boot time once, at screen
creation, via `calendar_init_reference()` — every other date computation is exact given that
anchor. When RTC/SNTP lands in Phase 7, only `calendar_init_reference()`'s fixed constants need
to change to a real time read; no other function in this block is affected.

**What changed:**
- **Day picker.** The `lv_list` relative-day rows are gone. `s_picker_calendar` (an
  `lv_calendar_create()`) now renders a real month grid inside the existing picker panel, with
  custom `s_picker_prev_btn`/`s_picker_next_btn` month navigation (LVGL's own
  `lv_calendar_header_arrow` has no min/max bound support, so navigation is hand-rolled).
  `refresh_picker_calendar()` disables (`LV_BTNMATRIX_CTRL_DISABLED`) every day cell outside
  `[s_cal_min, s_cal_today]` — where `s_cal_min` is `today − 90 days`, recomputed each time the
  picker opens — and disables the prev/next buttons at the boundary month. Navigating
  correctly carries across a year boundary (e.g. Dec 2025 → Jan 2026) since month/year are
  tracked as a real `{year, month}` pair, not a relative offset.
- **Day selection.** Tapping an enabled day cell fires `on_picker_day_clicked()` (registered
  for `LV_EVENT_VALUE_CHANGED` on the calendar object itself — LVGL's calendar button matrix
  has `LV_OBJ_FLAG_EVENT_BUBBLE` set, so the click bubbles up from the internal widget), which
  reads the tapped date via `lv_calendar_get_pressed_date()`, stores it in the real
  `lv_calendar_date_t s_selected_date` (replacing the old `uint16_t s_selected_day_offset`),
  and calls `apply_chart_mode(CHART_MODE_SELECTED_DAY)`.
- **Chart title.** Selected-Day mode now renders the actual picked date — "TVOC Trend -
  06 Jul 2026" — instead of relative wording ("3 Days Ago").
- **X-axis, Selected-Day mode:** changed from 5 uneven ticks (`0h/6h/12h/18h/23h`) to 7 ticks
  at fixed 4-hour boundaries — `0, 4, 8, 12, 16, 20, 24` — per spec.
- **X-axis, Last-7-Days mode:** changed from static relative labels (`-6d … Today`) to real
  calendar dates computed per refresh. `apply_chart_mode()`'s `CHART_MODE_LAST_7_DAYS` branch
  now converts each returned `history_record_t.timestamp_s` to a calendar date via
  `boot_ts_to_calendar_date()` and formats it "D Mon" (e.g. "30 Jun", "6 Jul") into the new
  `s_day_axis_labels[7][8]` buffer, which correctly spans a month boundary since each label is
  computed independently from its own record's timestamp rather than from a shared offset.
- **Y-axis:** unchanged (0 / 250 / 500 / 750 / 1000 ppb gauge-value ticks) — not in scope.

**Files modified (1, no files created):**
- `main/ui/screens/screen_chart.c` — day-picker UI (`lv_list` → `lv_calendar`), calendar math
  block (`days_from_civil`/`civil_from_days`/`calendar_day_of_week`/etc.), month-nav and
  day-click handlers, `apply_chart_mode()` X-axis/title branches, layout constants
  (`PICKER_PANEL_W/H`, `PICKER_NAV_*`, `HISTORY_RETENTION_DAYS`, `HOUR_AXIS_TICKS` widened
  5→7). `history_manager.h/.c` and the Dashboard are untouched.

**Known limitations:**
- Not visually verified on hardware/simulator in this pass (no device/simulator available in
  this environment, consistent with every prior phase in this log) — recommend a flash-and-look
  pass, specifically exercising month navigation across the Dec 2025 → Jan 2026 boundary and
  the 90-day min-date clamp.
- "Today" is a fixed reference date anchored to boot time (see design decision above), not a
  real wall-clock read — inherits the existing no-RTC limitation (TD-17), does not introduce a
  new one.
- No compiler was available in this environment to build-verify this change; correctness was
  checked by hand-tracing the Hinnant algorithm against its public reference implementation and
  by cross-checking LVGL calendar API usage against the vendored `lv_calendar.c`/`.h` source.

**Next phase:** not yet scoped — awaiting review of this implementation.

---

### Phase 5.6 — Picker Simplification + Axis Label Fix
**Status: ✅ COMPLETE**

**Context:** two issues reported after reviewing Phase 5.5: (1) the full `lv_calendar` grid
picker was more than currently needed — request to simplify back to a plain 2-option dropdown
("Today" / "7 Days") for now, deferring arbitrary-date selection to a later phase; (2) neither
chart axis showed any tick labels at all.

**Picker simplification:** `s_picker_calendar` and its month-navigation machinery
(`s_picker_prev_btn`/`s_picker_next_btn`/`s_picker_month_lbl`, `refresh_picker_calendar()`,
`on_picker_month_nav()`, `on_picker_day_clicked()`, `open_day_picker()`) are removed. The
overlay panel now shows exactly two rows, "Today" and "7 Days"; tapping one hides the overlay
and calls `apply_chart_mode()` directly (`on_picker_today_click()` / `on_picker_7days_click()`).
`chart_mode_t`'s second value is renamed `CHART_MODE_SELECTED_DAY` → `CHART_MODE_TODAY` — it now
always means "today" (recomputed fresh from `boot_ts_to_calendar_date()` each time
`apply_chart_mode()` runs), not an arbitrary stored date, so `s_selected_date`/`s_cal_today`/
`s_cal_min` are gone along with the calendar-grid-only helpers `calendar_is_leap_year()`,
`calendar_days_in_month()`, `calendar_day_of_week()`, `calendar_date_serial()`. The title for
this mode is now the literal string "TVOC Trend - Today" rather than a formatted date. The
dependency-free calendar math added in Phase 5.5 (`days_from_civil`/`civil_from_days`/
`calendar_init_reference`/`boot_ts_to_calendar_date`/`calendar_date_to_boot_ts`) is **retained**
— it still backs the Last-7-Days real-calendar-date X-axis labels and Today's midnight-aligned
day-boundary lookup, so no regression there; only the UI on top of it got simpler.

**Axis label fix:** neither axis was rendering tick labels because `s_chart` had
`lv_obj_set_style_clip_corner(s_chart, true, 0)` set (added in Phase 5.2 for the rounded-border
look). LVGL draws chart axis tick labels *outside* the chart's own coordinate box, using its
ext-draw-size mechanism (Y labels to the left of x=0, X labels below the bottom edge — this
project's `CHART_X`/`CHART_BOTTOM_GAP` layout constants were sized specifically to reserve that
space). `clip_corner` builds a rounded-rectangle mask scoped to exactly the object's own
coordinate box for *all* of that object's drawing, main and extended alike — so anything drawn
outside the box, including both axes' tick labels, was being masked out completely, without
otherwise affecting appearance. Fix: removed the `clip_corner` call; the chart's rounded
appearance is unaffected since its background/border are already a plain filled rounded rect
drawn within its own box — clip_corner was only ever needed to stop *content* from poking past
the rounded corners, and this chart has no such content.

**Files modified (1, no files created):** `main/ui/screens/screen_chart.c` only.

**Known limitations:**
- Not visually verified on hardware/simulator — no device/simulator available in this
  environment, same caveat as every phase in this log. This fix in particular should be
  confirmed by an actual flash-and-look pass, since it reverses a specific, previously
  unverified assumption from Phase 5.2.
- Arbitrary-date selection (the Phase 5.5 calendar grid) is deliberately deferred, not lost —
  the underlying calendar math it would need already exists and is exercised daily by the
  Last-7-Days X-axis and Today's day-boundary calculation.

**Next phase:** not yet scoped — awaiting review of this implementation.

---

### Phase 5.7 — Chart Screen Freeze
**Status: ✅ COMPLETE**

The Chart screen (`screen_chart.c/.h`) is now **FROZEN**, on the same standing as the Dashboard:
no further changes without explicit approval. Development moves on to the **Logs screen**
(Phase 5 in the roadmap — table view of `history_manager` records: date/time, TVOC, temp,
humidity, record count, CSV export, "Load More" pagination), which is still `⬜ PLANNED`
(`screen_logs.c` is a stub).

**Files affected:** none — this is a status declaration, not a code change.

---

*(Phase 4C and Phase 4D, as originally sketched here, are both superseded — see Phase 5.3
above for the as-built History Manager and its detailed API/architecture/memory/data-flow
write-up, and Phase 5.4 above for the current, revised Chart data-binding design.)*

---

### Phase 5 — Logs Screen (PLANNED) — `record_manager.c` retired, merged into `history_manager`

**Architecture review finding (Phase 5.3):** this page originally planned a standalone
`record_manager.c/.h` (1-minute snapshots, 1440-record/24h ring buffer) purely for the Logs
table. Phase 5.3's `history_manager` is explicitly chartered as "the single source of truth for
historical data" for **both** Chart and Logs (per its design brief) — a second, separately
maintained ring buffer of the same sensor readings would duplicate that responsibility and let
the two screens drift out of sync. `record_manager.c` is retired; Logs should query
`history_manager` instead.

**Known gap to resolve when Logs is implemented:** `history_manager` currently stores only
*hourly*-resolution records (see Phase 5.3) — it has no standing per-minute buffer beyond the
single "latest reading" cache (`history_manager_get_latest()`). If the Logs table truly needs
1-minute-resolution rows (not hourly), Phase 5 will need to add a bounded minute-resolution
ring buffer to `history_manager` (e.g. a rolling 24h/1440-slot tier alongside the existing
90-day hourly tier) rather than reintroducing a separate module. If hourly resolution is
acceptable for the Logs table, no `history_manager` change is needed at all.

**Files to modify:**
- `main/ui/screens/screen_logs.c` — `lv_table`, columns: Time 60 / TVOC 60 / Temp 52 / Hum 44 / Status 56 = 272 px; scrollable, 430 px content height; status cell coloured by level; reads via `history_manager_get_samples()` / `_get_range()`
- `main/data/history_manager.c/.h` — only if per-minute resolution is required (see gap above)

**sdkconfig:** verify `CONFIG_LV_USE_TABLE=y`.

---

### Phase 6 — Settings Screen (PLANNED)

**Files to create:**
- `main/data/config_manager.c/.h` — NVS read/write abstraction; `config_get_thresholds()` / `config_set_thresholds()`

**Files to modify:**
- `main/ui/screens/screen_settings.c` — brightness `lv_slider`, VOC/temp/hum threshold `lv_spinbox` rows, Save button → NVS + hot-reload; content height 436
- `main/display/display_driver.c/.h` — add `display_set_backlight_pct(uint8_t pct)` via LEDC PWM (GPIO 2)
- `main/app_main.c` — load thresholds from config_manager at boot; pass to sensor_manager and alarm_manager
- `main/CMakeLists.txt` — add `"data/config_manager.c"`

**Threshold hot-reload** (must implement in Phase 6):
```c
// In save_cb() after nvs_commit():
sensor_manager_reload_thresholds();   // re-reads NVS into sensor_manager statics
alarm_manager_reload_thresholds();    // re-reads NVS into alarm_manager statics
```

**sdkconfig:** verify `CONFIG_LV_USE_SLIDER=y`; add `CONFIG_LEDC_ENABLED=y` for PWM.

---

### Phase 7 — Sensor Framework (PLANNED)

**Goal:** Fill in `sensor_backend_hw.c`. One `CMakeLists.txt` line activates it — zero changes to `sensor_manager.c`, dashboard, or any other module.

**Files to modify:**
- `main/sensors/sensor_backend_hw.c` — implement `sensor_backend_init()` (I2C bus, ENS160 mode, AHT21 probe) and `sensor_backend_sample()` (AHT21 read → ENS160 compensation → ENS160 TVOC)
- `main/app_main.c` — add `i2c_master_init()` call before `sensor_manager_init()`
- `main/CMakeLists.txt` — comment out `sensor_backend_sim.c`, uncomment `sensor_backend_hw.c`; add ENS160/AHT21 driver sources

**Files to create:**
- `main/sensors/ens160_driver.c/.h` — I2C mode set, TVOC read, compensation write, validity flag check
- `main/sensors/aht21_driver.c/.h` — trigger/read/CRC

**I2C details:**
- SDA = GPIO 17, SCL = GPIO 18 (confirm in `board.h`)
- ENS160 address: 0x53 (ADDR low) or 0x52 (ADDR high) — confirm wiring before Phase 7
- AHT21 address: 0x38
- ENS160 warm-up ~60 s before TVOC valid — add `SENSOR_LEVEL_WARMING` state; show `"Warming..."` badge on dashboard during warm-up

---

### Phase 8 — Alarm Framework (PLANNED)

**Goal:** Add NVS persistence to `alarm_manager` and expose alarm acknowledgement via the UI.

**Files to modify:**
- `main/data/alarm_manager.c/.h` — serialize ring buffer to NVS on each push; restore on boot; add `alarm_ack(id)` API; add `alarm_manager_get_unacked_count()`
- `main/ui/` — alarm unread count badge in header bar; optional alarm detail bottom-sheet on badge tap

---

### Phase 9 — Storage Framework (PLANNED)

**Goal:** Persist records to NVS (short-term) and SD card CSV export (long-term).

**Files to modify:**
- `main/data/history_manager.c` — add NVS/SD persistence of ring buffer state (currently
  RAM-only since Phase 5.3 — `record_manager.c` was retired in favor of this module, see Phase 5)
- `main/ui/screens/screen_logs.c` — add "Export CSV" button (visible only when SD detected)

**Files to create:**
- `main/data/sd_export.c/.h` — SD card mount/unmount, CSV write; SNTP sync for timestamps if WiFi available

---

### Phase 10 — Production Hardening (PLANNED)

OTA firmware update (ESP-IDF OTA over WiFi/BLE), task watchdog registration and feed for LVGL task and sensor task, display auto-sleep (LEDC dim after 5 min idle, wake on touch), full-screen error view for critical failures (permanent sensor loss, NVS corrupt, OOM), `heap_caps_print_heap_info()` on boot, production log level (`CONFIG_LOG_DEFAULT_LEVEL_WARN`), startup self-test (sensor comms check, touch corner verification on first boot).

---

### Standing Technical Requirements

#### Touch calibration (pre-production)

Factory values in `touch_driver.h`: `TOUCH_RAW_X_MIN=200`, `TOUCH_RAW_X_MAX=4000`, `TOUCH_RAW_Y_MIN=200`, `TOUCH_RAW_Y_MAX=3600`. Run a calibration routine on the physical device and update these constants if taps register in wrong positions.

#### PSRAM — N4R2 module required

PSRAM is enabled in `sdkconfig.defaults` for the N4R2 (2 MB OPI) module. If building for a non-PSRAM variant (N4): remove `CONFIG_ESP32S3_SPIRAM_SUPPORT` entries, set `fb_in_psram=0`, and reduce the LVGL draw buffer to line mode in `lvgl_port.c` (internal SRAM cannot hold 261 KB).

#### RTC / time source

`dashboard_set_time()` and `dashboard_set_date()` API exists in `screen_dashboard.h`. Currently not called — header shows mock values `"08:25 AM"` / `"May 24, 2025"`. Wire one of: PCF8563 I2C RTC (recommended for medical device), `esp_sntp` (needs WiFi), or `esp_timer_get_time()` elapsed time (currently active fallback in `dash_timer_cb`). Time is already updated once per second via `dashboard_set_time()` in `dash_timer_cb` (`ui.c`). Replace the `esp_timer_get_time()` calculation with a real RTC read in `dash_timer_cb` when the RTC hardware is available in Phase 7.

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

### Sensor data is simulated (via backend pattern)
All readings on the dashboard come from `sensor_backend_sim.c` (sine-wave) via the
`sensor_backend` interface. The dashboard, `sensor_manager`, and `ui` layers are agnostic
to whether a simulation or real driver is active — only `CMakeLists.txt` selects the backend.
Values are not real until Phase 7 fills in `sensor_backend_hw.c`.

### Thread safety
All LVGL API calls from outside the LVGL task must be wrapped with `lvgl_port_lock()` / `lvgl_port_unlock()`. Direct `lv_*` calls from other tasks without the lock will cause crashes. This applies to all navigation and drawer API calls as well.

Dashboard sensor updates run via `dash_timer_cb` — an LVGL timer that fires inside `lv_timer_handler()` on the LVGL task (Core 1). No mutex is needed because the callback already holds the LVGL lock. The removed `ui_refresh_task` + `ui_dashboard_refresh()` pattern caused priority-inversion starvation at the same priority level (see TD-2 resolved, Section 11).

### Header layout (Phase 4.2.6)
WiFi icon is at `HDR_WIFI_X=244` (far right, 8 px from edge). SD card icon is not present.
Time/date labels use `LV_ALIGN_TOP_RIGHT, -32` — the `-32` offset clears the WiFi icon
(`8 + 20 + 4 = 32`). Labels grow leftward from offset 32, leaving the WiFi icon clear.
Title is clipped at `HDR_TITLE_MAX_X=156` to prevent overflow into the time/date column.
The `HDR_TIME_COL_W=80` px reserved for time/date is sized for the Figma placeholder
"May 24, 2026" (~76 px at Montserrat 12 pt); all expected Phase 7 RTC formats fit.

---

## 11. Technical Debt

| # | Item | File(s) | When to fix |
|---|------|---------|-------------|
| TD-1 | Time/date labels are mock values (`"08:25 AM"` / `"May 24, 2025"`) hardcoded. `dashboard_set_time()` / `dashboard_set_date()` API exists and is ready — nothing calls it. | `app_main.c`, `screen_dashboard.h` | Phase 7 (RTC source available) |
| ~~TD-2~~ | ~~`ui_refresh_task` / `ui_dashboard_refresh()` cross-task LVGL access.~~ | ~~`app_main.c`, `ui.c`~~ | ✅ Resolved — `ui_refresh_task` removed from `app_main.c`; `ui_dashboard_refresh()` removed from `ui.c`/`ui.h`; dashboard refresh moved to `lv_timer_create(dash_timer_cb, 1000)` inside `lv_timer_handler()`. Root cause: `ui_refresh_task` (priority 2) and `lvgl_task` (priority 2, Core 1) competed for the LVGL mutex at equal priority; LVGL's 10 ms lock timeout caused `lv_timer_handler()` to be skipped, freezing animations and touch response. LVGL timer runs on the same task — zero contention. |
| TD-3 | Touch calibration constants are factory estimates. Taps may register offset on some units. | `touch/touch_driver.h` | Pre-production |
| ~~TD-4~~ | ~~Dashboard sparkline ranges are static~~ | ~~`screen_dashboard.c`~~ | ✅ Resolved Phase 4.2.6 — sparklines removed from sensor cards; `CARD_H` reduced to 90 px |
| TD-5 | `lv_obj_set_style_bg_color()` on `s_badge` called at 1 Hz. LVGL 8 should recycle the local style slot but this has not been stress-tested over hours. Verify no heap growth with `lv_obj_get_local_style_cnt()`. | `screen_dashboard.c` | Validate during Phase 4B soak test |
| TD-6 | `sensor_backend_hw.c` requires `i2c_master_init()` in `app_main.c` before `sensor_manager_init()`. Hook is not wired — current app_main does not call it. | `app_main.c`, `sensor_backend_hw.c` | Phase 7 |
| TD-7 | No task watchdog on `sensor_task` or the LVGL FreeRTOS task. A deadlock causes a silent freeze with no auto-reset. | `sensor_manager.c`, `lvgl_port.c` | Phase 10 |
| TD-8 | `history_manager`'s hourly ring buffer (Phase 5.3, now read by Chart since Phase 5.4, ~59 KB) is RAM-only — power cycle clears all trend history. NVS/SD persistence is Phase 9. | `data/history_manager.c` | Phase 9 |
| TD-9 | `alarm_manager` ring buffer is RAM-only — alarm history lost on reboot. | `data/alarm_manager.c` | Phase 8 |
| TD-10 | `screen_chart_refresh()` is a silent no-op; the chart/stat cards show static illustrative data (see TD-14) instead of live `history_manager` data. Data binding is Phase 5.4. | `screen_chart.c` | Phase 5.4 |
| TD-11 | `sensor_manager_reload_thresholds()` and `alarm_manager_reload_thresholds()` do not exist. Settings save requires a reboot to take effect. | `sensor_manager.c`, `alarm_manager.c` | Phase 6 |
| TD-12 | Bottom tab bar is still present in all four content screens. `ui_build_tab_bar()` calls and `IVF_TAB_H` references must be removed when nav drawer is implemented. | `screen_dashboard.c`, `screen_chart.c`, `screen_logs.c`, `screen_settings.c`, `ui.c`, `ui.h` | Phase 4B |
| ~~TD-13~~ | ~~`circular_gauge.c` font references~~ | ~~`circular_gauge.c`~~ | ✅ Resolved Phase 4.2.4 — replaced `&lv_font_montserrat_48/16/12` with `IVF_FONT_HUGE/NORMAL/SMALL` |
| ~~TD-14~~ | ~~`SAMPLE_AVG[]`/`SAMPLE_MAX[]` and hardcoded stat-card values~~ | ~~`screen_chart.c`~~ | ✅ Resolved Phase 5.4 — `apply_chart_mode()` now queries live `history_manager` data via `get_range()`/`get_daily_aggregates()`/`compute_stats()`; no more sample arrays |
| TD-16 | `history_manager` retains only hourly-resolution records — no standing per-minute buffer beyond the single `history_manager_get_latest()` cache. **Settled in Phase 5.8**: the Logs screen deliberately shows one row per hourly record rather than per-minute, because a full 90-day per-minute buffer would need ~2.5 MB (129,600 records) — more than this board's total 2 MB PSRAM. Not revisiting unless a future requirement genuinely needs per-minute granularity and a memory budget is found for it. | `data/history_manager.c`, `screen_logs.c` | Not scheduled |
| ~~TD-15~~ | ~~Static illustrative `MONTHS[6]` X-axis labels~~ | ~~`screen_chart.c`~~ | ✅ Resolved Phase 5.4 (mode-aware relative labels), then Phase 5.5 (`s_day_axis_labels[]`/`HOUR_LABELS[]` now show real calendar dates, not relative text) |
| TD-17 | Chart day boundaries are computed from a fixed reference date anchored to boot-relative `esp_timer` time (see `calendar_init_reference()`), not a real wall-clock/RTC read — "today" is correct only as long as the device has been running continuously since the anchor was set. The Last-7-Days X-axis dates and Today's day-boundary lookup are otherwise fully general; only `calendar_init_reference()`'s fixed `CHART_REF_YEAR/MONTH/DAY` constants need to change once RTC/SNTP exists (Phase 7) — no other function in `screen_chart.c` needs to change. | `screen_chart.c` | Phase 7 |
| TD-18 | Full visual verification of a populated "Last 7 Days" view requires 7 real days of device uptime (the simulated backend runs in real wall-clock time) — not exercised end-to-end. Cold-start / partial-history states (`"--"` cards, empty chart) and the Phase 5.6 axis-label fix were verified by code inspection only, no device/simulator available in this environment. | `screen_chart.c`, `data/history_manager.c` | Soak test + flash-and-look before final sign-off |
| TD-19 | Chart's Today/7-Days picker is a fixed 2-option dropdown (Phase 5.6) — arbitrary-date selection was deliberately deferred (see Phase 5.5, superseded). The dependency-free calendar math (`days_from_civil`/`civil_from_days`/etc.) needed to bring it back already exists and is exercised daily by the Last-7-Days labels and Today's boundary calc, so re-adding a calendar-grid picker later is additive, not a rewrite. | `screen_chart.c` | Not scheduled |
| TD-20 | `screen_chart.c` and `data/calendar_util.c` each contain their own copy of the same Hinnant calendar-conversion algorithm. Phase 5.8 introduced `calendar_util.c` as the shared version for new consumers (Logs) specifically *because* Chart is FROZEN (Phase 5.7) and could not be refactored to use it without touching a frozen file. De-duplicate by switching `screen_chart.c` to `calendar_util.c` once Chart is unfrozen — behavior-preserving, not a functional change. | `screen_chart.c`, `data/calendar_util.c` | When Chart is unfrozen |
| TD-21 | Logs screen's "Export CSV" button is a visual placeholder — its click handler only logs a debug message. Real CSV export needs SD/flash write infrastructure that does not exist yet (`data/sd_export.c/.h`, Phase 9, `⬜ PLANNED`). | `screen_logs.c` | Phase 9 |
| TD-22 | Logs screen's "Load More" pagination is capped at `LOGS_MAX_LOADED_ROWS` (100 rows / 10 pages) as an embedded-safety guard against unbounded LVGL object growth — clicking past the cap simply hides "Load More" even if older history remains within the 90-day window. Not expected to matter in practice (100 rows is far more than anyone pages through by hand), but worth knowing if a future requirement needs the full 2160-row history reachable via this control. | `screen_logs.c` | Not scheduled |
| TD-23 | Logs screen has no live/periodic refresh — `screen_logs_refresh()` only fires on navigation into the screen, so a newly-completed hourly record does not appear until the user leaves and comes back. Whether Logs needs a live tick (and on what cadence, and whether it should preserve scroll position) is an open product question, not yet answered — see Phase 5.9's "What's pending" note. | `screen_logs.c` | Awaiting requirements |
| TD-24 | No log row is ever produced by an *event* (e.g. an alarm threshold crossing) — every row comes from the fixed hourly cadence in `history_manager`. `history_record_t.alarm_state` exists as a field but is hardcoded to 0 everywhere; whether alarm events should someday produce their own out-of-band rows is undecided. | `data/history_manager.c`, `data/alarm_manager.c` | Phase 8 (alarm_manager integration) |
| TD-25 | Settings' "Threshold (ppb)" and "TVOC High Threshold" (display-range values) are persisted via `config_manager` but consumed by nothing — their only plausible use (Dashboard gauge scale / Chart Y-axis range) sits in two FROZEN screens. Wiring them in requires an explicit decision to unfreeze Dashboard and/or Chart. | `screen_settings.c`, `screen_dashboard.c`, `screen_chart.c` | Awaiting unfreeze decision |
| TD-26 | Temperature/humidity alarm thresholds (`TEMP_HIGH_C`/`TEMP_LOW_C`/`HUM_HIGH_PCT`/`HUM_LOW_PCT` in `alarm_manager.c`, `s_temp_warn_c` etc. in `sensor_manager.c`) have no Settings UI and no `config_manager` entries — only VOC warn/alarm got a live-reload path in Phase 6. They still require a firmware reboot to change (the original, still-partially-open TD-11). | `data/alarm_manager.c`, `sensors/sensor_manager.c` | Not scheduled |
| TD-27 | `display_power.c`'s dim level (15%, `CONFIG_DIM_BRIGHTNESS_PCT`) and the alarm-active gate have not been visually/behaviorally verified on hardware — no device available in this environment. In particular, the touch-swallow wake behavior (`lvgl_port.c` reporting `LV_INDEV_STATE_RELEASED` for the waking touch) and the LEDC PWM brightness curve (is 15% duty actually visibly-dim-but-recoverable on this specific panel, or does it need further tuning?) should be the first things checked on a flash-and-look pass. | `display/display_power.c`, `display/display_driver.c`, `lvgl_port/lvgl_port.c` | Flash-and-look before sign-off |
| TD-28 | Light/Dark theme (Phase 6.1) reaches Dashboard/Chart/Logs' rendered colors purely by redefining the `IVF_COLOR_*` macros in `ui.h` from literals into function calls — no byte of any of those three frozen screens' own source was touched, but their *rendered appearance* now depends on a global setting none of them were reviewed against. One real source-level fix was already needed and made (header.c's SD-icon "absent" state was recoloring at 30% opacity, which blends with — doesn't replace — the bitmap's native dark pixels, so it read as black in Dark mode; fixed to full opacity). Worth a specific look on a flash-and-look pass for anything similar elsewhere: low-opacity icon recolors, hardcoded colors that bypass `IVF_COLOR_*`, or the chart's fixed-color series lines against a dark background. | `ui/ui.h`, `ui/ui.c`, `screen_dashboard.c`, `screen_chart.c`, `screen_logs.c`, `ui/components/header/header.c` | Flash-and-look before sign-off |
| ~~TD-29~~ | ~~Widening the burger button (`HDR_BTN_W` 20→44, Phase 6.3) shrunk the header title's available width from 101px to 77px, and "DASHBOARD" may not fit.~~ | ~~`ui/components/header/header.c`~~ | ✅ Resolved Phase 6.4 — confirmed clipping on real hardware as predicted; `HDR_BTN_W` set to 30 (title width now 91px), fits "DASHBOARD" without `LV_LABEL_LONG_CLIP` truncating it. Still not re-verified on hardware after the 30px change — a final flash-and-look is warranted, but this is no longer an open design question. |
| TD-30 | Phase 6.4's touch-passthrough fix (`lvgl_touch_read_cb` in `lvgl_port.c` gating raw touch points outside the drawer's width while it's open) treats a symptom without a confirmed root cause. The nav drawer's full-screen clickable backdrop on `lv_layer_top()` is the standard, documented LVGL pattern for blocking clicks to the screen beneath a modal, and LVGL's indev hit-testing is supposed to check the top layer before the active screen for exactly this reason — so the passthrough should not have been possible by design. No vendored LVGL 8.4.0 source was available in this environment to trace the actual dispatch order and explain why it was. The fix is defensive (correct regardless of cause) but only covers touches in the dimmed area outside the drawer's own 200px column; if the same symptom also occurs for taps landing directly on the drawer's own column, this fix does not cover that case and the underlying dispatch-order question would need real investigation (LVGL source access, or targeted on-device logging of which object each press resolves to). | `lvgl_port/lvgl_port.c`, `ui/components/navigation_drawer/navigation_drawer.c` | Flash-and-look before sign-off; deeper investigation only if taps on the drawer's own column show the same symptom |

---

## 12. Risk Register

| # | Risk | Severity | Likelihood | Mitigation |
|---|------|----------|-----------|-----------|
| R-1 | **LVGL heap exhaustion** — Dashboard and Chart both live in LVGL memory simultaneously. A fully implemented chart screen (lv_chart + 90 points + buttons + axis labels) significantly increases heap pressure. Nav drawer adds further objects on `lv_layer_top()`. | High | Medium | Profile with `heap_caps_print_heap_info()` after Phase 4B build. Allocate chart data buffers from PSRAM explicitly. |
| R-2 | **FreeRTOS stack overflow in `sensor_task`** — Phase 5.3 added `history_manager_add_sample()` (a few floats + a mutex take, no recursion) inside `sensor_task()`, called every 60th iteration. Stack left at 4096 bytes (not bumped — the added call is small), but not yet measured on hardware. | Medium | Low | Verify with `uxTaskGetStackHighWaterMark()` on real device before Phase 5.4 adds any further work to this task. Bump to 6144 bytes only if headroom is actually tight. |
| R-3 | **ENS160 warm-up UX gap** — ENS160 requires ~60 s warm-up before TVOC readings are valid. During warm-up the dashboard shows `"--"` / `"ERROR"` badge which may alarm users. | Medium | High | Add `SENSOR_LEVEL_WARMING` state and `"Warming..."` badge text in Phase 7. Do not display `"ERROR"` during expected warm-up period. |
| R-4 | **Medical device regulatory compliance not addressed** — IVF laboratory use implies IEC 62304 (medical device software lifecycle), ISO 14971 (risk management), and potential IVD (in-vitro diagnostic) regulations. Current architecture has no FMEA, no safety classification, no traceability matrix, and no calibration traceability. | Critical | High | A formal regulatory compliance review is required before any clinical or laboratory deployment. This is outside the scope of the current development roadmap but must be planned. |
| R-5 | **Chart Y-axis fixed at 0–1000 ppb** — If real ENS160 readings exceed 1000 ppb (possible in severe contamination or calibration error), the chart clips silently. For a monitoring device, silent clipping is a data integrity risk. | High | Low | Add dynamic range update or visible clip indicator in Phase 7. Log all out-of-range values. |
| R-6 | **Power-cycle history loss** — `history_manager`'s hourly ring buffer (Phase 5.3, now live) is RAM-only. A brownout or intentional restart clears all trend data. Accepted for now but must be resolved before clinical use. | Medium | High | NVS/SD persistence of `history_manager` state is Phase 9. Document limitation in release notes until then. |
| R-7 | **No accurate timestamps** — Without an RTC, all sensor records are stamped with `esp_timer_get_time()` (ms since boot). Records lose wall-clock time on every reboot. Log data is not time-correlated to patient events. | High | High | Add PCF8563 I2C RTC in Phase 7. Until then, surface `"No RTC"` warning on logs screen. |
| R-8 | **PSRAM as single point of failure** — The 261 KB framebuffer and 261 KB LVGL draw buffer both require PSRAM. A PSRAM initialisation failure causes a boot panic. There is no graceful degradation path to an error screen. | Medium | Low | Add `assert(fb != NULL)` with UART error output and infinite loop in `display_driver.c` so the failure mode is explicit. Phase 10 adds a proper error screen. |
| R-9 | **Badge style accumulation under sustained 1 Hz update** — `lv_obj_set_style_bg_color(s_badge, color, 0)` at 1 Hz. Risk of LVGL style list growth if the implementation does not recycle the slot. | Low | Low | Verify with `lv_obj_get_local_style_cnt()` during a 30-minute soak test in Phase 4B. |
| R-10 | **Nav drawer animation with `full_refresh=1`** — Animating the drawer panel's x-position triggers a full-frame flush on every animation tick (every 5 ms). A 200 ms slide = ~40 full-frame flush cycles. At 261 KB per flush this is ~10 MB of DMA traffic over the animation. May cause perceptible jitter. | Medium | Medium | Benchmark animation smoothness on device in Phase 4B. If jitter is unacceptable, reduce drawer width, shorten animation to 100 ms, or switch to a fade rather than slide. |

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
