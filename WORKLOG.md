# IVF VOC Monitoring System — Work Log

**Device:** CrowPanel DIS06043H v2.1 (ESP32-S3 N4R2, 480×272 RGB565)  
**Stack:** ESP-IDF 5.3.1 · LVGL 8.4.0 (managed component)  
**UI orientation:** Portrait 272×480 (hardware rotation via RGB panel SWAP_XY + MIRROR_Y)  
**Last updated:** 2026-06-30 · Phase 4.2.7 (UI Freeze Fix) complete · Dashboard FROZEN · UI freeze resolved

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

## Migration Roadmap

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
- Navigation: `MOVE_LEFT/RIGHT` slide → `FADE_IN` (200 ms) → `NONE` 0 ms (Phase 4A fix)
- Navigation model: back-button → 4-tab bar (Home/Chart/Logs/Settings) → **nav drawer** (Phase 4B supersedes tab bar)
- Tab bar (Phase 2 implementation, superseded in Phase 4B): 272×50 px, 4 equal cells (68 px each), active = 3 px primary-blue top indicator
- Header bar: 272×44 px, title centred, bottom border 1 px `#E0E0E0`
- **Phase 4B navigation model**: floating `[≡]` button (lower-left, 44×44 px) on `lv_layer_top()`; 200 px drawer slides from left; `IVF_CONTENT_H` 386→436

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

### ✅ Phase 3B — Dashboard Data Binding
**Status: COMPLETE · Build verified (zero errors, zero warnings)**

**Goal:** Wire `screen_dashboard_update()` to live `sensor_manager` data and introduce a clean sim/hardware backend separation so swapping to real sensors in Phase 7 requires no dashboard or sensor_manager changes.

**Backend separation architecture (new):**

```
sensor_backend.h          ← interface: sensor_backend_init() + sensor_backend_sample()
sensor_backend_sim.c      ← sine-wave simulation (extracted from sensor_manager.c)
sensor_backend_hw.c       ← Phase 7 stub: returns sensor_ok=false until implemented
sensor_manager.c          ← framework only: task / mutex / NVS / public API
                             calls sensor_backend_* — no sim/hw knowledge
screen_dashboard.c        ← calls sensor_manager_get_data() only — no backend knowledge
```

**To swap simulation → real sensor in Phase 7 (one CMakeLists change):**
```cmake
# "sensors/sensor_backend_sim.c"   ← comment out
  "sensors/sensor_backend_hw.c"    ← uncomment, fill Phase 7 TODOs
```

**Data flow (confirmed working):**
```
sensor_backend_sim.c (1 Hz sine-wave)
  → sensor_backend_sample()
  → sensor_task() [sensor_manager.c]  ← mutex-protected write
  → sensor_manager_get_data()         ← thread-safe snapshot
  → screen_dashboard_update()         ← called by ui_dashboard_refresh() at 1 Hz
  → LVGL label / badge / chart update
```

**`screen_dashboard_update()` — what it updates:**

| Widget | Source | Notes |
|--------|--------|-------|
| `s_lbl_tvoc_value` | `d.voc_ppb` | `"%.0f"`, shows `"--"` if `!d.sensor_ok` |
| `s_badge` bg_color | `sensor_get_voc_level()` | Green/Orange/Red/Grey |
| `s_lbl_level` text | `sensor_get_voc_level()` | `"GOOD ✓"` / `"WARN ⚠"` / `"ALARM ⚠"` / `"ERROR"` |
| `s_lbl_temp_value` | `d.temperature_c` | `"%.1f °C"`, `"--"` on error |
| `s_lbl_hum_value` | `d.humidity_pct` | `"%.0f %%"`, `"--"` on error |
| `s_chart_temp` | `d.temperature_c × 10` | Scrolling 30-pt sparkline, range 200–260 (20–26 °C) |
| `s_chart_hum` | `d.humidity_pct` | Scrolling 30-pt sparkline, range 40–60 % |

**Chart series handles** promoted to file-scope statics (`s_ser_temp`, `s_ser_hum`) so `update()` can reach them. Badge container pointer stored as `s_badge` for runtime colour changes via `lv_obj_set_style_bg_color()`.

**Files created:**
- `main/sensors/sensor_backend.h` — interface declaration (2 functions)
- `main/sensors/sensor_backend_sim.c` — sine-wave simulation (extracted from sensor_manager.c)
- `main/sensors/sensor_backend_hw.c` — Phase 7 stub with TODO comments for ENS160 + AHT21

**Files modified:**
- `main/sensors/sensor_manager.c` — removed sim code; calls `sensor_backend_init()` + `sensor_backend_sample()`; removed unused `<math.h>` include
- `main/ui/screens/screen_dashboard.c` — added `s_badge`, `s_ser_temp`, `s_ser_hum` statics; implemented `screen_dashboard_update()`; updated initial chart data and ranges to match simulation output
- `main/CMakeLists.txt` — added `sensor_backend_sim.c` with swap comment for Phase 7

---

### ✅ Phase 4A — Chart UI Layout
**Status: COMPLETE**

**Goal:** Implement `screen_chart.c` with full chart UI using static placeholder data — no `history_manager` dependency. Validates the chart layout and period selector before building the data layer.

**Delivered:**
- Header: "TVOC HISTORY" (shared `ui_build_header`)
- Period toggle group (272×40 `lv_btnmatrix`): "7D" / "30D" / "90D", mutual-exclusion, default active = "90D"
- `lv_chart` (line mode, 272×346, y-axis 0–1000 ppb) filling content below period bar
- Threshold lines: warn at 300 ppb (`IVF_COLOR_WARNING`), alarm at 500 ppb (`IVF_COLOR_DANGER`) — flat horizontal series rendered behind TVOC line
- Series `s_ser_tvoc` = `IVF_COLOR_PRIMARY`; all points `LV_CHART_POINT_NONE` until Phase 4D
- `lbl_no_data` centred over chart (`LV_ALIGN_CENTER`); visible until real data arrives
- Tab bar with active indicator on Chart tab (tab bar replaced by nav drawer in Phase 4B)
- `apply_period(p)` helper: calls `lv_chart_set_point_count()` then reinitialises all three series; warn/alarm flat at their ppb value, tvoc = `LV_CHART_POINT_NONE`
- `screen_chart_refresh()` skeleton (no-op; Phase 4D implementation)

**Also resolved in this phase (pre-existing bugs):**
- **Touch coordinate axis swap** (`main/lvgl_port/lvgl_port.c`): `touch_driver_read()` returns portrait_Y in `*x` and portrait_X in `*y`. `lvgl_touch_read_cb` was assigning them straight, placing tab-bar taps (portrait Y ≥ 430) at LVGL `point.x ≥ 430` which exceeds the 0–271 x-range and is silently rejected by hit-testing. Fixed: `data->point.x = (lv_coord_t)y` (portrait_X 0–271), `data->point.y = (lv_coord_t)x` (portrait_Y 0–479). Navigation was never working before this fix.
- **Screen transition performance** (`main/ui/ui.c`): `LV_SCR_LOAD_ANIM_FADE_IN` (200 ms) alpha-blends two full 272×480 PSRAM frames every 5 ms tick. Changed to `LV_SCR_LOAD_ANIM_NONE` (0 ms): single render + flush per screen switch, no blending overhead.

