# IVF VOC Monitor — Architecture & Developer Guide

**Project:** IVF Environment Monitoring System (EMS) — VOC / Temperature / Humidity / CO₂  
**Hardware:** Elecrow CrowPanel ESP32-S3 4.3" HMI (SKU: DIS06043H, v2.1)  
**Framework:** ESP-IDF 5.3.1 (pure — no Arduino layer)  
**UI:** LVGL 8.4.0 (managed component, `idf_component.yml` pins `>=8.3.0, <9.0.0`)  
**Version:** 1.0.0 — Phase 4B (Nav Drawer) complete · Phase 4.1 (Shared UI Framework) complete · Phase 4.2.6 (Hardware Validation Polish) complete · Dashboard FROZEN · **UI freeze resolved — LVGL-timer dashboard refresh**

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
    │       ├── screen_chart.h/.c        ← Phase 4A complete (lv_chart TVOC history — see Section 5)
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
        └── history_manager.c/.h    ← Phase 4C (planned) — hourly/daily TVOC ring buffers
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
- Alarm types: `VOC_HIGH`, `TEMP_HIGH`, `TEMP_LOW`, `HUM_HIGH`, `HUM_LOW`, `CO2_HIGH`, `SENSOR_ERROR`
- Persists active state in-RAM only (NVS serialization is Phase 8)

---

### `data/history_manager` — Phase 4C (planned)
- Stores and aggregates 1 Hz TVOC samples into hourly and daily ring buffers
- Fed by `sensor_task()` via `history_push(voc_ppb)` — the only write path
- Queried by `screen_chart.c` via `history_get_hourly()` / `history_get_daily()` — the only read path
- Zero LVGL dependency — pure C data layer
- See **Screen: Chart** in Section 5 for the complete data model and public API

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

### Screen: Chart (272 × 480 portrait) — Phase 4A complete · Phase 4C–4D planned

#### Architecture

The chart screen is a **pure display layer** — it renders only. All aggregation, storage, and retrieval
of sensor history is the sole responsibility of `history_manager.c`.

```
screen_chart.c ─── queries ───► history_manager.c
                                      │
                              ring buffers (hourly / daily)
                                      ▲
                                      │
                     sensor_task() → history_push(voc_ppb)
```

#### Phase 4A — Chart UI Layout (COMPLETE)

> **Note:** Phase 4A was implemented with a bottom tab bar (IVF_CONTENT_H = 386). Phase 4B
> (Navigation Drawer) will remove the tab bar, update IVF_CONTENT_H to 436, and increase the
> chart height from 306 to 356 px. The widget hierarchy and layout below reflect the
> **post-Phase-4B target state**.

**Widget hierarchy (post Phase 4B):**
```
screen_chart (lv_obj, 272×480)
├── header   (ui_build_header "TVOC HISTORY")         y=0,   h=44
└── content  (lv_obj, 272×436)                         y=44
    ├── period_bar  (lv_obj, 272×40)                   y=0 in content
    │   ├── btn_7d   (lv_btn, 80×34, toggle)
    │   ├── btn_30d  (lv_btn, 80×34, toggle)
    │   └── btn_90d  (lv_btn, 80×34, toggle — default active)
    ├── chart  (lv_chart, 272×356, line mode)          y=40 in content
    │   ├── y-axis range: 0–1000 ppb
    │   ├── h-line at 300 ppb  (IVF_COLOR_WARNING)
    │   ├── h-line at 500 ppb  (IVF_COLOR_DANGER)
    │   └── series s_ser_tvoc  (IVF_COLOR_PRIMARY)
    └── lbl_no_data  (lv_label, centred)               visible when series is empty

[≡] nav_btn on lv_layer_top()                          lower-left overlay, always visible
```

**Screen layout (post Phase 4B):**
```
┌── Header 272×44 ─────────────────────────┐
│             TVOC HISTORY                 │
├── Period bar 272×40 ──────────────────────┤
│   [  7D  ]    [  30D  ]    [  90D  ]     │  toggle group, active = primary fill
├── Chart 272×356 ──────────────────────────┤
│ 1000 ─────────────────────────────────── │
│  500 ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─  │  ← ALARM  (IVF_COLOR_DANGER,  dashed)
│  300 ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─  │  ← WARN   (IVF_COLOR_WARNING, dashed)
│    0 ─────────────────────── x-labels   │
│                                          │
│ [≡]                           (436 total)│  ← floating nav button lower-left
└───────────────────────────────────────────┘
```