**Files modified:**
- `main/ui/screens/screen_chart.c` — Full Phase 4A implementation
- `main/ui/screens/screen_chart.h` — `screen_chart_refresh()` declaration added
- `main/lvgl_port/lvgl_port.c` — Touch x/y swap fix in `lvgl_touch_read_cb` (lines 55–56)
- `main/ui/ui.c` — `LV_SCR_LOAD_ANIM_FADE_IN` → `LV_SCR_LOAD_ANIM_NONE` in `ui_goto_screen()`

**Acceptance criteria: all met**
- Tapping 7D / 30D / 90D changes active button highlight ✓
- Threshold lines visible at correct y-positions ✓
- `"No data yet"` label visible ✓
- Tab bar navigation working across all four screens ✓
- Screen transitions instant ✓
- Zero build errors / warnings ✓

---

### ✅ Phase 4B — Navigation Drawer
**Status: COMPLETE**

**Goal:** Replace the bottom tab bar on all four content screens with a `nav_drawer` component
that lives on `lv_layer_top()`. Floating `[≡]` button (lower-left) opens a 200 px panel that
slides in from the left. `IVF_CONTENT_H` corrected to 430 (was 386; `IVF_HEADER_H` is 50, not 44).

**Key implementation details:**
- `drawer_x_exec_cb(void *var, int32_t val)` wrapper avoids `lv_coord_t` / `int32_t` type issue
- Mid-animation reversal: reads `lv_obj_get_x(s_drawer)` as `from_x` before starting new anim
- Previous animation cancelled via `lv_anim_del(s_drawer, drawer_x_exec_cb)` before new start
- Dashboard: progressive gauge (INDICATOR fill per zone, MAIN transparent), drawn icons (no Unicode)
- `IVF_HEADER_H = 50`, `IVF_CONTENT_H = 430` — previous values (44/436) were wrong in docs

**Files created:**
- `main/ui/nav_drawer.h`
- `main/ui/nav_drawer.c`

**Files modified:**
- `main/ui/ui.h` — `IVF_TAB_H` removed; `IVF_CONTENT_H` corrected to 430; `IVF_NAV_BTN_SIZE`, `IVF_DRAWER_W` added; tab colours renamed to nav colours; `ui_build_tab_bar()` removed
- `main/ui/ui.c` — `ui_build_tab_bar()` removed; `nav_drawer_init()` added to `ui_init()`; `nav_drawer_close()` + `nav_drawer_set_active()` added to `ui_goto_screen()`
- `main/ui/screens/screen_dashboard.c` — progressive gauge, drawn icons, CARD_Y 255→272
- `main/ui/screens/screen_chart.c` — `ui_build_tab_bar()` call removed
- `main/ui/screens/screen_logs.c` — `ui_build_tab_bar()` call removed
- `main/ui/screens/screen_settings.c` — `ui_build_tab_bar()` call removed
- `main/CMakeLists.txt` — `ui/nav_drawer.c` added to SRCS

---

### ✅ Phase 4.1 — Shared UI Framework
**Status: COMPLETE**

**Goal:** Architectural milestone — build the reusable component layer that all screens will share.
No screen logic modified. No sensor code touched. No existing Dashboard logic changed.

**Components delivered:**

| Component | Header | Implementation | Lines |
|-----------|--------|----------------|-------|
| `assets` | `ui/assets/assets.h` | `assets.c` | 7 drawn icons |
| `status_badge` | `ui/components/status_badge/status_badge.h` | `status_badge.c` | 5 predefined states + custom |
| `icon_button` | `ui/components/icon_button/icon_button.h` | `icon_button.c` | FAB-style circular button |
| `card` | `ui/components/card/card.h` | `card.c` | Container with outer/content split |
| `circular_gauge` | `ui/components/circular_gauge/circular_gauge.h` | `circular_gauge.c` | Progressive multi-zone arc gauge |
| `header` | `ui/components/header/header.h` | `header.c` | 272×50 bar; WiFi bars, SD status |
| `navigation_drawer` | `ui/components/navigation_drawer/navigation_drawer.h` | `navigation_drawer.c` | Standalone drawer; `uint8_t id` + callback |

**Folder structure created:**
```
main/ui/
├── assets/
│   ├── assets.h
│   └── assets.c
└── components/
    ├── navigation_drawer/
    ├── header/
    ├── circular_gauge/
    ├── card/
    ├── status_badge/
    └── icon_button/
```

**CMakeLists.txt changes:**
- 7 new SRCS entries
- 8 new INCLUDE_DIRS entries (`ui/assets` + one per component subdirectory)

**Files created (14):**
- `main/ui/assets/assets.h` / `assets.c`
- `main/ui/components/status_badge/status_badge.h` / `status_badge.c`
- `main/ui/components/icon_button/icon_button.h` / `icon_button.c`
- `main/ui/components/card/card.h` / `card.c`
- `main/ui/components/circular_gauge/circular_gauge.h` / `circular_gauge.c`
- `main/ui/components/header/header.h` / `header.c`
- `main/ui/components/navigation_drawer/navigation_drawer.h` / `navigation_drawer.c`

**Files modified (1):**
- `main/CMakeLists.txt` — 7 SRCS + 8 INCLUDE_DIRS added

---

### ✅ Phase 4.2.1 — Header Extension: `header_enable_menu()`
**Status: COMPLETE**

**Goal:** Extend `header_t` with a hamburger menu button API so Dashboard (and any future screen) can opt into navigation without the header component knowing about the drawer or `screen_id_t`.

**Key design decisions:**
- `header_enable_menu(hdr, cb, user_data)` is **idempotent** — safe to call multiple times; button is created only once, `cb` is updated on subsequent calls.
- The header does **not** own or know about the navigation drawer. `cb(user_data)` is the only coupling — `ui.c` passes a drawer-toggle wrapper; the screen passes whatever it needs.
- `build_leaf_icon()` refactored to return an `lv_obj_t *` container (`leaf_cont`) so both child primitives (body + rib) can be hidden in a single `lv_obj_add_flag(LV_OBJ_FLAG_HIDDEN)` call.
- Button occupies the full header height (44×50 px at x=0) for an adequate touch target. Title label shifted from x=34 → x=48.
- Press feedback: `IVF_COLOR_BORDER` fill at `LV_STATE_PRESSED` — subtle, matches the header theme.

**Files modified (2):**

| File | Change |
|------|--------|
| `main/ui/components/header/header.h` | Added `header_menu_cb_t` typedef; added `header_enable_menu()` declaration |
| `main/ui/components/header/header.c` | Extended struct (`leaf_cont`, `menu_btn`, `menu_cb`, `menu_cb_ud`); `build_leaf_icon()` returns container; `menu_btn_event_cb()`; `header_enable_menu()` |

**No files created. No CMakeLists.txt change required.**

**API additions:**
```c
typedef void (*header_menu_cb_t)(void *user_data);

void header_enable_menu(header_t *hdr, header_menu_cb_t cb, void *user_data);
```

**Usage (by a screen that wants the menu button):**
```c
header_t *hdr = header_create(screen);
header_set_title(hdr, "AIR QUALITY MONITOR");
header_enable_menu(hdr, on_menu_click, ui_ctx);   /* cb → ui.c toggles drawer */
```