**Phase 4A public API (`screen_chart.h`):**
```c
lv_obj_t *screen_chart_create(void);   // build all widgets, load placeholder series
void      screen_chart_refresh(void);  // Phase 4D: query history_manager and redraw
```

**Phase 4A deliverables:**
- Period selector toggle group: tapping 7D / 30D / 90D changes x-point count (no data binding yet)
- `lv_chart` in line mode, correct y-axis (0–1000), placeholder flat series (zero line)
- Threshold lines at 300 ppb and 500 ppb rendered as horizontal overlay
- `lbl_no_data` label shown when series has no real data
- `screen_chart_refresh()` skeleton — no-op until Phase 4D
- **Zero dependency on `history_manager`** — Phase 4A builds and flashes without any data module

#### Phase 4C — History Manager (PLANNED)

New module: `main/data/history_manager.c/.h`

**Responsibilities:**
- Receive 1 Hz TVOC samples via `history_push(float voc_ppb)` (called from `sensor_task()`)
- Aggregate: per-minute avg → per-hour avg/min/max → per-day avg/min/max
- Expose query API for each view period

**Data model:**

| View | Resolution | Max points | RAM |
|------|-----------|------------|-----|
| 7D | Hourly avg | 168 (7×24) | ~2 KB |
| 30D | Daily avg | 30 | ~0.4 KB |
| 90D | Daily avg | 90 | ~1.1 KB |