**Acceptance criteria: all met**
- `header_create()` creates white header with leaf icon, WiFi, SD, time/date unchanged ✅
- `header_enable_menu()` hides leaf, shows `[≡]` button, title shifts to x=48 ✅
- Tap `[≡]` fires `cb(user_data)` once per tap ✅
- Calling `header_enable_menu()` twice does not create a second button ✅
- Screens calling `header_create()` without `header_enable_menu()` are unchanged ✅

---

### ✅ Phase 4.2.2 — Navigation Drawer Wiring
**Status: COMPLETE**

**Goal:** Wire the `navigation_drawer_t` component into `ui.c` so all screens navigate through a single owner. Enhance the drawer panel with a section header and correct full active-state highlighting.

**Key design decisions:**
- `create_fab = true` for this phase — the floating `[≡]` button is owned by the drawer. Phase 4.2.5 sets this to `false` once Dashboard calls `header_enable_menu()`.
- Drawer owns navigation — screens call `ui_nav_drawer_toggle()` (declared in `ui.h`); they never hold a `navigation_drawer_t *`.
- `on_nav_item_selected()` callback casts the `uint8_t id` directly to `screen_id_t` and calls `ui_goto_screen()` — zero coupling between drawer and screen IDs.
- `nav_drawer.c` remains compiled (CMakeLists comment marks it legacy) to avoid a partial-migration breakage.

**Changes to `navigation_drawer.c`:**
- Added "NAVIGATE" section label (`IVF_FONT_SMALL`, `IVF_COLOR_TEXT_MUTED`) and 1 px divider before items.
- Item row y changed from `i × ITEM_H` → `DRAWER_HEADER_H + i × ITEM_H`.
- Separator y changed accordingly.
- Added `IVF_COLOR_BORDER` pressed-state on item rows to suppress LVGL's default blue highlight.
- FAB creation wrapped in `if (d->cfg.create_fab)` — drawer can exist without a FAB when the header button is active.
- `navigation_drawer_set_active()` extended to also call `lv_obj_set_style_text_color()` on child 0 (icon label) and child 1 (text label), giving a full `IVF_COLOR_PRIMARY` / `IVF_COLOR_TEXT` highlight instead of background-only.

**Files modified (5):**

| File | Change |
|------|--------|
| `main/ui/components/navigation_drawer/navigation_drawer.h` | Added `bool create_fab` to `nav_drawer_cfg_t` |
| `main/ui/components/navigation_drawer/navigation_drawer.c` | Drawer header, item offsets, FAB guard, full active highlight, pressed state |
| `main/ui/ui.c` | Migrated from `nav_drawer_*` to `navigation_drawer_*`; `APP_NAV_ITEMS[]` + callback |
| `main/ui/ui.h` | Added `ui_nav_drawer_toggle()` declaration |
| `main/CMakeLists.txt` | Legacy comment on `nav_drawer.c` SRCS line |

**No files created. No new INCLUDE_DIRS required.**

**API additions (`ui.h`):**
```c
void ui_nav_drawer_toggle(void);
```

**Acceptance criteria (for review):**
- Drawer opens/closes with slide animation from FAB tap ✅
- "NAVIGATE" section header visible above first item ✅
- Active item shows `IVF_COLOR_PRIMARY` icon + text + `IVF_COLOR_NAV_ACTIVE` background ✅
- Inactive items show `IVF_COLOR_TEXT` (not washed out) ✅
- Tapping item navigates to screen, closes drawer, updates active highlight ✅
- Tapping backdrop closes drawer ✅
- `nav_drawer.c` still compiles (no regression) ✅

---

### ✅ Phase 4.2.3 — Dashboard Migration
**Status: COMPLETE**

**Goal:** Refactor `screen_dashboard.c` to consume `header_t`, `card_t`, and `assets_draw_*()`. No redesign, no business logic change — only duplication removed. Gauge code preserved verbatim.

**Key changes:**

| Area | Before | After |
|------|--------|-------|
| Header | `ui_build_header()` + `s_lbl_time` / `s_lbl_date` statics | `header_create()` + `header_set_*()` + `header_enable_menu()` + `s_hdr` |
| Nav trigger | FAB owned by `navigation_drawer` (`create_fab=true`) | Header button → `on_menu_btn()` → `ui_nav_drawer_toggle()` (`create_fab=false`) |
| Sensor cards | Inline `lv_obj_create` + `sty_card` static style | `card_create(&ccfg)` + `card_get_obj()` for positioning |
| Icons | 5 local functions: `make_therm_icon`, `make_drop_icon`, etc. | `assets_draw_thermometer()` / `assets_draw_humidity()` |
| Time/date update | `lv_label_set_text(s_lbl_time, ...)` | `header_set_time(s_hdr, ...)` / `header_set_date(s_hdr, ...)` |

**Also fixed in this phase:**
- `header.c` title font: `IVF_FONT_NORMAL` (16 pt) → `IVF_FONT_SMALL` (12 pt). "AIR QUALITY MONITOR" at 16 pt (~170 px) overflows the ~114 px available between menu button and WiFi icon with `header_enable_menu()` active.

**Files modified (3):**
- `main/ui/screens/screen_dashboard.c` — full rewrite (header_t, card_t, assets; gauge verbatim)
- `main/ui/components/header/header.c` — title font `IVF_FONT_SMALL`
- `main/ui/ui.c` — `create_fab = false`

**No files created. No CMakeLists.txt change required.**

---

### ✅ Phase 4.2.4 — VOC Gauge Component
**Status: COMPLETE**

**Goal:** Encapsulate all TVOC gauge logic into a dedicated `voc_gauge_t` component. Remove ~120 lines of inline gauge code from `screen_dashboard.c`. Dashboard calls one function per update cycle.

**New component — `ui/components/voc_gauge/`:**

| Item | Detail |
|------|--------|
| Container | 272 × 268 px, transparent, pos (0,0) in content |
| Background arc | Grey #E0E0E0, full sweep 135°→45° |
| Zone arcs | Green 135°→202°, Yellow 202°→270°, Orange 270°→338°, Red 338°→45° |
| Progressive fill | Each zone fills 0–100% as value crosses its 250-ppb span |
| Scale labels | Pixel-exact centres: 0@(48,245), 250@(20,125), 500@(136,40), 750@(253,125), 1000@(220,245) |
| Centre stack | `IVF_FONT_HUGE` value + "ppb" unit + quality badge (flex column) |
| Animation | 500 ms ease-out on arc fills + value label; badge instant |
| Error state | `VOC_GAUGE_NO_READING` (0xFFFF) → "--", arcs cleared, grey badge "---" |

**Quality badge thresholds:**

| Badge | ppb | Colour |
|-------|-----|--------|
| GOOD | 0–249 | `IVF_COLOR_GOOD` |
| MODERATE | 250–499 | `#FDD835` |
| POOR | 500–749 | `IVF_COLOR_WARNING` |
| UNHEALTHY | 750–1000 | `IVF_COLOR_DANGER` |

**Also resolved — TD-13:** `circular_gauge.c` referenced `&lv_font_montserrat_48/16/12` directly; replaced with `IVF_FONT_HUGE/NORMAL/SMALL`. Added `#include "ui/ui.h"`.