Ring buffers are allocated from PSRAM (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`) with internal SRAM
fallback. Total ~3.5 KB — fits comfortably alongside existing allocations.

**Public API:**
```c
typedef struct {
    float    avg_ppb;
    float    min_ppb;
    float    max_ppb;
    uint32_t timestamp_ms;
} history_point_t;

esp_err_t history_manager_init(void);
void      history_push(float voc_ppb);                                   // called at 1 Hz
uint16_t  history_get_hourly(history_point_t *out, uint16_t max_count);  // 7D view
uint16_t  history_get_daily (history_point_t *out, uint16_t max_count);  // 30D/90D view
```

**Coupling rule:** `history_manager` is called only by `sensor_manager.c` (push) and `screen_chart.c`
(query). It has zero LVGL dependency — pure C data layer. NVS/SD persistence is deferred to Phase 9.

#### Phase 4D — Chart Data Binding (PLANNED)

Wire `screen_chart_refresh()` to `history_manager` queries:
- Period button event → `screen_chart_set_period(period_t p)` stores current period
- `screen_chart_refresh()` selects `history_get_hourly()` (7D) or `history_get_daily()` (30D/90D)
- `lv_chart_set_point_count(s_chart, returned_count)` then `lv_chart_set_value_by_id()` loop
- Handle partial data (first startup, < 7 days of history): chart reflects actual data count
- `screen_chart_refresh()` called from `dash_timer_cb` (LVGL timer, `ui.c`) when chart screen is active — guard with `if (s_current == SCREEN_CHART)` before calling

**Files changed in Phase 4D:**
- `main/ui/screens/screen_chart.c` — implement `screen_chart_refresh()` and `screen_chart_set_period()`
- `main/sensors/sensor_manager.c` — add `history_push(fresh.voc_ppb)` in `sensor_task()` after `alarm_manager_check()`
- `main/CMakeLists.txt` — add `"data/history_manager.c"`

### Screen: Logs (272 × 480 portrait) — Phase 5 stub
```
┌── Header 272×44 ───────────────────────────┐
│              DATA LOGS                     │
├── Content 272×436 ─────────────────────────┤
│  [empty — Phase 5]                         │
│                                            │
│ [≡]                                        │  ← floating nav button (lv_layer_top)
└────────────────────────────────────────────┘
```

### Screen: Settings (272 × 480 portrait) — Phase 6 stub
```
┌── Header 272×44 ───────────────────────────┐
│               SETTINGS                     │
├── Content 272×436 ─────────────────────────┤
│  [empty — Phase 6]                         │
│                                            │
│ [≡]                                        │  ← floating nav button (lv_layer_top)
└────────────────────────────────────────────┘
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
| 4 | LVGL 8.4.0 port | ✅ Complete | Full-frame PSRAM draw buffer, `full_refresh=1`, `LV_DISP_ROT_NONE`, FreeRTOS task, mutex. Touch read callback swaps x↔y to correct axis convention (see Display Rotation). |
| 5 | LVGL config | ✅ Complete | `CONFIG_LV_CONF_SKIP=y` — config via `sdkconfig.defaults`; light theme; Montserrat 12–48pt |
| 6 | `sdkconfig.defaults` | ✅ Complete | PSRAM enabled (N4R2), 240 MHz, 4 MB flash, RGB IRAM-safe, 1 kHz tick |
| 7 | Stale files cleaned | ✅ Done | `hello_world_main.c` and stale `sdkconfig` deleted |
| 8 | `app_main.c` | ✅ Complete | Init sequence; `app_main` returns after `ui_init()` — no refresh task |
| 9 | Sensor manager + backend | ✅ Phase 3B complete | Framework separated from backend. Active: `sensor_backend_sim.c`. Real driver goes in `sensor_backend_hw.c` (Phase 7). |
| 10 | Alarm manager | ✅ Complete | Debounce (3 samples), 50-entry ring buffer, NVS ack |
| 11 | NVS thresholds | ✅ Complete | Load on boot from `ivf_cfg`; save from Settings (Phase 6) |
| 12 | Screen: Splash | ✅ Complete | Progress bar, 6-step timer, auto-advance to Dashboard |
| 13 | UI framework | ✅ Phase 4B complete | Light theme, header builder, instant navigation. Phase 4B: nav drawer on `lv_layer_top()` replaces tab bar. Phase 4.1: reusable component layer added. |
| 14 | Screen: Dashboard | ✅ Phase 4.2.6 complete · **FROZEN** | `header_t` + `card_t` + `voc_gauge_t`; title "DASHBOARD"; WiFi far right (x=244); SD removed; sparklines removed; `CARD_H=90`; full-screen drawer with new top section. |
| 15 | Screen: Chart | ✅ Phase 4A complete | Period selector (7D/30D/90D), lv_chart, threshold lines at 300/500 ppb, "No data yet" label. Phase 4C (history_manager) and 4D (data binding) follow. |
| 16 | Navigation Drawer (`nav_drawer`) | ✅ Phase 4B complete | Floating `[≡]` button + slide-in drawer on `lv_layer_top()`; replaces bottom tab bar |
| 16.1 | Shared UI components (`components/`) | ✅ Phase 4.1 complete | 7 reusable components: `navigation_drawer`, `header`, `circular_gauge`, `card`, `status_badge`, `icon_button`, `assets` |
| 16.2 | `header_enable_menu()` | ✅ Phase 4.2.1 complete | Hamburger `[≡]` button in header; leaf icon hidden; title repositioned; callback-based — header does not own the drawer |
| 16.3 | Navigation Drawer Wiring (`navigation_drawer` + `ui.c`) | ✅ Phase 4.2.2 complete | `navigation_drawer_t` integrated into `ui.c`; FAB visible via `create_fab=true`; `ui_nav_drawer_toggle()` added to `ui.h`; drawer items with NAVIGATE header, active highlight (bg+icon+text), pressed state |
| 16.4 | Dashboard Migration | ✅ Phase 4.2.3 complete | `screen_dashboard.c` migrated to `header_t` + `card_t` + `assets_draw_*()`; `create_fab=false`; gauge code untouched |
| 16.5 | VOC Gauge Component | ✅ Phase 4.2.4 complete | `voc_gauge_t` — product-specific TVOC gauge; progressive arc zones, badge (GOOD/MODERATE/POOR/UNHEALTHY), 500 ms animation; TD-13 (`circular_gauge.c` font fix) resolved |
| 16.6 | Dashboard Final Polish | ✅ Phase 4.2.5 complete | Header 80 px right column (SD x=160, WiFi x=136); time/date right-aligned via `LV_ALIGN_TOP_RIGHT`; title fixed-width + `LV_LABEL_LONG_CLIP`; humidity `lbl_name` x 18→22; VOC gauge initialises to `NO_READING`; MODERATE badge dark text; `nav_drawer.c` removed from build |
| 16.7 | Hardware Validation Polish | ✅ Phase 4.2.6 complete | WiFi far right (`HDR_WIFI_X=244`); SD icon removed; `HDR_TIME_ROFS=32`; title font `IVF_FONT_NORMAL`; title "DASHBOARD"; sparklines removed (`CARD_H=90`); drawer full-screen (480 px, y=0); `DRAWER_HEADER_H=148`; new top section (blue circle + shield + badge + title + pill); "TVOC Chart" / "Data Logs" nav item labels; version footer; `assets_draw_shield()` added; `assets_draw_humidity()` updated (16×22 teardrop) |
| 16.8 | UI Freeze Fix | ✅ Phase 4.2.7 complete | `ui_refresh_task` removed from `app_main.c`; `ui_dashboard_refresh()` removed from `ui.c`/`ui.h`; dashboard refresh moved to `lv_timer_create(dash_timer_cb, 1000)` inside LVGL task; root cause: same-priority mutex starvation between tasks caused `lv_timer_handler()` skips, freezing animations and touch. TD-2 resolved. |
| 17 | Screen: Logs | ⬜ Stub | Phase 5 — data log table not yet implemented |
| 18 | Screen: Settings | ⬜ Stub | Phase 6 — brightness/threshold controls not yet implemented |

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
| 4C | History Manager | ⬜ PLANNED | `data/history_manager.c/.h` |
| 4D | Chart Data Binding | ⬜ PLANNED | `screen_chart.c`, `sensor_manager.c`, `CMakeLists.txt` |
| 5 | Logs Screen | ⬜ PLANNED | `screen_logs.c`, `data/record_manager.c/.h` |
| 6 | Settings Screen | ⬜ PLANNED | `screen_settings.c`, `data/config_manager.c/.h`, `display_driver.c` |
| 7 | Sensor Framework | ⬜ PLANNED | `sensors/sensor_backend_hw.c`, ENS160 + AHT21 driver files |
| 8 | Alarm Framework | ⬜ PLANNED | `data/alarm_manager.c/.h`, alarm UI |
| 9 | Storage Framework | ⬜ PLANNED | `data/record_manager.c`, `data/sd_export.c/.h`, SNTP |
| 10 | Production Hardening | ⬜ PLANNED | OTA, watchdog, display sleep, memory audit |

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

### Phase 4C — History Manager (PLANNED)

**Files to create:**
- `main/data/history_manager.c/.h` — hourly and daily ring buffers; `history_push()`, `history_get_hourly()`, `history_get_daily()`

**Files to modify:**
- `main/CMakeLists.txt` — add `"data/history_manager.c"` (INCLUDE_DIRS already contains `"data"`)

See **Screen: Chart** in Section 5 for the complete data model, ring buffer design, and public API.

---

### Phase 4D — Chart Data Binding (PLANNED)

**Files to modify:**
- `main/ui/screens/screen_chart.c` — implement `screen_chart_refresh()` and `screen_chart_set_period()`; query `history_get_hourly()` or `history_get_daily()` per active period; update `lv_chart` series
- `main/sensors/sensor_manager.c` — add `history_push(fresh.voc_ppb)` in `sensor_task()` after `alarm_manager_check()`

---

### Phase 5 — Logs Screen + Record Manager (PLANNED)

**Files to create:**
- `main/data/record_manager.c/.h` — 1-minute averaged sensor snapshots; 1440-record ring buffer (24 h)

**Files to modify:**
- `main/ui/screens/screen_logs.c` — `lv_table`, columns: Time 60 / TVOC 60 / Temp 52 / Hum 44 / Status 56 = 272 px; scrollable, 436 px content height; status cell coloured by level
- `main/CMakeLists.txt` — add `"data/record_manager.c"`

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
- `main/sensors/ens160_driver.c/.h` — I2C mode set, TVOC/eCO₂ read, compensation write, validity flag check
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
- `main/data/record_manager.c` — add NVS flush on 5-minute cycle; restore records on boot
- `main/ui/screens/screen_logs.c` — add "Export CSV" button (visible only when SD detected)
- `main/data/history_manager.c` — add NVS persistence of ring buffer state (currently RAM-only since Phase 4C)

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
| TD-8 | `history_manager` ring buffers (Phase 4C) are RAM-only — power cycle clears all trend history. NVS/SD persistence is Phase 9. | `data/history_manager.c` | Phase 9 |
| TD-9 | `alarm_manager` ring buffer is RAM-only — alarm history lost on reboot. | `data/alarm_manager.c` | Phase 8 |
| TD-10 | `screen_chart_refresh()` is a silent no-op. Placeholder label added in Phase 4A; data binding is Phase 4D. | `screen_chart.c` | Phase 4D |
| TD-11 | `sensor_manager_reload_thresholds()` and `alarm_manager_reload_thresholds()` do not exist. Settings save requires a reboot to take effect. | `sensor_manager.c`, `alarm_manager.c` | Phase 6 |
| TD-12 | Bottom tab bar is still present in all four content screens. `ui_build_tab_bar()` calls and `IVF_TAB_H` references must be removed when nav drawer is implemented. | `screen_dashboard.c`, `screen_chart.c`, `screen_logs.c`, `screen_settings.c`, `ui.c`, `ui.h` | Phase 4B |
| ~~TD-13~~ | ~~`circular_gauge.c` font references~~ | ~~`circular_gauge.c`~~ | ✅ Resolved Phase 4.2.4 — replaced `&lv_font_montserrat_48/16/12` with `IVF_FONT_HUGE/NORMAL/SMALL` |

---

## 12. Risk Register

| # | Risk | Severity | Likelihood | Mitigation |
|---|------|----------|-----------|-----------|
| R-1 | **LVGL heap exhaustion** — Dashboard and Chart both live in LVGL memory simultaneously. A fully implemented chart screen (lv_chart + 90 points + buttons + axis labels) significantly increases heap pressure. Nav drawer adds further objects on `lv_layer_top()`. | High | Medium | Profile with `heap_caps_print_heap_info()` after Phase 4B build. Allocate chart data buffers from PSRAM explicitly. |
| R-2 | **FreeRTOS stack overflow in `sensor_task`** — Adding `history_manager_push()` (float aggregation math) inside `sensor_task()` increases stack usage. Current stack: 4096 bytes. | Medium | Medium | Increase `sensor_task` stack to 6144 bytes in Phase 4C. Monitor `uxTaskGetStackHighWaterMark()` during integration test. |
| R-3 | **ENS160 warm-up UX gap** — ENS160 requires ~60 s warm-up before TVOC readings are valid. During warm-up the dashboard shows `"--"` / `"ERROR"` badge which may alarm users. | Medium | High | Add `SENSOR_LEVEL_WARMING` state and `"Warming..."` badge text in Phase 7. Do not display `"ERROR"` during expected warm-up period. |
| R-4 | **Medical device regulatory compliance not addressed** — IVF laboratory use implies IEC 62304 (medical device software lifecycle), ISO 14971 (risk management), and potential IVD (in-vitro diagnostic) regulations. Current architecture has no FMEA, no safety classification, no traceability matrix, and no calibration traceability. | Critical | High | A formal regulatory compliance review is required before any clinical or laboratory deployment. This is outside the scope of the current development roadmap but must be planned. |
| R-5 | **Chart Y-axis fixed at 0–1000 ppb** — If real ENS160 readings exceed 1000 ppb (possible in severe contamination or calibration error), the chart clips silently. For a monitoring device, silent clipping is a data integrity risk. | High | Low | Add dynamic range update or visible clip indicator in Phase 7. Log all out-of-range values. |
| R-6 | **Power-cycle history loss** — Phase 4C ring buffers are RAM-only. A brownout or intentional restart clears all trend data. This is accepted for Phase 4C but must be resolved before clinical use. | Medium | High | NVS persistence of `history_manager` state is Phase 9. Document limitation in release notes until then. |
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