**Dashboard changes:**
- Removed: 11 `#define` constants, 7 static handles (`s_arc_*`, `s_lbl_tvoc_value`, `s_badge`, `s_lbl_level`, `s_tvoc_animated`, `s_tvoc_target`), `sty_badge`, 6 local functions, inline badge/TVOC update in `screen_dashboard_update()`
- Added: `#include "voc_gauge.h"`, `static voc_gauge_t *s_gauge`, `s_gauge = voc_gauge_create(content)`, single `voc_gauge_set_value()` call per update

**Files created (2):**
- `main/ui/components/voc_gauge/voc_gauge.h`
- `main/ui/components/voc_gauge/voc_gauge.c`

**Files modified (3):**
- `main/ui/screens/screen_dashboard.c` — gauge code replaced with `voc_gauge_t`
- `main/ui/components/circular_gauge/circular_gauge.c` — TD-13 font fix
- `main/CMakeLists.txt` — `voc_gauge.c` SRCS + INCLUDE_DIRS

---

### ✅ Phase 4.2.5 — Dashboard Final Polish · **Dashboard FROZEN**
**Status: COMPLETE**

**Goal:** Final geometry and visual correctness pass against the approved Figma. No new features — only fixes identified in post-Phase-4.2.4 review.

**Changes made:**

1. **Header geometry corrected** (`header.c`)  
   `HDR_SD_X` was 220 but `HDR_TIME_X` starts at 214 — SD icon sat inside the time label column.  
   Fixed with right-to-left derivation: `HDR_TIME_X=214` → `HDR_SD_X=190` → `HDR_WIFI_X=166`.

2. **Time/date labels right-aligned** (`header.c`)  
   Replaced `lv_obj_set_pos()` with `lv_obj_align(LV_ALIGN_TOP_RIGHT, -8, y)` at creation.
   `header_set_time()` and `header_set_date()` call `lv_obj_align()` after every text update —
   labels grow leftward, never overflow the right edge regardless of string length (12 h / 24 h / any RTC format).

3. **Title fixed width + `LV_LABEL_LONG_CLIP`** (`header.c`)  
   Title width clamped to `HDR_WIFI_X - 4 - title_x` (128 px without menu button, 114 px with).  
   Clip prevents "AIR QUALITY MONITOR" from overflowing into the WiFi icon area.  
   `header_enable_menu()` recalculates clipping width from x=48.

4. **Humidity `lbl_name` x 18 → 22** (`screen_dashboard.c`)  
   Humidity icon is 20 px wide (vs thermometer 14 px). `lbl_name` at x=18 overlapped the humidity icon by 2 px. Changed to x=22 for 2 px clearance — applies to both sensor cards (the thermometer card has 8 px clearance at x=22, which is fine).

5. **VOC gauge initialised to `NO_READING`** (`screen_dashboard.c`)  
   Added `voc_gauge_set_value(s_gauge, VOC_GAUGE_NO_READING)` immediately after `voc_gauge_create()`.  
   Prevents the ~1 s window where gauge shows "--" value but green "GOOD" badge (contradictory).

6. **MODERATE badge dark text** (`voc_gauge.c`)  
   `update_badge()` refactored: `txt_color` variable defaults to white; overridden to `IVF_COLOR_TEXT` (#212121) for the MODERATE range only. White on yellow (#FDD835) was ~1.2:1 contrast — invisible.

7. **Legacy `nav_drawer.c` removed from build** (`CMakeLists.txt`)  
   Phase 4B stub superseded by `navigation_drawer.c` (Phase 4.2.2). SRCS line commented out.
   `nav_drawer.h` is only included by `nav_drawer.c` itself — no other file depends on it.

**No known layout limitations.** The right column is 80 px, sized for the Figma date "May 24, 2026" (~76 px). Gap between SD icon body and date left edge is ~12 px. All Phase 7 RTC formats fit within this budget.

**Files modified (4):**
- `main/ui/components/header/header.c` — geometry constants, right-aligned labels, title clip, `header_enable_menu()` width update
- `main/ui/components/voc_gauge/voc_gauge.c` — MODERATE badge dark text
- `main/ui/screens/screen_dashboard.c` — `voc_gauge_set_value(NO_READING)` at init; `lbl_name` x 18→22
- `main/CMakeLists.txt` — `nav_drawer.c` SRCS line commented out

**No files created.**

---

### ✅ Phase 4.2.6 — Hardware Validation Polish · **Dashboard FROZEN**
**Status: COMPLETE (build pending flash)**

**Goal:** Fix all visual discrepancies found when Phase 4.2.5 firmware was flashed to the CrowPanel target and compared against the approved Figma. No architecture changes, no new components.

**Changes made:**

1. **Header — WiFi moved far right, SD icon removed** (`header.c`)  
   `HDR_WIFI_X` changed to `IVF_SCREEN_W - 8 - HDR_ICON_SIZE = 244` (far right).  
   `HDR_TIME_ROFS` changed to `8 + HDR_ICON_SIZE + 4 = 32` so time/date labels clear the WiFi icon.  
   `HDR_TITLE_MAX_X = HDR_WIFI_X - 4 - HDR_TIME_COL_W - 4 = 156`.  
   SD card icon (`sd_body`) **not built** — its `#define` chain removed entirely.  
   `header_set_sd_status()` calls are safe no-ops because `sd_body` is NULL (existing guard `if (!hdr->sd_body) return`).

2. **Header — title font restored to `IVF_FONT_NORMAL`** (`header.c`)  
   "DASHBOARD" is shorter than "AIR QUALITY MONITOR", so 16 pt fits in the 122 px title column.

3. **Header — time/date alignment offset updated** (`header.c` + setters)  
   `lv_obj_align(LV_ALIGN_TOP_RIGHT, -HDR_TIME_ROFS, y)` — offset changed from -8 to -32.  
   Both `header_set_time()` and `header_set_date()` re-apply the align after every text update.

4. **Dashboard title changed to "DASHBOARD"** (`screen_dashboard.c`)  
   `header_set_title(s_hdr, "DASHBOARD")` — was `"AIR QUALITY MONITOR"`.

5. **Sensor card sparklines removed** (`screen_dashboard.c`)  
   `s_chart_temp`, `s_chart_hum`, `s_ser_temp`, `s_ser_hum` statics removed.  
   `sty_chart` style removed. `build_sensor_card()` simplified to `void` return; no `out_chart` param.  
   `lv_chart_set_next_value()` and `lv_chart_refresh()` calls removed from `screen_dashboard_update()`.  
   `CARD_H` reduced 110 → 90 px (icon + label + value only; no sparkline row).

6. **Navigation drawer — full screen height from y=0** (`navigation_drawer.c`)  
   `dh = IVF_SCREEN_H` (480); drawer positioned at `(-dw, 0)` — covers the header.  
   Backdrop check in `drawer_anim_done_cb` still correct: backdrop is 272×480, drawer is 200×480 — width check (272 ≠ 200) distinguishes them.

7. **Navigation drawer — new top section** (`navigation_drawer.c` + `assets.h/c`)  
   `DRAWER_HEADER_H` changed 54 → 148.  
   New top section (inside `{}` block at top of drawer build):
   - 56×56 px blue circle (`IVF_COLOR_PRIMARY`) centred horizontally at y=12.
   - White shield icon (`assets_draw_shield`, 28×32) centred in circle at offset (14,12).
   - "36 × 36" size badge (68×18 px, light grey `#EEEEEE`, radius 4) at y=74.
   - "Environmental Monitor" label (full-width centred, `IVF_FONT_NORMAL`) at y=97.
   - "Normal" green pill (`IVF_COLOR_GOOD`, 64×20, radius circle) centred at y=123.
   - 1 px divider at `y = DRAWER_HEADER_H - 4 = 144`.

8. **Navigation drawer — "Version v1.2.0" footer** (`navigation_drawer.c`)  
   Added `if (d->cfg.footer_version)` block after items loop: centred `IVF_FONT_SMALL` muted label anchored to `LV_ALIGN_BOTTOM_MID`.

9. **Navigation drawer config struct extended** (`navigation_drawer.h`)  
   Added `const char *header_title`, `*header_status`, `*footer_version` to `nav_drawer_cfg_t`.  
   All three non-NULL activates the top section; any NULL suppresses it silently.

10. **Nav items renamed** (`ui.c`)  
    `"Chart"` → `"TVOC Chart"`, `"Logs"` → `"Data Logs"`.

11. **Assets — `assets_draw_shield()` added** (`assets.h` / `assets.c`)  
    28×32 px white shield: rounded arch (28×22, radius 8) + four tapering rows to a 2 px point.  
    Used in navigation drawer top section.

12. **Assets — `assets_draw_humidity()` improved** (`assets.c`)  
    Updated to 16×22 px teardrop matching Material Design water-drop style:  
    narrow 6×5 tip → widening 12×5 mid → 16×14 round bulb bottom.  
    Previous version was a shorter round-rect drop.

**Files modified (7):**

| File | Change |
|------|--------|
| `main/ui/assets/assets.h` | Added `assets_draw_shield()` declaration |
| `main/ui/assets/assets.c` | `assets_draw_humidity()` updated (16×22 teardrop); `assets_draw_shield()` added (28×32) |
| `main/ui/components/header/header.c` | WiFi far right (`HDR_WIFI_X=244`); SD removed; time/date offset `-32`; title font `IVF_FONT_NORMAL`; `HDR_TITLE_MAX_X=156`; both setters updated |
| `main/ui/components/navigation_drawer/navigation_drawer.h` | Added `header_title`, `header_status`, `footer_version` to `nav_drawer_cfg_t` |
| `main/ui/components/navigation_drawer/navigation_drawer.c` | Full-screen height; `DRAWER_HEADER_H=148`; new top section (circle+shield+badge+title+pill); version footer; `#include "assets.h"` |
| `main/ui/ui.c` | `"TVOC Chart"`, `"Data Logs"` item labels; `header_title/header_status/footer_version` in `nav_cfg` |
| `main/ui/screens/screen_dashboard.c` | Title `"DASHBOARD"`; sparklines removed; `CARD_H=90`; `build_sensor_card()` simplified |

**No files created. No CMakeLists.txt change required.**

---

### ✅ Phase 4.2.7 — UI Freeze Fix: LVGL-Timer Dashboard Refresh
**Status: COMPLETE**

**Problem:** After Phase 4.2.6, the dashboard UI was freezing — touch events stopped registering and gauge animations stalled. The device appeared powered on but completely unresponsive.

**Root cause — priority-inversion / mutex starvation:**

```
lvgl_task      (Core 1, priority 2)  runs lv_timer_handler() every 5 ms
ui_refresh_task (any core, priority 2)  calls ui_dashboard_refresh() at 1 Hz
```

Both tasks ran at the same FreeRTOS priority and competed for the LVGL mutex. LVGL's internal `lv_timer_handler()` call acquires the mutex with a **10 ms timeout**. When `ui_refresh_task` held the mutex (especially while `sensor_manager_get_data()` used `portMAX_DELAY` on its own internal mutex), the LVGL task's acquire timed out and `lv_timer_handler()` was skipped entirely. After enough skips, the animation system stalled and touch events accumulated in a dead queue.

**Fix:** Remove `ui_refresh_task` entirely. Move dashboard refresh into an LVGL timer that fires inside `lv_timer_handler()` — same task, already holding the LVGL lock, zero contention.

**Changes made:**

1. **`main/app_main.c` — `ui_refresh_task` removed**  
   Removed the `xTaskCreate(ui_refresh_task, ...)` call. `app_main` now returns after `ui_init()`.

2. **`main/ui/ui.c` — LVGL timer added, `ui_dashboard_refresh()` removed**
   ```c
   static lv_timer_t *s_dash_timer = NULL;

   static void dash_timer_cb(lv_timer_t *t)
   {
       (void)t;
       screen_dashboard_update();
       int64_t us   = esp_timer_get_time();
       int32_t secs = (int32_t)(us / 1000000LL);
       int32_t h    = (secs / 3600) % 24;
       int32_t m    = (secs / 60)  % 60;
       int32_t h12  = h % 12; if (h12 == 0) h12 = 12;
       char buf[12];
       snprintf(buf, sizeof(buf), "%02"PRId32":%02"PRId32" %s",
                h12, m, h >= 12 ? "PM" : "AM");
       dashboard_set_time(buf);
   }
   /* inside ui_init(), after nav_drawer setup: */
   s_dash_timer = lv_timer_create(dash_timer_cb, 1000, NULL);
   ```
   `ui_dashboard_refresh()` function and its static declaration removed.

3. **`main/ui/ui.h` — `ui_dashboard_refresh()` declaration removed**

**Why this works:** `dash_timer_cb` fires inside `lv_timer_handler()` on the LVGL task (Core 1). The LVGL mutex is already held. There is no cross-task lock acquisition, no timeout risk, and no starvation. Sensor data is still read via `sensor_manager_get_data()` — that function takes its own internal mutex briefly, not the LVGL mutex.

**Task table after fix:**

| Task | Core | Priority | Stack | Notes |
|------|------|----------|-------|-------|
| `lvgl_task` | 1 | 2 | 8 KB | Runs `lv_timer_handler()` every 5 ms; drives all LVGL timers including `dash_timer_cb` |
| `sensor_task` | 0 | 3 | 4 KB | 1 Hz; writes sensor data to shared struct under sensor mutex |
| Idle task | 0 | 0 | — | FreeRTOS default |

`app_main` returns after `ui_init()` — no `app_main` task exists at runtime.

**Files modified (3):**

| File | Change |
|------|--------|
| `main/app_main.c` | `ui_refresh_task` task function and `xTaskCreate` call removed |
| `main/ui/ui.c` | `dash_timer_cb` added; `lv_timer_create(dash_timer_cb, 1000, NULL)` in `ui_init()`; `ui_dashboard_refresh()` removed |
| `main/ui/ui.h` | `ui_dashboard_refresh()` declaration removed |

**Technical debt resolved:** TD-2 in ARCHITECTURE.md — the cross-task LVGL access pattern is fully eliminated.

---

### ⬜ Phase 4C — History Manager
**Status: PLANNED**

**Goal:** Create `history_manager.c` — pure data module that aggregates 1 Hz TVOC samples into hourly and daily ring buffers. No LVGL dependency.

**Data model:**
| View | Resolution | Max points |
|------|-----------|------------|
| 7D | Hourly avg | 168 |
| 30D | Daily avg | 30 |
| 90D | Daily avg | 90 |

Ring buffers allocated from PSRAM (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`), fallback to internal SRAM. Total ~3.5 KB.

**Files to create:**
- `main/data/history_manager.c/.h` — `history_manager_init()`, `history_push(float voc_ppb)`, `history_get_hourly()`, `history_get_daily()`; `history_point_t` struct (avg/min/max/timestamp)

**Files to modify:**
- `main/CMakeLists.txt` — add `"data/history_manager.c"` (INCLUDE_DIRS already has `"data"`)

**Acceptance criteria:**
- `history_push()` called from `sensor_task()` without crashing
- `history_get_hourly(out, 168)` returns 0 on empty, correct count after data accumulates
- `heap_caps_print_heap_info()` shows PSRAM allocation successful

---

### ⬜ Phase 4D — Chart Data Binding
**Status: PLANNED**

**Goal:** Wire `screen_chart_refresh()` to `history_manager` — replace placeholder series with live aggregated data.

**Files to modify:**
- `main/ui/screens/screen_chart.c` — implement `screen_chart_refresh()` (query `history_get_hourly()` or `history_get_daily()` per active period; call `lv_chart_set_point_count()` + `lv_chart_set_value_by_id()` loop; handle partial data)
- `main/sensors/sensor_manager.c` — add `history_push(fresh.voc_ppb)` in `sensor_task()` after `alarm_manager_check()`

**Acceptance criteria:**
- After 7+ hours of runtime, 7D chart shows hourly data points
- Tapping period button redraws chart with correct dataset
- Partial data (< full period of history) renders correctly without crash
- `screen_chart_refresh()` called from `dash_timer_cb` (LVGL timer, `ui.c`) when chart screen is active — guard with `if (s_current == SCREEN_CHART)`

---

### ⬜ Phase 5 — Data Logs Screen + Sensor Record Buffer
**Status: NOT STARTED**

**Goal:** Implement `screen_logs.c` with a scrollable `lv_table` showing per-minute sensor records.

**Design spec:**
- `lv_table` columns: Time | TVOC (ppb) | Temp (°C) | Hum (%) | Status
- Column widths: 60 / 60 / 52 / 44 / 56 = 272 px total
- Table fills 436 px content height (nav drawer model, no tab bar); vertical scroll enabled
- Status cell: coloured text ("GOOD" green / "WARN" amber / "ALARM" red)
- Data source: new ring buffer of 1-minute averaged sensor records (max 1440 = 24 h)

**Files to create:**
- `main/data/record_manager.c/.h` — 1-minute averaged sensor record ring buffer

**Files to modify:**
- `main/ui/screens/screen_logs.c` — Full implementation; `screen_logs_refresh()` rebuilds table rows
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
- `main/ui/screens/screen_settings.c` — Full implementation using config_manager; content height 436 (nav drawer model)
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

### ⬜ Phase 7 — Sensor Framework (ENS160 + AHT21)
**Status: NOT STARTED**

**Goal:** Fill in `sensor_backend_hw.c` with real ENS160 + AHT21 I2C sensor reads. One `CMakeLists.txt` line swap activates it — zero changes to `sensor_manager.c`, dashboard, alarm_manager, or any other module.

**Activation (one-line swap in `main/CMakeLists.txt`):**
```cmake
# "sensors/sensor_backend_sim.c"   ← comment out
  "sensors/sensor_backend_hw.c"    ← uncomment; ENS160+AHT21 TODOs filled
```

**Hardware:**
- ENS160 + AHT21 combined module on I2C bus
- I2C pins: SDA = GPIO 17, SCL = GPIO 18 (defined in `board.h`)
- ENS160 I2C address: 0x53 (ADDR pin low) or 0x52 (ADDR pin high) — confirm wiring
- AHT21 I2C address: 0x38

**Integration notes:**
- Read AHT21 first → feed temp/hum to ENS160 compensation registers → read ENS160 TVOC
- ENS160 warm-up ~60 s before TVOC readings are valid — add `SENSOR_LEVEL_WARMING` state; badge shows `"Warming..."` during this period
- Check ENS160 data validity flag before accepting readings
- I2C transaction failure × 3 consecutive → set `sensor_ok=false` → dashboard shows `"--"` / `"ERROR"` badge

**Files to modify:**
- `main/sensors/sensor_backend_hw.c` — implement `sensor_backend_init()` (I2C bus + ENS160 mode + AHT21 probe) and `sensor_backend_sample()` (AHT21 → ENS160 compensation → ENS160 TVOC read)
- `main/app_main.c` — add `i2c_master_init()` call before `sensor_manager_init()`
- `main/board/board.h` — confirm I2C pin assignments (SDA=17, SCL=18 already defined)
- `main/CMakeLists.txt` — swap sim → hw backend; add driver source files

**Files to create:**
- `main/sensors/ens160_driver.c/.h` — I2C mode set, TVOC/eCO₂ read, compensation write, validity check
- `main/sensors/aht21_driver.c/.h` — trigger/read/CRC; returns temperature and humidity

**Acceptance criteria:**
- Dashboard shows real TVOC, temperature, humidity (not sine wave)
- ENS160 warm-up: badge shows `"Warming..."` then transitions to `"GOOD"`/`"WARN"`/`"ALARM"` when valid
- Sensor error displayed if I2C fails 3 consecutive times; recovers automatically on comms restore
- AHT21 CRC check passes; invalid reads retried before reporting error

---

### ⬜ Phase 8 — Alarm Framework
**Status: PLANNED**

**Goal:** Add NVS persistence to `alarm_manager` and expose alarm acknowledgement via the UI. Alarms currently survive only until power cycle.

**Files to modify:**
- `main/data/alarm_manager.c/.h` — serialize ring buffer to NVS on each push (`alarm_manager_save()`); restore on boot (`alarm_manager_load()`); add `alarm_ack(uint8_t id)` API; add `alarm_manager_get_unacked_count()` for header badge
- `main/ui/` — alarm unread count badge in header bar (top-right indicator); optional alarm detail bottom-sheet triggered by tapping the badge

**Acceptance criteria:**
- Alarm history survives a reboot; count badge reflects unacknowledged alarms on next boot
- `alarm_ack(id)` marks alarm as acknowledged; badge count decrements
- NVS write completes within 50 ms (non-blocking to sensor task)

---

### ⬜ Phase 9 — Storage Framework (NVS Log + SD Card Export)
**Status: PLANNED**

**Goal:** Persist sensor records to NVS flash (short-term) and export to SD card CSV (long-term). Also persist `history_manager` ring buffer state so trend charts survive a reboot.

**Design:**
- NVS persistence: write 1-minute averaged records to NVS on a 5-minute flush cycle
- Record limit: 1440 records (24 h in NVS); oldest overwritten when full
- SD card: optional export button on Data Logs screen writes all records to `/sdcard/ivf_log_YYYYMMDD.csv`
- SD pins: SDMMC or SPI2 — confirm available GPIOs in `board.h`
- SNTP time sync: if WiFi available, sync RTC for accurate timestamps
- `history_manager` NVS persistence: save/restore hourly and daily ring buffers on boot/shutdown

**Files to modify:**
- `main/data/record_manager.c` — add NVS flush and restore-on-boot
- `main/data/history_manager.c` — add NVS persistence of ring buffer state
- `main/ui/screens/screen_logs.c` — add "Export CSV" button (visible only when SD card detected)

**Files to create:**
- `main/data/sd_export.c/.h` — SD card mount/unmount, CSV write

**Acceptance criteria:**
- After reboot, logs screen shows records from before the reboot
- After reboot, chart screen shows trend data from before the reboot
- Tapping Export writes valid CSV to SD card
- Records have correct timestamps (real time if SNTP synced, elapsed ms otherwise)

---

### ⬜ Phase 10 — Production Hardening
**Status: PLANNED**

**Goal:** Final production readiness — OTA firmware update, watchdog, error screens, display sleep, and memory audit.

**Items:**
- **OTA update**: ESP-IDF OTA over WiFi or BLE; version displayed on splash screen
- **Watchdog**: enable task watchdog for LVGL task and sensor task; proper feed calls
- **Display sleep**: auto-dim after 5 min idle (LEDC PWM step-down); wake on touch
- **Error screen**: dedicated full-screen error view for critical failures (sensor permanently lost, NVS corrupt, OOM)
- **Memory audit**: `heap_caps_print_heap_info()` on boot; assert if draw buffer malloc fails
- **Startup self-test**: sensor communication check; touch corner verification on first boot
- **Splash version**: display firmware version string from `IDF_VER` and `APP_VERSION`
- **Code review**: remove all `ESP_LOGD` in production build; set `CONFIG_LOG_DEFAULT_LEVEL_WARN`

**Acceptance criteria:**
- OTA flash completes without power cycle
- Device recovers from 72-hour continuous soak test without panic or memory leak
- Display dims after 5 min and brightens on touch
- All production configs set in `sdkconfig.defaults`

---

## File Inventory (Current State)

### Hardware / LVGL Port
| File | Status | Notes |
|------|--------|-------|
| `main/display/display_driver.c/.h` | ✅ Phase 2.1 complete | ST7262 RGB565, PSRAM fb, SWAP_XY+MIRROR_Y hardware rotation |
| `main/touch/touch_driver.c/.h` | ✅ Phase 2.1 complete | map_x direct, map_y inverted — correct for left-edge-up. `*x` = portrait_Y, `*y` = portrait_X (by design) |
| `main/lvgl_port/lvgl_port.c/.h` | ✅ Phase 4A fix applied | `LV_DISP_ROT_NONE`, full-frame PSRAM draw buffer. Phase 4A: x/y swap in `lvgl_touch_read_cb` so LVGL receives portrait_X in `point.x`, portrait_Y in `point.y` |
| `main/board/board.h` | ✅ Phase 1 complete | Central GPIO pin map |

### UI Framework
| File | Status | Notes |
|------|--------|-------|
| `main/ui/ui.h` | ✅ Phase 4B complete | `IVF_HEADER_H=50`, `IVF_CONTENT_H=430`, `IVF_DRAWER_W`, `IVF_NAV_BTN_SIZE`; tab bar constants removed |
| `main/ui/ui.c` | ✅ Phase 4.2.7 updated | LVGL timer `dash_timer_cb` (1 Hz, replaces `ui_refresh_task`); `ui_dashboard_refresh()` removed; `navigation_drawer_t` integration; `"TVOC Chart"` / `"Data Logs"` labels; `create_fab=false` |
| `main/ui/nav_drawer.h` | ✅ Phase 4B complete | Legacy nav drawer API — retained as header only; `nav_drawer.c` removed from build |
| `main/ui/nav_drawer.c` | ⛔ Phase 4.2.5 removed from build | Commented out in `CMakeLists.txt` — superseded by `navigation_drawer.c` (Phase 4.2.2) |
| `main/ui/assets/assets.h` | ✅ Phase 4.2.6 updated | Drawn icon API: leaf, wifi, sd_card, thermometer, humidity, clock, chart, **shield** |
| `main/ui/assets/assets.c` | ✅ Phase 4.2.6 updated | `assets_draw_humidity()` updated (16×22 teardrop); `assets_draw_shield()` added (28×32 geometric primitive) |
| `main/ui/components/status_badge/status_badge.h/.c` | ✅ Phase 4.1 complete | Pill badge GOOD/MODERATE/POOR/DANGER/ERROR + custom |
| `main/ui/components/icon_button/icon_button.h/.c` | ✅ Phase 4.1 complete | Circular FAB button with symbol, shadow, callback |
| `main/ui/components/card/card.h/.c` | ✅ Phase 4.1 complete | Card container; `card_get_obj()` for positioning, `card_get_content()` for widgets |
| `main/ui/components/circular_gauge/circular_gauge.h/.c` | ✅ Phase 4.2.4 updated | Progressive arc gauge; `circular_gauge_set_value_animated()`; font references fixed (TD-13 resolved) |
| `main/ui/components/voc_gauge/voc_gauge.h/.c` | ✅ Phase 4.2.5 updated | Product-specific TVOC gauge; 4-zone progressive arcs; badge thresholds; 500 ms animation; `VOC_GAUGE_NO_READING` sentinel; MODERATE badge uses dark text |
| `main/ui/components/header/header.h/.c` | ✅ Phase 4.2.6 updated | 272×50 header; WiFi far right (`HDR_WIFI_X=244`); SD icon removed; `HDR_TIME_ROFS=32`; `HDR_TITLE_MAX_X=156`; title font `IVF_FONT_NORMAL`; time/date right-aligned |
| `main/ui/components/navigation_drawer/navigation_drawer.h/.c` | ✅ Phase 4.2.6 updated | Full-screen height (480 px, y=0); `DRAWER_HEADER_H=148`; top section (circle+shield+badge+title+pill); `header_title/header_status/footer_version` cfg fields; version footer; `#include "assets.h"` |
| `main/ui/screens/screen_splash.c/.h` | ✅ Phase 2 complete | Portrait size fix |
| `main/ui/screens/screen_dashboard.c/.h` | ✅ Phase 4.2.6 complete · **FROZEN** | `header_t` + `card_t` + `voc_gauge_t`; title "DASHBOARD"; sparklines removed; `CARD_H=90`; `build_sensor_card()` simplified |
| `main/ui/screens/screen_chart.c/.h` | ✅ Phase 4A complete | Period selector, lv_chart, threshold lines, no-data label. Phase 4C (history_manager) and 4D (data binding) follow. |
| `main/ui/screens/screen_logs.c/.h` | ⬜ Stub | Full content Phase 5 (content height 430) |
| `main/ui/screens/screen_settings.c/.h` | ⬜ Stub | Full content Phase 6 (content height 430) |

### Data / Sensors
| File | Status | Notes |
|------|--------|-------|
| `main/sensors/sensor_backend.h` | ✅ Phase 3B complete | Backend interface: `sensor_backend_init()` + `sensor_backend_sample()` |
| `main/sensors/sensor_backend_sim.c` | ✅ Phase 3B complete | Sine-wave simulation — active backend, swap out in Phase 7 |
| `main/sensors/sensor_backend_hw.c` | ⬜ Phase 7 stub | Real ENS160+AHT21 — fill TODOs in Phase 7 |
| `main/sensors/sensor_manager.c/.h` | ✅ Phase 3B complete | Pure framework: task, mutex, NVS, public API — calls sensor_backend_* |
| `main/data/alarm_manager.c/.h` | ⬜ Pending simplify | Simplify in Phase 5 (remove ring buffer) |
| `main/data/history_manager.c/.h` | ❌ Not created | Create in Phase 4C |
| `main/data/record_manager.c/.h` | ❌ Not created | Create in Phase 5 |
| `main/data/config_manager.c/.h` | ❌ Not created | Create in Phase 6 |
| `main/sensors/ens160_driver.c/.h` | ❌ Not created | Create in Phase 7 |
| `main/sensors/aht21_driver.c/.h` | ❌ Not created | Create in Phase 7 |

### Application
| File | Status | Notes |
|------|--------|-------|
| `main/app_main.c` | ✅ Phase 4.2.7 updated | `ui_refresh_task` removed; `app_main` returns after `ui_init()` |

---

## Known Issues / Decisions Pending Hardware Test

1. ~~**Rotation direction**: RESOLVED in Phase 2.1.~~ Hardware rotation via `esp_lcd_panel_swap_xy` +
   `esp_lcd_panel_mirror` in `display_driver.c`. `LVGL_ROTATION = LV_DISP_ROT_NONE`. LVGL software
   rotation cannot work with `full_refresh=1` (LVGL 8.4.0 explicitly blocks it).

2. ~~**Touch x/y coordinate axis swap**: RESOLVED in Phase 4A.~~ `touch_driver_read()` returns portrait_Y
   in `*x` and portrait_X in `*y` by design (driver was written for LVGL ROT_270; project uses ROT_NONE).
   `lvgl_touch_read_cb` in `lvgl_port.c` now swaps them: `point.x = y` (portrait_X 0–271), `point.y = x`
   (portrait_Y 0–479). Tab-bar taps (portrait Y ≥ 430) were previously rejected by LVGL hit-testing
   because they exceeded the 0–271 x-range. Navigation was non-functional before this fix.

3. **Touch calibration**: Raw ADC min/max values (`TOUCH_RAW_X_MIN=200`, `TOUCH_RAW_X_MAX=4000`, `TOUCH_RAW_Y_MIN=200`, `TOUCH_RAW_Y_MAX=3600`) are initial estimates. Fine-tune after Phase 2 flash by tapping all four corners and logging raw values.

4. **sdkconfig widget verification:**
   - `CONFIG_LV_USE_ARC=y` ✅ confirmed — arc gauge builds and runs (Phase 3A)
   - `CONFIG_LV_USE_CHART=y` ✅ confirmed — sparklines build and run (Phase 3A)
   - `CONFIG_LV_USE_TABLE=y` ⬜ verify before Phase 5 — logs table
   - `CONFIG_LV_USE_SLIDER=y` ⬜ verify before Phase 6 — brightness slider

5. ~~**app_main.c tight coupling**~~: ✅ Resolved in Phase 4.2.7 — `ui_refresh_task` and `ui_dashboard_refresh()` removed. Dashboard refresh runs via LVGL timer `dash_timer_cb` inside `lv_timer_handler()`. TD-2 in ARCHITECTURE.md closed.

6. **ENS160 I2C address**: confirm ADDR pin wiring before Phase 7 (0x52 or 0x53).

---

## Build Record

| Date | Phase | Result | Binary size |
|------|-------|--------|-------------|
| 2026-06-17 | Phase 2 | ✅ 1373/1373, zero errors | 0x88A90 (553 KB, 47% free) |
| 2026-06-17 | Phase 2.1 rotation fix | ✅ 1373/1373, zero errors | 0x88D00 (553 KB, 47% free) |
| 2026-06-17 | Phase 3A (first build — 1 warning) | ✅ build OK, 1 warning (-Wunused-function `tvoc_to_angle`) | 0xAD980 (689 KB, 32% free) |
| 2026-06-18 | Phase 3A (corrected — 4 zones, no indicator arc) | ✅ 1373/1373, zero errors, zero warnings | 0xA84A0 (689 KB, 34% free) |
| 2026-06-18 | Phase 3A visual refinement + freeze (header layout, gauge shift, label abs coords, card resize) | ✅ zero errors, zero warnings | 0xA8500 (34% free) |
| 2026-06-19 | Phase 3B data binding + backend separation (sensor_backend_sim/hw, screen_dashboard_update) | ✅ zero errors, zero warnings | 0xA8500 (34% free) |
| 2026-06-19 | Phase 4A chart UI + touch axis fix + instant navigation (lvgl_port.c x/y swap, ui.c ANIM_NONE) | ✅ zero errors, zero warnings | — |
| 2026-06-27 | Phase 4B nav drawer (nav_drawer.c, progressive gauge, drawn icons, correct IVF_HEADER_H=50) | ✅ zero errors, zero warnings | nav_drawer.c ~5 KB added |
| 2026-06-27 | Phase 4.1 shared UI framework (7 components, assets.c, CMakeLists update) | ✅ zero errors, zero warnings | 14 new files added |
| 2026-06-27 | Phase 4.2.1 header extension (`header_enable_menu()`, leaf_cont, menu_btn_event_cb) | ✅ zero errors, zero warnings | header.h + header.c only; no new files |
| 2026-06-27 | Phase 4.2.2 navigation drawer wiring (drawer header, FAB guard, full active highlight, ui.c migration) | ⬜ not yet built — pending review | 5 files modified; no new files |
| 2026-06-27 | Phase 4.2.3 dashboard migration (header_t, card_t, assets_draw_*; create_fab=false; header.c font fix) | ⬜ not yet built — pending review | 3 files modified; no new files |
| 2026-06-27 | Phase 4.2.4 VOC gauge component (voc_gauge_t, badge thresholds, animation; circular_gauge.c TD-13 fix) | ⬜ not yet built — pending review | 2 files created; 3 files modified |
| 2026-06-28 | Phase 4.2.5 Dashboard Final Polish (header geometry, right-aligned time/date, title clip, humidity overlap, NO_READING init, MODERATE contrast, nav_drawer.c removed) | ⬜ not yet built — pending user review | 4 files modified; no files created |
| 2026-06-29 | Phase 4.2.6 Hardware Validation Polish (WiFi far right, SD removed, full-screen drawer, new top section, sparklines removed, DASHBOARD title, improved icons) | ⬜ not yet built — pending flash to device | 7 files modified; no files created |
| 2026-06-30 | Phase 4.2.7 UI Freeze Fix (`ui_refresh_task` removed, `dash_timer_cb` LVGL timer added, `ui_dashboard_refresh()` removed) | ⬜ pending build + flash | 3 files modified; no files created |

---

## Sensor Thresholds (Default, stored in NVS `ivf_cfg`)

| Parameter | Warning | Alarm |
|-----------|---------|-------|
| TVOC | 300 ppb | 500 ppb |
| Temperature | 26 °C | 28 °C |
| Humidity low | — | 35 % |
| Humidity high | 65 % | — |
