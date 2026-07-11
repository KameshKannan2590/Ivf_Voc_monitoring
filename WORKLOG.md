# IVF VOC Monitoring System — Work Log

**Device:** CrowPanel DIS06043H v2.1 (ESP32-S3 N4R2, 480×272 RGB565)  
**Stack:** ESP-IDF 5.3.1 · LVGL 8.4.0 (managed component)  
**UI orientation:** Portrait 272×480 (hardware rotation via RGB panel SWAP_XY + MIRROR_Y)  
**Last updated:** 2026-07-10 · **Phase 6.2 — SHT41 Hardware Bring-up & Live Sensor Verification complete, CONFIRMED on real hardware** (task-numbered "Phase 6.2"; distinct from the earlier Settings-Screen-Freeze "Phase 6.2" below) · Phase 6.1 — SHT41 Temperature & Humidity Sensor Integration complete (task-numbered "Phase 6.1"; distinct from the earlier Phase 6.1 below) · Phase 6.4 (Burger Width Tuning, Instant Drawer, Touch-Passthrough Fix) complete · Phase 6.3 (Navigation Drawer & Burger Button Responsiveness) complete · Settings screen FROZEN (Phase 6.2) · Phase 6.1 (Font Size, Brightness Floor, Light/Dark Theme) complete · Phase 6 (Settings Screen + Brightness/Timeout) complete · Logs screen FROZEN (Phase 5.9) · Phase 5.8 (Logs Screen) complete · Chart screen FROZEN · Phase 5.6 (Picker Simplification + Axis Label Fix) complete · Phase 5.5 (Real Calendar Date Picker) complete · Phase 5.4.1 (Real Bitmap Icons) complete · Phase 5.4 (Chart Mode Integration & History Binding) complete · Phase 5.3 (History Manager Backend) complete · Phase 5.2 (Chart Visual Polish) complete · Phase 5.1 (Chart UI Migration) complete · Phase 4.2.7 (UI Freeze Fix) complete · Dashboard FROZEN · UI freeze resolved

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

### ✅ Phase 5.1 — Chart UI Migration
**Status: COMPLETE**

**Goal:** Migrate `screen_chart.c` to the approved Figma design — UI/styling only. No architecture
change: `apply_period()`/`period_cb()` logic, `screen_chart_refresh()`, and the eventual
`history_manager` integration points are untouched.

**Changes made:**

1. **Header** — replaced `ui_build_header(s_scr, "TVOC HISTORY")` + inline `LV_SYMBOL_LIST` icon
   with the shared `header_t` component (same one Dashboard uses): `header_create()`,
   `header_set_title("CHART")`, `header_set_wifi_strength()`, `header_set_sd_status()`,
   `header_set_time()`, `header_set_date()`, `header_enable_menu()`. Pixel identical to Dashboard.
2. **Period selector** — same `lv_btnmatrix` + `period_cb()`, restyled as rounded pill buttons
   labelled `"90 Days" / "30 Days" / "07 Days"` (was `"7D"/"30D"/"90D"`), 90 Days active by
   default. `period_t` / `PERIOD_POINTS[]` reordered to match the new button order — the
   index-to-period mapping logic itself is unchanged. Active = blue fill + white text, inactive
   = white fill + dark text, `IVF_COLOR_BORDER` outline.
3. **Calendar button** — new non-clickable button next to the period selector using the new
   `assets_draw_calendar()` icon (no functionality yet, per spec).
4. **TVOC title + legend** — `"TVOC (ppb)"` font raised to `IVF_FONT_NORMAL` (same as header
   title). Legend rebuilt as colour dot + muted text: `"Daily Average"` / `"Max"`.
5. **Chart appearance** — white background, rounded border, light grid
   (`lv_chart_set_div_line_count(chart, 4, 6)`), small circular point markers. **Area fill under
   the average line only:** LVGL 8.4's `lv_chart` has no native per-series area fill for
   `LV_CHART_TYPE_LINE` (confirmed against the vendored `lv_chart.c` — `LV_PART_ITEMS` bg-opacity
   only affects bar charts). Implemented via a `chart_draw_part_cb()` hook on
   `LV_EVENT_DRAW_PART_BEGIN` / `LV_CHART_DRAW_PART_LINE_AND_POINT`: identifies the average
   series by `dsc->sub_part_ptr == s_ser_avg` and paints a translucent quad
   (`lv_draw_polygon()`) from each line segment down to the chart's bottom edge. The max series
   gets no such hook, so it renders as a clean line — matching Figma. `lv_chart` itself was not
   replaced or subclassed.
6. **Stat cards** — `make_stat_card()` rebuilt on the shared `card_t` component (adds a light
   drop shadow for free) instead of raw `lv_obj_create()`. `LV_SYMBOL_UP/DOWN/LIST` icons
   replaced with 4 new drawn icons. Removed a hardcoded `lv_color_hex(0x7B1FA2)` in favour of
   `IVF_COLOR_TEXT`.
7. **New drawn icons** (`assets.h/.c`, geometric primitives, no bitmaps, no CMakeLists.txt
   change): `assets_draw_calendar()`, `assets_draw_date_range()`, `assets_draw_chart_average()`,
   `assets_draw_chart_max()`, `assets_draw_chart_min()`.
8. **Illustrative sample data** — `SAMPLE_AVG[12]`/`SAMPLE_MAX[12]` static arrays baked into
   `screen_chart.c`, resampled to the active period's point count by `load_sample_series()`
   (called from `apply_period()`). Stat cards show static Figma values (`245`/`820`/`82`/`26`).
   This is UI-layer decoration only — no data module was added. See TD-14.

**Not implemented in this phase** (per spec): `history_manager`, CSV export, real sensor data,
RTC, NVS, chart animations/zoom/scroll/gestures.

**Files modified (2):**
- `main/ui/screens/screen_chart.c` — full visual rewrite
- `main/ui/assets/assets.h` / `assets.c` — 5 new icon functions

**No files created. No CMakeLists.txt change required.**

---

### ✅ Phase 5.2 — Chart Screen Visual Polish
**Status: COMPLETE**

**Goal:** Make the chart screen match the approved Figma as closely as possible. Pure visual
refinement — no architecture, data-model, `history_manager`, statistics-engine, or RTC changes.
The shared `header_t` component is **frozen** and was not touched.

**Visual improvements made:**

1. **Legend** — replaced colour-dot bullets with `assets_draw_chart_average()` /
   `assets_draw_chart_max()` icons; labels now `"Daily Average"` / `"Maximum"` (was `"Max"`).
   No `LV_SYMBOL_*`. Icons vertically centred against the `"TVOC (ppb)"` title row.
2. **Chart container spacing** — more room below the title (`CHART_Y` 72→84), slightly shorter
   chart (`CHART_H` 180→160), tighter Y-axis label gutter (`CHART_X` 36→32), named bottom gap
   before the stat cards (`CHART_BOTTOM_GAP=16`). Content still fits `IVF_CONTENT_H` (430) with
   ~22 px to spare.
3. **Chart style** — background changed white→`IVF_COLOR_NAV` (light grey, reused from the
   header palette — no new hardcoded colour); grid lines softened with `LV_OPA_60`; internal
   padding increased 4→6 px. Green average / orange max lines, rounded point markers, and the
   average-line area fill (added in Phase 5.1) are unchanged. `lv_chart` itself untouched.
4. **Chart axes** — Y-axis already matched the Dashboard gauge's scale-label font/colour
   (`IVF_FONT_SMALL` + `IVF_COLOR_TEXT_MUTED`, confirmed from Phase 5.1); tick marks shortened
   and minor sub-ticks removed for a cleaner look. X-axis relabelled from relative day-offsets
   to illustrative month names (`"Feb"`…`"Jul"`) via a static 6-entry lookup indexed by the
   tick's major index — a label-formatting change only, point-count/period logic untouched.
5. **Statistics cards** — `shadow` flipped `true`→`false` to exactly match Dashboard's sensor
   tiles (radius/border were already identical); value/unit spacing widened so units read as
   `"245  ppb"` instead of `"245ppb"`.
6. **Card icons** — all four card icons unified to one 16×16 size (previously 16 for three,
   18 for the date-range icon); `assets_draw_calendar()` / `assets_draw_date_range()` redrawn
   at 16×16 to match. Right-aligned within each card, same as before.
7. **Spacing/consistency audit** — screen margin (8 px), inter-card gap (8 px), and all
   typography/colour tokens reuse existing `ui.h` constants; no new hardcoded values.

**Known limitations:**
- X-axis month labels are static illustrative placeholders, not real dates, and don't vary by
  the selected period (7D/30D/90D) — see TD-15. Real labels arrive with Phase 4D/5.3.
- `lv_chart` 8.4 always draws Y-axis labels in an external gutter to the left of the chart's
  own bordered box (confirmed against the vendored source) — a single full-width card with
  numbers "inside" it, as in the Figma mock, isn't achievable without a custom draw hook, which
  was out of scope for a styling-only phase. The gutter was tightened instead (36→32 px).
- Chart lines remain straight-segment (no spline/bezier) — `lv_chart` 8.4 has no built-in curve
  smoothing.
- Stat-card sample values (245/820/82/26) and the chart's sample series remain the Phase 5.1
  static illustrative data (TD-14) — no statistics engine exists yet.

**Files modified (2):**
- `main/ui/screens/screen_chart.c` — legend, chart spacing/background/grid/axis polish, card
  shadow + spacing, unified icon size
- `main/ui/assets/assets.h` / `assets.c` — `assets_draw_calendar()` / `assets_draw_date_range()`
  resized 18×18 → 16×16

**No files created. No CMakeLists.txt change required.**

**Next phase (completed below):** Phase 5.3 — History Manager Backend.

---

### ✅ Phase 5.3 — History Manager Backend
**Status: COMPLETE**

**Goal:** Backend architecture only — the historical-data storage layer that will eventually
drive Chart, Logs, statistics, and CSV export. No LVGL dependency, no UI changes. Dashboard
and Chart UI (Phase 5.2, frozen) were **not touched**. Simulated sensor data only; no RTC,
flash, WiFi, or real sensors; no statistics calculated; no CSV export.

**Pre-implementation architecture review (requested before coding):**
- Found that a separate `record_manager.c` was already planned (old Phase 5 Logs-screen note)
  for a "1-minute snapshot, 24h ring buffer" — a second store of the same sensor readings that
  `history_manager` is now explicitly chartered to own for *both* Chart and Logs. **Resolved
  by retiring `record_manager.c`** — the Logs screen will read `history_manager` instead once
  implemented (see the updated Phase 5 note below, including one open gap: `history_manager`
  today is hourly-resolution only, no per-minute buffer).
- Found that the existing sibling module `alarm_manager.h` depends directly on
  `sensor_manager.h`'s `sensor_data_t` type. **Avoided that coupling deliberately**:
  `history_manager.h` has zero dependency on `sensor_manager.h` or `alarm_manager.h` —
  `history_manager_add_sample()` takes plain `float` scalars. `sensor_manager.c` is the only
  file that includes `history_manager.h`; the dependency is one-directional.

**Design — single ring buffer, sliced by period (not three separate buffers):**
- One 2160-slot circular buffer of hourly-averaged records covers the full 90-day horizon.
  `HISTORY_PERIOD_7D`/`_30D`/`_90D` are just window sizes (168 / 720 / 2160) into that same
  buffer — asking for a longer period returns more of the one buffer, nothing is duplicated.
- Two lightweight accumulators sit in front of it: a single "latest reading" cache (for
  `history_manager_get_latest()`), and a running-sum accumulator for the *in-progress* hour
  (no individual per-minute records are stored — only sums — which is why the RAM footprint is
  dominated entirely by the 2160-slot buffer).
- `sensor_task()` (`sensor_manager.c`, 1 Hz) calls `history_manager_add_sample()` every 60th
  iteration (~once/minute), skipped on a sensor fault. Every 60 of those calls (~1 hour), the
  running sums are averaged into one record and pushed into the ring buffer, overwriting the
  oldest slot once full.
- All state is protected by a `SemaphoreHandle_t` mutex, same pattern as `sensor_manager.c` /
  `alarm_manager.c` (writer runs on the sensor task / Core 0; readers will run on the LVGL task
  / Core 1 once Phase 5.4 calls them).

**API (project's `module_verb_noun()` convention; 1:1 mapping to the requested `History_*`
names is documented in the header and in ARCHITECTURE.md):**
```c
esp_err_t history_manager_init(void);
void      history_manager_add_sample(float voc_ppb, float temperature_c, float humidity_pct);
uint16_t  history_manager_get_samples(history_period_t period, history_record_t *out, uint16_t max_count);
bool      history_manager_get_latest(history_record_t *out);
void      history_manager_clear(void);
uint16_t  history_manager_get_sample_count(history_period_t period);
uint16_t  history_manager_get_range(history_period_t period, uint32_t from_ts, uint32_t to_ts, history_record_t *out, uint16_t max_count);
```
`history_record_t` = `{ timestamp_s, voc_ppb, temperature_c, humidity_pct, alarm_state }` —
`alarm_state` is reserved (always 0; no `alarm_manager` dependency added). New fields belong at
the end of the struct. Timestamps are `esp_timer_get_time()/1e6` (boot-relative — same
convention as `alarm_manager.c` and `ui.c`'s `dash_timer_cb`) until an RTC/SNTP source exists.

**Memory estimate:**
`sizeof(history_record_t)` = 20 bytes (4-byte aligned: 3 floats + a uint32 = 16 B, + 1 B
`alarm_state` + 3 B padding). 2160 × 20 B = **43,200 B ≈ 42.2 KB**, allocated via
`heap_caps_malloc(MALLOC_CAP_SPIRAM)` with an internal-SRAM fallback (same pattern as
`lvgl_port.c`'s draw buffer). Accumulator + latest-cache + mutex overhead is negligible
(<150 B). Storing raw per-minute samples for 90 days instead would need ≈2.6 MB — larger than
the entire PSRAM budget — which is why only the hourly average is retained long-term.

**Data flow:** `sensor_backend_sim.c` → `sensor_task()` (1 Hz) → `alarm_manager_check()`
[unchanged] + `history_manager_add_sample()` [new, ~1/min] → hourly ring buffer → **no readers
yet** (Chart/Logs/statistics/CSV export all read through `history_manager_get_*()` starting
Phase 5.4+). See ARCHITECTURE.md's Phase 5.3 write-up for the full ASCII diagrams.

**Files created (2):**
- `main/data/history_manager.h` — public API, `history_record_t` / `history_period_t`
- `main/data/history_manager.c` — ring buffer, accumulator, mutex-protected implementation

**Files modified (3):**
- `main/sensors/sensor_manager.c` — `#include "data/history_manager.h"`; `sensor_task()` calls
  `history_manager_add_sample()` every 60th iteration (skipped on sensor fault)
- `main/app_main.c` — `#include "data/history_manager.h"`; `history_manager_init()` called
  after `alarm_manager_init()`, before `sensor_manager_init()`
- `main/CMakeLists.txt` — `"data/history_manager.c"` added to SRCS

**Dashboard and Chart UI confirmed untouched** — no changes to any file under `main/ui/`.

**Known limitations:**
- No screen reads `history_manager` yet (Phase 5.4 wires the chart).
- Hourly resolution only — no per-minute buffer beyond the single latest-reading cache; a
  future Logs screen needing true 1-minute rows will need a small addition here (TD-16), not a
  new standalone module.
- RAM-only (TD-8, unchanged) — reboot clears all history; NVS/SD persistence is Phase 9.
- Boot-relative timestamps (no RTC yet).
- `alarm_state` reserved, always 0 (Phase 8 wiring).

**Next phase:** Phase 5.4 — Chart Data Binding (`screen_chart_refresh()` reads
`history_manager`, replacing the Phase 5.1 static sample data).

---

### ✅ Phase 5.4 — Chart Mode Integration & History Binding
**Status: COMPLETE — architecture review below was approved, then implemented**

**Functional design change:** the Chart screen's 90D/30D/7D period selector is being removed.
Two modes replace it: **Last 7 Days** (default on open — one point/day, 7 points, stats from
those 7 days) and **Selected Day** (via the calendar icon — one point/hour for a single chosen
day, stats from that day only). Reviewed against Phase 5.3's `history_manager` and the current
`screen_chart.c` before writing any code, per request.

**Verdict on Phase 5.3's storage:** unchanged, still correct. The one 90-day hourly ring buffer
already covers both modes — 7 daily points come from grouping the most recent 168 hourly
records into 7×24 buckets **at query time**; the day view is just a 24-hour `get_range()`. No
new ring buffer, no duplicate storage.

**Two gaps found, independent of the mode change itself:**
1. `history_period_t` (7D/30D/90D) modeled the period-selector buttons directly — a UI concept
   baked into a module that's supposed to be UI-independent. **Removed**, replaced by plain
   `(from_ts, to_ts)` ranges.
2. The Phase 5.3 accumulator only ever tracked a running sum (for the average) — no min/max.
   A "Maximum" series or "Minimum" stat card has nothing real to plot once wired to live data.
   **Fixed by widening `history_record_t`** (adds `min_voc_ppb`/`max_voc_ppb`), not by adding
   storage — safe now since `screen_chart_refresh()` has zero real callers today.

**Recommended API (replaces the 3 names given for review — `Get Last 7 Days` / `Get Day
History` / `Get Statistics`):**
```c
uint16_t history_manager_get_range(uint32_t from_ts, uint32_t to_ts,
                                    history_record_t *out, uint16_t max_count);
uint16_t history_manager_get_daily_aggregates(uint32_t from_ts, uint32_t to_ts,
                                               history_record_t *out, uint16_t max_days);
uint16_t history_manager_get_count_in_range(uint32_t from_ts, uint32_t to_ts);
void     history_manager_compute_stats(const history_record_t *records, uint16_t count,
                                        float threshold_ppb, history_stats_t *out);
```
- "Get Last 7 Days" → generalized to `get_daily_aggregates(from_ts, to_ts, ...)` with no "7"
  baked in — Chart just calls it with a 7-day range; a future 30-day view reuses it as-is.
- "Get Day History" → not a separate function; it's `get_range()` bounded to 24 hours.
- "Get Statistics" → kept, but as **one** reducer, not two — hourly and daily records now
  share the same widened struct, so one function serves both modes.

Memory impact of the widened struct: 20B → 28B/record × 2160 ≈ **59 KB** (was ~42 KB) —
still trivial.

**Data flow:** `sensor_task()` → `history_manager_add_sample()` [unchanged] → hourly
accumulator (now with running min/max) → 90-day ring buffer → **either** `get_range()` (Mode 2)
**or** `get_daily_aggregates()` (Mode 1) → `history_manager_compute_stats()` (same reducer for
both) → `screen_chart_refresh()`.

**Mode switching:** a `chart_mode_t` state in `screen_chart.c`, reset to Last-7-Days every time
the screen opens. The calendar icon (currently decorative) becomes functional — taps open a
date picker (`lv_calendar` already enabled: `CONFIG_LV_USE_CALENDAR=y`); picking a day switches
to Selected-Day mode and calls `screen_chart_refresh()`, which branches on mode for the query,
X-axis labels, and 4th stat card.

**No-RTC limitation (flagged):** all timestamps remain boot-relative — there's no real
midnight to align day buckets to, and no real dates for the picker. `get_daily_aggregates()`
buckets by fixed 24h windows counting back from *now* (relative days), not wall-clock midnight,
until RTC/SNTP exists (Phase 7) — same caveat as TD-1/R-7.

**Statistics card:** the Days/Hours split is correct, kept as proposed. Recommend counting by
`max_voc_ppb` exceeding the threshold (peak-based, not average-based) for both modes. Flagged
(not changed): 150 ppb doesn't match the app's existing 300/500 ppb warn/alarm thresholds —
treated as an intentional third tier since it's been stated twice; suggest a named constant.

**Revision — mode-switch-back, dynamic title, and one new finding (confirmed after follow-up):**
- ✅ **"← Last 7 Days" chip** — agreed as the return mechanism (visible only in Selected-Day
  mode, immediate single-tap return). Placement refinement: anchor it in the vacated
  period-bar row rather than beside the title, since the title's length changes with the mode
  and a moving/crowded neighbour would make the tap target unstable.
- ✅ **Dynamic title** (`"TVOC Trend (Last 7 Days)"` / `"TVOC Trend (24 Jun 2026)"`) — agreed
  as the end-state format, with two caveats: (1) pre-RTC there's no real calendar date to show
  — render a relative label instead (`"TVOC Trend — 3 Days Ago"`); the same title code upgrades
  to real dates automatically once RTC/SNTP lands (Phase 7), no logic change needed. (2) Both
  title strings are longer than the current static label and won't fit sharing a row with the
  legend as today — give the title its own row (the vacated period-bar space allows this) and
  move the legend to a row beneath it.
- 🆕 **New finding:** the legend text `"Daily Average"` is wrong in Mode 2 (hourly, not daily)
  — generalize to `"Average"` in both modes. Title/legend/4th-card are now all mode-dependent;
  recommend one `apply_mode_labels()`-style update point rather than three scattered edits.
- ✅ Always reset to Last-7-Days on screen entry — confirmed, no sticky state.
- ⬜ 150 ppb threshold — no further input; proceeding as a named (non-configurable) constant.

**Files that will change once approved (none touched yet):** `main/data/history_manager.h/.c`,
`main/ui/screens/screen_chart.c`. No CMakeLists.txt change expected.

---

#### Implementation (approved design above, now built)

**Files modified (2), no files created, no CMakeLists.txt change:**
- `main/data/history_manager.h/.c` — removed `history_period_t`; added `get_range()` (no period
  param), `get_daily_aggregates()`, `get_count_in_range()`, `compute_stats()`,
  `VOC_WARNING_THRESHOLD_PPB`; widened `history_record_t` (`voc_ppb` → `avg_voc_ppb`, added
  `min_voc_ppb`/`max_voc_ppb`); accumulator now tracks running min/max per hour.
- `main/ui/screens/screen_chart.c` — removed the period selector (`period_bar`, `period_cb`,
  `apply_period`, `PERIOD_POINTS`, `period_t`, `SAMPLE_AVG`/`SAMPLE_MAX`/`MONTHS` dummy data);
  added `chart_mode_t` + `apply_chart_mode()` (the single UI update point), the "◀ Last 7 Days"
  chip, a day-picker overlay (`lv_list` of 30 relative-day rows), dynamic title, generalized
  legend text, and mode-aware 4th stat card.

**New/changed APIs:** see the "Modified/new APIs" table in ARCHITECTURE.md's Phase 5.4 section
— summary: `get_samples(period,...)`/`get_sample_count(period)`/`history_period_t` removed;
`get_range(from_ts,to_ts,...)` (period param dropped), `get_daily_aggregates()`,
`get_count_in_range()`, `compute_stats()`, `VOC_WARNING_THRESHOLD_PPB` added.

**Mode flow:** `screen_chart_create()` and `screen_chart_refresh()` both call
`apply_chart_mode()`. `screen_chart_refresh()` always passes `CHART_MODE_LAST_7_DAYS` (called by
`ui_goto_screen()` on every navigation to Chart — reset-on-entry, no new call site needed). The
calendar icon → day picker → `apply_chart_mode(CHART_MODE_SELECTED_DAY)`; the "◀ Last 7 Days"
chip (visible only in that mode) → `apply_chart_mode(CHART_MODE_LAST_7_DAYS)`. See
ARCHITECTURE.md for the full state diagram and updated architecture diagram.

**Memory impact:** `history_record_t` 20 B → 28 B (added 2 floats). Ring buffer 2160 × 28 B =
**≈59.1 KB** (was ~42.2 KB at Phase 5.3), still PSRAM-allocated with SRAM fallback.
`get_daily_aggregates()` uses a ~900 B fixed local bucket array on the caller's stack during the
call only (LVGL task has an 8 KB stack — no concern).

**Bug caught during implementation:** `CONFIG_LV_SPRINTF_USE_FLOAT` is **off** in this project's
sdkconfig, so `lv_snprintf("%.0f", ...)` cannot format floats — would have silently mis-rendered
every stat-card value and the 4th card's threshold title. Fixed by using standard `snprintf()`
(`<stdio.h>`) for all float formatting, matching `screen_dashboard.c`'s existing convention.
Purely textual/integer formatting still uses `lv_snprintf()`.

**Known limitations:**
- No-RTC: day/hour bucketing and the day picker are relative-offset based, not wall-clock
  calendar-aligned (TD-17). `format_relative_day_label()` and the bucketing math in
  `get_daily_aggregates()` are the only two places that change once RTC/SNTP lands (Phase 7).
- Cold-start / soak-test verification not exercised end-to-end — the simulated backend runs in
  real wall-clock time, so a fully populated 7-day view needs 7 real days of uptime to observe
  (TD-18). Empty/partial states verified by code inspection (`"--"` cards, `LV_CHART_POINT_NONE`).
- Day picker is a flat relative list (30 rows), not a calendar grid — consistent with no-RTC.
- 150 ppb threshold remains fixed/non-configurable, per the review (no further input given).
- `history_manager` is still hourly-resolution only (TD-16, unaffected by this phase).

**Confirmations:**
- Dashboard: untouched — no changes to any file under `main/ui/` other than `screen_chart.c`.
- History Manager remains the single source of truth — `screen_chart.c` reads exclusively
  through its public API; no new data manager was created; `record_manager.c` stays retired.

**Next phase:** Phase 5.5 — not yet scoped; awaiting review of this implementation.

---

### ✅ Phase 5.4.1 — Real Bitmap Icons for the Chart Screen
**Status: COMPLETE**

**Context:** 5 unused, unregistered files were found in `main/ui/assets/` during the Phase 5.3
review (`calendar_icon.c`, `chart_average_icon.c`, `chart_maximum_icon.c`,
`chart_minimum_icon.c`, `date_range_icon.c`) — real LVGL-converted bitmap icons matching the
names originally referenced in the Phase 5.1 spec, but never wired into the build. They
contained a compile-breaking bug inherited from the icon-conversion tool: hyphens in C
identifiers (e.g. `LV_ATTRIBUTE_IMG_LETS-ICONS_DATE-RANGE-FILL`, `carbon_chart-average`) —
invalid syntax. Flagged during Phase 5.3/5.4 but deliberately left untouched (both phases were
explicitly out of scope for Chart UI icon changes). This phase fixes and wires them in.

**Fix:** every hyphen in each file's identifiers (macro guard, array name, struct name, `.data`
reference) replaced with an underscore — confirmed via grep that hyphens appear nowhere else in
these files (pixel data is pure `0x..` hex), so no pixel data was touched, only identifiers:

| File | Fixed identifier | Size |
|---|---|---|
| `calendar_icon.c` | `lets_icons_date_range_fill` | 24×24 |
| `date_range_icon.c` | `ic_outline_date_range` | 16×16 |
| `chart_average_icon.c` | `carbon_chart_average` | 16×16 |
| `chart_maximum_icon.c` | `tdesign_chart_maximum` | 16×16 |
| `chart_minimum_icon.c` | `tdesign_chart_minimum` | 16×16 |

**Wiring:** all 5 added to `CMakeLists.txt` SRCS; 5 `LV_IMG_DECLARE()` entries added to
`assets.h`; the 5 `assets_draw_*()` functions in `assets.c` rewritten from hand-drawn
`make_icon_cont()`/`make_filled_rect()`/`lv_line` primitives to `lv_img_create()` +
`lv_img_set_src()` + `lv_obj_set_style_img_recolor(..., LV_OPA_COVER)` — the same pattern
already proven by `assets_draw_thermometer/_humidity/_sd_card` (confirmed same
`LV_IMG_CF_TRUE_COLOR_ALPHA` format). Function signatures unchanged, so `screen_chart.c` needed
only one change: the calendar bitmap is 24×24 (was a 16×16 hand-drawn icon), so its `cal_btn`
centering math was updated (`CAL_ICON_SIZE` constant added). The other 4 bitmaps are 16×16,
matching `STAT_ICON_SIZE` exactly — no other layout change needed.

**Files modified (5 pre-existing bitmap `.c` files fixed, no new files):**
- `main/ui/assets/{calendar,date_range,chart_average,chart_maximum,chart_minimum}_icon.c` —
  identifier fix only
- `main/ui/assets/assets.h` — 5 new `LV_IMG_DECLARE()`
- `main/ui/assets/assets.c` — 5 `assets_draw_*()` bodies switched to bitmaps
- `main/ui/screens/screen_chart.c` — `CAL_ICON_SIZE` added, calendar centering updated
- `main/CMakeLists.txt` — 5 new SRCS lines

**Known limitation:** not visually verified on hardware/simulator in this pass — no device or
simulator available in this environment. Flash-and-look recommended before full sign-off.

**Next phase:** Phase 5.5 — not yet scoped.

---

### ✅ Phase 5.5 — Real Calendar Date Picker
**Status: COMPLETE**

**Context:** the Phase 5.4 Selected-Day picker used an `lv_list` of relative rows ("Today",
"Yesterday", "2 Days Ago", …). User feedback: this doesn't give access to a real 90-day range
and can't express a date the user actually wants (a specific day, possibly in a different month
or year than today). Requirements given: real calendar date selection, bounded to exactly the
90 days `history_manager` retains, correct navigation across a year boundary (e.g. selecting a
day in Dec 2025 while "today" is in 2026), Selected-Day X-axis at fixed 4-hour ticks
(0/4/8/12/16/20/24), and Last-7-Days X-axis showing real calendar dates (e.g. "30 Jun … 6 Jul")
instead of relative labels. Y-axis (0/250/500/750/1000 ppb) explicitly unchanged.

**Design decision — no `<time.h>`:** no RTC/SNTP exists yet (Phase 7), so `history_manager`
timestamps are boot-relative. Rather than risk depending on `mktime()`/`gmtime()`/`timegm()`
newlib behaviour that could not be verified on this target in this environment (no toolchain
installed here), calendar arithmetic was implemented as dependency-free integer math — Howard
Hinnant's public-domain `days_from_civil()`/`civil_from_days()` — plus manual month-name tables
instead of `strftime()`. A fixed reference date (`CHART_REF_YEAR/MONTH/DAY` = 2026-07-06) is
anchored to boot time once, in `calendar_init_reference()`; only that one function's constants
need to change once real time exists.

**What changed in `screen_chart.c`:**
- Day picker: `lv_list` relative rows → real `lv_calendar` grid (`s_picker_calendar`) with
  hand-rolled prev/next month buttons (`s_picker_prev_btn`/`s_picker_next_btn` +
  `s_picker_month_lbl`), since LVGL's built-in `lv_calendar_header_arrow` has no min/max bound
  support. `refresh_picker_calendar()` disables every day cell outside
  `[s_cal_min, s_cal_today]` (today − 90 days) via `LV_BTNMATRIX_CTRL_DISABLED`, and disables
  the prev/next buttons at the boundary month — verified against LVGL's own internal button-index
  formula (`first_dow + (day-1) + 7`) read from the vendored `lv_calendar.c` source, so the
  disabled cells line up exactly with the grid LVGL draws.
- Day selection: `on_picker_day_clicked()` on `LV_EVENT_VALUE_CHANGED` (bubbles up from the
  internal button matrix, which has `LV_OBJ_FLAG_EVENT_BUBBLE` set) reads
  `lv_calendar_get_pressed_date()`, stores a real `lv_calendar_date_t s_selected_date`
  (replacing the old relative `s_selected_day_offset`), and calls `apply_chart_mode()`.
- Title: Selected-Day mode now shows "TVOC Trend - 06 Jul 2026" instead of "3 Days Ago".
- X-axis (Selected Day): 5 uneven ticks (`0h/6h/12h/18h/23h`) → 7 fixed 4-hour ticks
  (`0,4,8,12,16,20,24`), per spec.
- X-axis (Last 7 Days): static relative labels (`-6d … Today`) → real per-record calendar dates
  (`s_day_axis_labels[7][8]`, "D Mon" format), computed from each `history_record_t.timestamp_s`
  independently, so a week spanning a month boundary (e.g. "30 Jun" → "6 Jul") labels correctly.

**Files modified (1, no files created):** `main/ui/screens/screen_chart.c` only. No changes to
`history_manager.h/.c` (calendar logic is entirely a Chart-UI concern) or the Dashboard.

**Known limitations:**
- Not visually verified on hardware/simulator — no device/simulator available in this
  environment, same caveat as every prior phase. Recommend a flash-and-look pass specifically
  exercising the Dec 2025 → Jan 2026 month-nav boundary and the 90-day min-date clamp.
- "Today" is a fixed reference date anchored to boot time, not a live RTC/wall-clock read —
  pre-existing no-RTC limitation (TD-17), not a new one; only `calendar_init_reference()` needs
  to change once RTC/SNTP exists.
- No compiler available in this environment to build-verify; correctness checked by hand-tracing
  the Hinnant calendar algorithm against its published reference and cross-checking LVGL
  calendar API usage against the vendored `lv_calendar.c`/`.h` source.

**Next phase:** not yet scoped — awaiting review of this implementation.

---

### ✅ Phase 5.6 — Picker Simplification + Axis Label Fix
**Status: COMPLETE**

**Context:** feedback on Phase 5.5: (1) simplify the calendar-grid picker back down to a plain
2-option dropdown ("Today" / "7 Days") for now — arbitrary-date selection isn't needed yet; (2)
neither chart axis was showing any tick labels at all.

**Picker simplification:** removed `s_picker_calendar` and everything that supported it
(`s_picker_prev_btn`/`s_picker_next_btn`/`s_picker_month_lbl`, `refresh_picker_calendar()`,
`on_picker_month_nav()`, `on_picker_day_clicked()`, `open_day_picker()`, plus the
calendar-grid-only math helpers `calendar_is_leap_year/_days_in_month/_day_of_week/_date_serial`
— none of these have any other caller). The overlay now shows 2 rows, "Today" and "7 Days";
tapping either hides it and calls `apply_chart_mode()` directly. `chart_mode_t`'s second value
is renamed `CHART_MODE_SELECTED_DAY` → `CHART_MODE_TODAY`: it always means "today" (recomputed
live each call), not a stored arbitrary date, so `s_selected_date`/`s_cal_today`/`s_cal_min` are
gone. Title for this mode is now the literal "TVOC Trend - Today". The Phase 5.5 dependency-free
calendar math (`days_from_civil`/`civil_from_days`/`calendar_init_reference`/
`boot_ts_to_calendar_date`/`calendar_date_to_boot_ts`) is kept — it still drives the Last-7-Days
real-calendar-date X-axis labels and Today's midnight-aligned day-boundary lookup.

**Axis label fix:** root cause was `lv_obj_set_style_clip_corner(s_chart, true, 0)`, added in
Phase 5.2 purely for the rounded-border look. LVGL draws chart tick labels *outside* the
object's own coordinate box (Y labels left of x=0, X labels below the bottom edge — this
screen's `CHART_X`/`CHART_BOTTOM_GAP` constants were sized to reserve exactly that space).
`clip_corner` applies a rounded-rect mask scoped to the object's own box to *all* of its
drawing, so anything rendered outside that box — both axes' labels — was being masked away
entirely, with no other visible side effect. Fix: removed the `clip_corner` call; the chart's
background/border are a plain filled rounded rect drawn fully inside its own box, so nothing
was actually relying on that clip for correctness.

**Files modified (1, no files created):** `main/ui/screens/screen_chart.c` only.

**Known limitations:**
- Not visually verified on hardware/simulator — no device/simulator in this environment.
  Recommend confirming the axis-label fix specifically with a flash-and-look pass, since it
  reverses an unverified Phase 5.2 assumption.
- Arbitrary-date selection is deferred, not lost — the calendar math it needs is already in
  place and in daily use by the Last-7-Days/Today logic.

**Next phase:** not yet scoped — awaiting review of this implementation.

**Follow-up (same day, #1):** title was still rendering on its own row below the calendar button
instead of alongside it. Swapped the two rows: the title now lives in Row A next to `cal_btn`
(vertically centered against its 34 px height, width capped via new `TITLE_W` so it stops short
of the button — no overlap), and the "◀ Last 7 Days" chip moved down into the row the title used
to occupy (still Today-mode-only, still reserved whether shown or not, so nothing shifts when
switching modes). Chart/stats cards shift down by ~20 px to fit; content height (430 px) still
has ~10 px to spare. `screen_chart.c` only.

**Follow-up (same day, #2):** the gap between the title and the "Average"/"Maximum" legend was
too large — it was sized to reserve a full 34 px chip row plus two 6 px gaps even though the
chip is empty text most of the time. Tightened it: chip shrunk to `CHIP_H` (28 px, was reusing
the 34 px title-row height) and the two inter-row gaps reduced from 6 px to 4 px, reclaiming
12 px total. That 12 px was added directly to `CHART_H` (150 → 162 px) rather than left as
empty space, per request — `STATS_Y` (and everything below it) is computed from
`CHART_Y + CHART_H + CHART_BOTTOM_GAP` and lands on the exact same pixel as before, so the stat
cards don't move. `screen_chart.c` only.

**Follow-up (same day, #3):** removed the "◀ Last 7 Days" return chip entirely — the calendar
dropdown already has a "7 Days" option that does the same thing, so the chip was a redundant
second way back to the default mode. `s_chip`, `on_chip_back_click()`, and the
mode-visibility toggle for it are gone; `chart_mode_t` no longer needs any UI element tied to
`CHART_MODE_TODAY` beyond the title text. The legend row moved up into the space the chip row
used to reserve (`LEGEND_Y` now sits directly below the title row, 18 px gap instead of the old
~46 px chip-reserved gap), and that reclaimed space was added to the chart (`CHART_H`:
162 → 180 px). `STATS_Y` still resolves to the same pixel as before. `screen_chart.c` only.

---

### ✅ Phase 5.7 — Chart Screen Freeze
**Status: COMPLETE**

The Chart screen (`screen_chart.c/.h`) is declared **FROZEN** — same standing as the Dashboard.
No further changes without explicit approval. Work moves on to the **Logs screen** (Phase 5 in
the roadmap, still `⬜ PLANNED`): a table view of `history_manager` records — date/time, TVOC,
temperature, humidity — with a record count, CSV export button, and "Load More" pagination.

**Files affected:** none — status declaration only.

---

### ✅ Phase 5.8 — Logs Screen
**Status: COMPLETE**

**Requirements (from the design mockup + explicit spec):** header unchanged (shared
`header_t`, same as every other screen); use `datalog_icon.c` for the "Total record" badge;
10 entries per page plus the table header; "Load More" at the bottom reveals the next
entries; total history capped at 90 days with oldest data automatically dropped as new data
arrives; no changes to Dashboard or the now-frozen Chart screen.

**Icon fix (same bug pattern as Phase 5.4.1):** `datalog_icon.c` — an unused, unregistered
file already sitting in `main/ui/assets/` — had the icon-conversion tool's hyphen bug *plus*
a worse one: its exported symbol was literally `icon-park-solid_log (1)` (hyphens **and** a
stray `(1)` duplicate-download suffix baked into the identifier — e.g.
`LV_ATTRIBUTE_IMG_ICON-PARK-SOLID_LOG (1)`), which doesn't compile. Renamed every occurrence
to `datalog_icon` / `datalog_icon_map` / `LV_ATTRIBUTE_IMG_DATALOG_ICON`. This is a
*different* file from the already-working `data_log_icon.c` (symbol `icon_park_solid_log`,
used only by the nav-drawer's "Data Logs" item) — same underlying icon, re-exported
separately, so both now compile as two independent bitmaps with no symbol collision. Wired
into `assets.h/.c` (`assets_draw_datalog_icon()`) and `CMakeLists.txt`.

**`history_manager` gained one new read API — `history_manager_get_latest_n(skip, count,
out)`:** returns the `count` most-recent records *newest-first*, skipping the newest `skip`
first. Unlike `get_range()` (which fills oldest-first and would require fetching/scanning the
whole 90-day buffer to find the tail), this walks backward from the ring buffer's write head —
O(count), not O(stored count). Built specifically for "10 at a time, newest on top, Load More
walks backward through history" pagination. No new storage — reads the exact same ring buffer
Chart already uses.

**New shared module — `data/calendar_util.c/.h`:** Logs needs to turn a boot-relative
`history_record_t.timestamp_s` into "24 May, 8:25 AM" for display, which requires the same
Hinnant calendar-conversion algorithm `screen_chart.c` already has — privately, as static
functions, written before Chart was frozen. Since Chart is now FROZEN (Phase 5.7) and
refactoring it to expose/share that logic would mean editing a frozen file, the algorithm was
extracted into a new, independent, LVGL-free module instead. **`screen_chart.c` was not
touched** — it keeps its own pre-existing private copy, and the resulting duplication between
the two is tracked as TD-20 (de-duplicate once Chart is unfrozen). Each screen calls its own
`_init_reference()` once (both happen within the same `ui_init()` call at boot, so both anchors
land within microseconds of each other — immaterial for a coarse RTC-placeholder date).

**Row resolution — hourly, not per-minute:** the design mockup's rows look like per-minute
readings, but `history_manager` only stores hourly aggregates (TD-16, previously open). Adding
a true per-minute 90-day tier was considered and rejected on the numbers alone: 90 days of
1-minute records is 129,600 entries — at roughly the same per-record size as the existing
hourly buffer, that's north of 2.5 MB, more than this board's total 2 MB of PSRAM. Each Logs
row is therefore one hourly `history_record_t` (avg TVOC/temp/humidity for that hour). TD-16
updated to record this as a settled, memory-driven decision rather than an open question.

**Pagination + rendering:**
- `screen_logs_create()`/`screen_logs_refresh()` both reset to `skip=0` and load page 1 fresh —
  same "always reset on screen entry" policy as Chart's default-mode reset. (`ui_goto_screen()`
  in `ui.c` already called `screen_logs_refresh()` on every navigation to Logs — that hook
  pre-dated this phase and needed no change.)
- Rows are appended to a **scrollable, flex-column** container (`s_rows_container`) — each
  `logs_make_row()` call just becomes the next flex item, stacking top-to-bottom automatically
  with no manual y-offset bookkeeping across repeated "Load More" clicks. (Caught in review: an
  early draft positioned rows with plain `lv_obj_create()` and no layout at all, which would
  have stacked every row at the same (0,0) — flex column fixes that.)
- "Load More" is capped at `LOGS_MAX_LOADED_ROWS` = 100 (10 pages) as an embedded-safety guard
  against unbounded LVGL object growth if someone clicked through the full 90-day/2160-record
  history — tracked as TD-22, not expected to matter in practice.
- Each row's status dot is green/orange based on `avg_voc_ppb` vs. the shared
  `VOC_WARNING_THRESHOLD_PPB` constant (same one Chart's 4th stat card uses) — a small,
  no-new-dependency enhancement over a plain always-green dot.

**Export CSV — placeholder only, matching prior direction:** the design shows an "Export CSV"
button; this project has no SD/flash write path yet (`data/sd_export.c/.h` is still
`⬜ PLANNED`, Phase 9), and CSV export was explicitly called out as out-of-scope back in Phase
5.3's spec. The button is visually present and wired to a click handler that only logs a debug
message — tracked as TD-21.

**"90 days, oldest dropped" requirement:** already satisfied with zero changes — this is
exactly what `history_manager`'s fixed-capacity ring buffer (`HISTORY_HOURLY_CAPACITY = 2160`)
has done since Phase 5.3. Nothing needed here beyond confirming it.

**Files modified:**
- `main/ui/screens/screen_logs.c` — full rewrite (was a stub)
- `main/data/history_manager.h/.c` — new `history_manager_get_latest_n()`
- `main/ui/assets/datalog_icon.c` — hyphen + `(1)`-suffix identifier fix
- `main/ui/assets/assets.h/.c` — new `assets_draw_datalog_icon()` + `LV_IMG_DECLARE`
- `main/CMakeLists.txt` — 2 new SRCS lines (`datalog_icon.c`, `data/calendar_util.c`)

**Files created:**
- `main/data/calendar_util.h/.c` — shared boot-ts → calendar-date/time conversion

**Known limitations:**
- Not visually verified on hardware/simulator — no device/simulator available in this
  environment, same caveat as every prior phase.
- Logs rows are hourly averages, not raw per-minute readings (TD-16) — a hard memory
  constraint, not an oversight; see above.
- Export CSV is a placeholder (TD-21); "Load More" is capped at 100 rows (TD-22); Chart's and
  Logs' calendar math are two independent copies pending Chart being unfrozen (TD-20).
- Uses the same fixed-reference-date placeholder as Chart pending real RTC/SNTP (Phase 7).

**Next phase:** not yet scoped — awaiting review of this implementation.

**Follow-up (same day):** table headers were overlapping — the four header labels
(`COL_TVOC_X=130` etc.) were positioned at hand-guessed fixed x-offsets that didn't match
their actual rendered width. Fixed properly instead of re-guessing: header labels now use
`lv_obj_align_to(label, prev_label, LV_ALIGN_OUT_RIGHT_MID, gap, 0)`, positioning each one
relative to the *actual* measured width of the previous label — physically cannot overlap,
regardless of font or text length. Gaps match the Figma spec exactly: DATE&TIME→TVOC = 21.5 px
(rounded to 22, `lv_coord_t` has no fractional pixels), TVOC→TEMP = 6 px, TEMP→HUM = 8 px.
Header font dropped from `IVF_FONT_SMALL` (12 px) to a new `IVF_FONT_TINY` (`lv_font_montserrat_10`,
already enabled in sdkconfig) — added to `ui.h` as a purely additive macro, so Dashboard/Chart
(which don't reference it) are unaffected despite the shared header touching `ui.h`. The three
resolved header x-positions (`s_col_tvoc_x/temp_x/hum_x`) are captured once via `lv_obj_get_x()`
right after each `align_to()` call and reused for every data row's values, so columns still
line up top-to-bottom even though the layout is now computed, not guessed. `screen_logs.c`,
`ui/ui.h` only.

**Follow-up (same day, #2):** two more issues — (1) the date/time text's left margin needed to
be exactly 26.5 px from the screen's left edge (Figma spec), not the ~20 px it landed at;
`COL_DATE_X` recomputed to 19 (`26.5 - MARGIN(8) = 18.5`, rounded) to hit that. (2) The HUM
column's "%" was being clipped — increasing the left margin per (1) pushes every downstream
column further right, and the header text ("TVOC (ppb)", "TEMP (C)", "HUM (%)") was already
wide enough that the total no longer fit inside the 256 px table width once shifted. First pass
dropped the units entirely ("TVOC"/"TEMP"/"HUM") to buy width back.

**Follow-up (same day, #3):** units must stay in the header per explicit request — reverted
to unit-bearing labels, but tightened them (no space before the parenthesis: "TVOC(ppb)",
"TEMP(C)", "HUM(%)") to claw back just enough width to still fit. By rough character-width
estimate this clears the 256 px table width by only ~3 px — a near-exact fit, not a
comfortable one, and this project has no way to render/measure actual glyph widths outside
real hardware. The defensive `ESP_LOGW` check added in follow-up #2 (fires if HUM's computed
right edge still exceeds the table width) is the thing to watch in the log on first flash;
if it fires, the next lever to pull is shortening "DATE & TIME" (untouched so far, and the
widest of the four header strings) rather than touching the units again. `screen_logs.c` only.

---

### ✅ Phase 5.9 — Logs Screen Freeze
**Status: COMPLETE**

The Logs screen (`screen_logs.c/.h`) is declared **FROZEN** — same standing as Dashboard and
Chart. No further changes without explicit approval.

**What's done:** header (shared `header_t`, unchanged), top bar (datalog icon + live total
record count + Export CSV placeholder), table (header row + scrollable rows, columns computed
from real label widths via `lv_obj_align_to`/`lv_obj_get_x` rather than guessed offsets),
"Load More" pagination via `history_manager_get_latest_n()`, status dots, 90-day/oldest-dropped
retention (free — already how `history_manager`'s ring buffer works), and three rounds of
layout fixes (overlap, left margin, right-edge clipping) all closed out.

**What's pending — open questions, not bugs:** these were deliberately left unanswered rather
than guessed at, because the answer depends on requirements this project doesn't have yet:
- **Live refresh while the screen is open.** Today, `screen_logs_refresh()` only runs when the
  user navigates *to* Logs — nothing updates the list while they're already looking at it. A
  new hourly record appearing mid-session won't show until they leave and come back. Whether
  Logs needs a live/periodic tick (and at what cadence, and whether it should preserve scroll
  position) is unanswered. Tracked as TD-23.
- **What should produce a log row.** Right now it's purely the hourly cadence baked into
  `history_manager` (TD-16) — there's no concept of an event-driven row, e.g. logging
  immediately when VOC crosses the alarm threshold. `history_record_t.alarm_state` exists as a
  field already but is hardcoded to 0 everywhere (reserved for Phase 8). Tracked as TD-24.
- **When real data starts.** The active sensor backend is still `sensor_backend_sim.c`
  (simulated) — every row shown today is simulated, not measured, and will stay that way until
  Phase 7 (`sensor_backend_hw.c`) lands. The Logs screen itself needs no changes when that
  happens (it only ever reads `history_manager`), but "the screen works" ≠ "the numbers are
  real" until then.
- **Whether 90-day/hourly retention is the right answer long-term**, e.g. if a future
  requirement demands regulatory-grade record-keeping with different retention/resolution
  rules — flagged (TD-16), not decided.

**Files affected:** none — status declaration + doc updates only.

**Next phase:** not yet scoped.

---

### ✅ Phase 6 — Settings Screen (Brightness, Timeout, Thresholds)
**Status: COMPLETE**

**Requirements (from the design mockup + explicit spec):** thresholds become a
`{0, 250, 500, 750, 1000}` dropdown instead of a stepper/text field; brightness becomes a
real slider that controls actual display brightness; a new screen-timeout field
(15 sec / 30 sec / 45 sec / 1 min / None) dims the backlight to 5% after inactivity; an
active alarm/error suppresses the timeout entirely until acknowledged; touching a dimmed
screen restores the configured brightness.

**Investigated before writing anything** (this phase touches real hardware control, not
just UI): the display driver only ever had `display_set_backlight(bool)` — a plain GPIO
on/off, no dimming capability at all. Touch had no activity-tracking of its own, but
LVGL's touch indev was already correctly registered. `alarm_manager` already had a full
acknowledged/unacknowledged concept (`alarm_manager_active_count()`) — a ready-made hook
for the "don't dim during an alert" requirement. No `config_manager` existed; thresholds
were read via raw NVS calls directly in `sensor_manager.c`, write-only intent flagged in
the Settings stub's own leftover comment. No slider/dropdown/roller widget existed
anywhere in this codebase.

**Two things needed clarifying before implementing** (asked, both answered): (1) the
mockup's top-level "Threshold (ppb): 500" / "TVOC High Threshold: 1000" fields, versus the
"ALERT SETTINGS" section's "TVOC Alert Threshold: 150" / "High Alert Threshold: 500" —
resolved as: the top two are the Dashboard/Chart gauge display-range (max-scale etc.),
unrelated to alarms. (2) whether the touch that wakes a dimmed screen should also act on
whatever's underneath it, or only wake it — resolved as wake-only, consuming that touch
(phone-lock-screen pattern).

**A real pre-existing architecture gap, found and fixed:** `sensor_manager.c` and
`alarm_manager.c` each had their own, completely disconnected copy of "the VOC alarm
threshold" — `sensor_manager`'s NVS-loaded `s_voc_warn_ppb`/`s_voc_alarm_ppb` (used only
for Dashboard/Chart gauge color classification) and `alarm_manager`'s hardcoded
`#define VOC_ALARM_PPB 500.0f` (the thing that actually raises `ALARM_VOC_HIGH`). They
shared the same default value by coincidence, never by wiring. A Settings screen that only
updated `sensor_manager`'s copy would have looked like it worked (gauge colors would
change) while never actually moving the real alarm trigger point — a misleading
half-implementation. Fixed by replacing `alarm_manager`'s `#define` with a runtime
`s_voc_alarm_ppb`, sourced from the new `config_manager` at boot, with a new
`alarm_manager_reload_thresholds()` for live updates. "High Alert Threshold" (critical) now
updates both `sensor_manager` and `alarm_manager`; "TVOC Alert Threshold" (warning) updates
only `sensor_manager`, since `alarm_manager` has no independent warning-tier alarm type —
not adding one was a deliberate scope decision, not an oversight (see TD-25 for the
adjacent, still-unwired display-range settings, and TD-26 for temp/humidity thresholds
which got no live-reload path in this phase).

**New: `data/config_manager.c/.h`.** Single source of truth for every Settings value —
brightness, timeout, the two display-range values, and VOC warn/alarm — all in NVS
namespace `ivf_cfg` (reusing `sensor_manager`'s existing `"voc_warn"`/`"voc_alarm"` keys, not
inventing competing ones). Deliberately passive: it only persists values, it never calls
into `display_driver`/`sensor_manager`/`alarm_manager` itself — `screen_settings.c`
orchestrates calling both the setter and the relevant reload function, same layering as
every other screen → data-layer boundary in this project. One default value changed:
the warning threshold's fresh-install default is now 250, not 150 — 150 doesn't fall on
the `{0,250,500,750,1000}` dropdown this screen exposes. Anyone with an existing
`"voc_warn"=150` in NVS keeps it; only a clean/erased install sees 250.

**`display_driver.c/.h` — real brightness.** `display_set_backlight(bool)` removed
(confirmed via grep it had exactly one caller, inside `display_driver.c` itself) and
replaced with `display_set_brightness(uint8_t percent)`, backed by LEDC PWM on
`LCD_BL_GPIO` (5 kHz, 10-bit duty — well above flicker range, finer resolution than the
0-100% API needs).

**New: `display/display_power.c/.h`.** Owns the dim/wake/timeout state machine: a
`s_last_activity_ms`-style idle clock, `display_power_tick()` (dims to
`CONFIG_DIM_BRIGHTNESS_PCT`=5% once the configured timeout elapses, gated by
`alarm_manager_active_count()`), `display_power_wake()` (restores configured brightness,
resets the idle clock), and `display_power_reload_settings()` (Settings calls this after a
live brightness/timeout change so it applies immediately, not on next boot).
`ui.c` runs `display_power_tick()` off a new 500 ms `lv_timer` alongside the existing 1 Hz
dashboard-refresh timer.

**`lvgl_port.c` — wake-touch consumption.** The touch read callback now checks
`display_power_is_dimmed()` before reporting a press to LVGL. If dimmed, that touch calls
`display_power_wake()` and reports `LV_INDEV_STATE_RELEASED` for that read cycle instead of
the real point/pressed state — the press never reaches any widget underneath. Relying on
LVGL's own built-in `lv_disp_get_inactive_time()` was considered and rejected: since the
waking touch has to be reported as "released" to swallow it, LVGL's internal activity
timer would never register that touch as activity either, risking an almost-immediate
re-dim right after waking. `display_power`'s own idle clock, updated directly in the touch
callback regardless of whether the press is swallowed, avoids that edge case entirely.

**`screen_settings.c` — full rebuild** (was a header-only stub). Scrollable content
(more rows than fit in 430 px with Alert Settings expanded, its default state) —
the first screen in this project that scrolls its main content. One generic value-picker
overlay (title + N option rows, centered backdrop) reused for every dropdown-style field,
built the same way as Chart's "Today/7-Days" overlay rather than introducing `lv_dropdown`
— no dropdown/slider/roller component existed anywhere in this codebase before this phase,
and a same-pattern overlay keeps the visual language consistent with the rest of the app.
Caught in review before it shipped: two flex-column omissions (`rows_body` and
`s_alert_content` both needed `LV_FLEX_FLOW_COLUMN`, or every row created inside them would
have landed at (0,0) and overlapped — the exact bug already caught once in `screen_logs.c`
in an earlier phase, now checked for proactively).

**Theme row:** a display-only "Light  ›" placeholder in this initial pass, not clickable —
this project only had one theme at the time. Made real in Phase 6.1, see below.

**Files created:**
- `main/data/config_manager.h/.c`
- `main/display/display_power.h/.c`

**Files modified:**
- `main/ui/screens/screen_settings.c` — full rewrite
- `main/display/display_driver.h/.c` — `display_set_backlight` → `display_set_brightness` (LEDC)
- `main/lvgl_port/lvgl_port.c` — wake-touch consumption in the touch read callback
- `main/sensors/sensor_manager.h/.c` — VOC thresholds sourced from `config_manager`; new `sensor_manager_reload_thresholds()`
- `main/data/alarm_manager.h/.c` — `VOC_ALARM_PPB` define → runtime `s_voc_alarm_ppb`; new `alarm_manager_reload_thresholds()`
- `main/app_main.c` — `config_manager_init()` + `display_power_init()` added, correctly ordered
- `main/ui/ui.c` — new 500 ms `display_power_tick()` timer
- `main/CMakeLists.txt` — 2 new SRCS lines

**Known limitations:**
- Not flashed/verified on hardware — no device available in this environment. Flagged
  specifically for a flash-and-look pass: the LEDC PWM brightness curve and the touch-swallow
  wake behavior (does the first tap after waking correctly do nothing, and does a second
  tap correctly act?). See TD-27.
- "Threshold (ppb)" / "TVOC High Threshold" (display-range settings) are persisted but
  consumed by nothing — Dashboard and Chart are both frozen. See TD-25.
- Temperature/humidity thresholds got no Settings UI or live-reload path in this phase —
  still boot-only, still raw NVS reads. See TD-26.

**Next phase:** not yet scoped.

---

### ✅ Phase 6.1 — Font Size, Brightness Floor, Light/Dark Theme
**Status: COMPLETE**

Three follow-up refinements requested right after Phase 6 shipped.

**Uniform 12px body text.** Brightness, Theme, Screen Timeout, the four ppb-threshold rows,
and the picker overlay were a mix of `IVF_FONT_NORMAL` (16px) and `IVF_FONT_SMALL` (12px) —
every one of them now uses `IVF_FONT_SMALL`. The header is untouched (separate, shared
component). Mechanical change — every `IVF_FONT_NORMAL` in `screen_settings.c` was the
Settings-specific body text, so a file-wide replace was safe and correct here (verified by
reading the whole diff, not just trusting the search-and-replace).

**Brightness floor raised 5% → 15%** (`CONFIG_DIM_BRIGHTNESS_PCT`). Reasoning: at 5% the
backlight risked being dim enough that the user couldn't see where to tap to wake it up —
directly undermining the point of having a wake-on-touch mechanism at all. Enforced in three
places, not just the constant, so it can't be quietly bypassed: the auto-dim level itself,
the brightness slider's minimum (`lv_slider_set_range(s_slider_brightness,
CONFIG_DIM_BRIGHTNESS_PCT, 100)` — a manual setting can't go dimmer than the auto-dim level
either), and a clamp on whatever loads from NVS at boot (covers an old save from before this
floor existed).

**Light/Dark theme — the interesting part is applying it without touching frozen screens.**
Dashboard, Chart, and Logs are all FROZEN. A real theme toggle inherently means "every
screen's colors change" — seemingly a direct conflict. The resolution: every screen already
sets every color through the same eight `IVF_COLOR_*` macros from `ui.h`
(`BG/CARD/BORDER/TEXT/TEXT_MUTED/NAV/NAV_ACTIVE/NAV_INACTIVE`), called like
`lv_obj_set_style_bg_color(obj, IVF_COLOR_BG, 0)`. Those macros were literal
`lv_color_hex(...)` values before this phase; they're now function calls
(`ivf_color_bg()` etc., defined in `ui.c`, resolving a `s_dark_mode` flag loaded once at the
top of `ui_init()`). Since a macro expanding to a function call is used identically at every
existing call site, **every screen — including the three frozen ones — became theme-aware
with zero bytes changed in Dashboard/Chart/Logs' own source.** Confirmed safe before making
the change: grepped for any `static`/file-scope usage of these macros that would require a
compile-time constant (none found — every usage is inside a function body, where a
function-call initializer is completely legal C). Semantic colors
(`IVF_COLOR_PRIMARY/GOOD/WARNING/DANGER`) stay literal, unchanged in both themes — brand/status
colors, not surface colors.

This is a real nuance worth being explicit about, not just a clean trick: the frozen files'
*source* is untouched, but their *rendered output* now depends on a global setting none of
them were written or reviewed with in mind. Tracked as TD-28 — specifically worth checking on
a flash-and-look pass whether dark mode actually looks acceptable on all three (contrast,
icon visibility, Chart's fixed-color series lines against a dark background).

**No live theme switching.** Colors are set once, when each screen is built, and every screen
is built once at boot. Making a switch instant would mean re-styling every already-built
widget on every screen — this codebase style each widget with one-shot
`lv_obj_set_style_*()` calls rather than reusable `lv_style_t` objects that could be bulk
target for a refresh, so "instant" would be a much larger refactor than this feature
warrants. Instead, picking a theme in Settings calls `config_manager_set_dark_mode()` then
immediately `esp_restart()` — the device comes back up already in the new theme. A deliberate
scope decision, not a bug.

Dark palette (Material-dark-style values, not derived from the light palette by a formula):
`BG #121212`, `CARD #1E1E1E`, `BORDER #333333`, `TEXT #ECECEC`, `TEXT_MUTED #9E9E9E`,
`NAV #1A1A1A`, `NAV_ACTIVE #0D3D73`, `NAV_INACTIVE #707070`.

**Files modified:**
- `main/ui/ui.h` — 8 color macros redefined as function-call macros
- `main/ui/ui.c` — 8 `ivf_color_*()` resolver functions; `s_dark_mode` loaded in `ui_init()`
- `main/data/config_manager.h/.c` — `dark_mode` field; brightness floor clamps (3 places)
- `main/ui/screens/screen_settings.c` — real Theme row (was a placeholder); font-size sweep

**Known limitations:**
- Not flashed/verified — dark-mode contrast/legibility across all screens, and the actual
  15%-brightness visual floor, both need a real device to confirm. See TD-27, TD-28.

**Follow-up (same day):** the four simple nav rows (Theme, Screen Timeout, Threshold (ppb),
TVOC High Threshold) were shrunk from `ROW_H`=44px to a new `NAV_ROW_H`=18px each — reclaiming
104px (4 × 26px) specifically so the screen fits inside the 430px content area without
scrolling, even with Alert Settings expanded (its default state). Split the old single
`ROW_H` constant in two rather than just changing its value, since `ROW_H` was also sizing the
"ALERT SETTINGS" collapsible header bar (bell icon + title + chevron) — shrinking that to 18px
too, un-asked, would have made it too cramped to render legibly. That one keeps its own
`ALERT_HEADER_H`=44px, unchanged. The Alert Settings threshold rows (`ALERT_ROW_H`=56, they
carry a two-line label+subtitle) were left alone too — not part of what was asked, and needed
for their extra line of text regardless. `screen_settings.c` only.

**Follow-up (same day, #2):** brightness slider's knob was rendering half-clipped. Root cause:
the slider's own bounding box was only 8px tall, but its round knob (`LV_PART_KNOB`) is bigger
than the track and doesn't take its size from that box — the knob's top/bottom overflowed the
slider's own bounds and got clipped by the auto-height card wrapping it, since auto-sizing
containers compute height from children's own declared boxes, not a child's visual overflow
(the same underlying class of bug as the Chart axis-label clipping fixed in Phase 5.6). Fixed
by growing the slider's box to 20px (comfortably containing the knob) and setting the knob's
size/shape explicitly (16px, `LV_RADIUS_CIRCLE`) instead of relying on the default theme.
Rechecked the total content height with this and the row-height change together: ~326px,
still well inside the 430px budget. `screen_settings.c` only.

**Follow-up (same day, #3): the `header_t` component — explicitly declared FROZEN back in
Phase 5.2 ("Do NOT modify the Header") — needed a one-line fix for dark theme.** The SD-card
icon's default "absent" state (every screen uses this — no SD hardware exists yet) recolored
the icon at only `LV_OPA_30` (30% opacity). `img_recolor_opa` blends the recolor color *with*
the bitmap's own native baked-in pixels (dark/near-black for this glyph), so at 30% the
original dark pixels dominated the result regardless of theme — it happened to read as a
plausible "faint grey" by accident against Light's white header, but was indistinguishable
from Dark's dark header background, i.e. the icon effectively vanished/looked wrong. Fixed by
raising it to `LV_OPA_COVER` (full) — at full opacity the theme-aware `IVF_COLOR_TEXT_MUTED`
completely replaces the bitmap's color instead of blending with it, correct in both themes.
`SD_STATUS_PRESENT`/`SD_STATUS_ERROR` were already at `LV_OPA_COVER` and needed no change.
This is a real, narrow exception to the header freeze — made because the user pointed at this
exact bug and asked for it directly, not a unilateral decision. `main/ui/components/header/
header.c` only, one `case` block.

**Next phase:** not yet scoped.

---

### ✅ Phase 6.2 — Settings Screen Freeze
**Status: COMPLETE**

The Settings screen (`screen_settings.c/.h`) is declared **FROZEN** — same standing as
Dashboard, Chart, and Logs. No further changes without explicit approval.

**What's done:** Brightness/Theme/Screen Timeout/Threshold (ppb)/TVOC High Threshold/TVOC
Alert Threshold/High Alert Threshold — all dropdowns through one shared value-picker overlay;
collapsible Alert Settings section; uniform 12px body text; fits inside 430px, no scrolling
needed at current row heights. Two things behind the UI are real, end-to-end, not cosmetic:
VOC warn/alarm changes actually reach both `sensor_manager` (gauge color classification) and
`alarm_manager` (the real critical-alarm trigger — this closed a pre-existing gap where the
two were never connected at all, see Phase 6) with live reload and no reboot; and brightness
is real LEDC PWM, not a placeholder. One narrow, explicitly-requested exception was made to
the Phase 5.2 header freeze (SD icon opacity, see above) — nothing else about Dashboard,
Chart, or Logs was touched.

**What's pending — open questions, not bugs, carried over from Phase 6/6.1 and not resolved
by freezing:**
- Display-range values ("Threshold (ppb)", "TVOC High Threshold") are persisted but consumed
  by nothing — their only plausible use is Dashboard's gauge / Chart's Y-axis, both frozen.
  See TD-25.
- Temperature/humidity thresholds got no Settings UI or live-reload path — still boot-only,
  still raw NVS reads, unlike VOC. See TD-26.
- **Nothing in Phase 6/6.1/6.2 has been flashed or visually verified on real hardware** — the
  LEDC brightness curve, wake-touch consumption, and dark-mode contrast/legibility across
  every screen all need a real device. This is the biggest open item before treating any of
  this as done rather than code-complete. See TD-27, TD-28.

**Files affected:** none — status declaration only.

**Next phase:** not yet scoped.

---

### ✅ Phase 6.3 — Navigation Drawer & Burger Button Responsiveness
**Status: COMPLETE**

**Reported:** the nav drawer feels laggy/slow when the burger icon is tapped, and the burger
button itself seems to need a harder press to register. Investigated before changing
anything, since "just remove the animation" and "just make the button bigger" are both
plausible-sounding guesses that could be wrong.

**Drawer animation:** confirmed a real `lv_anim_t`, 220ms, ease-out (`navigation_drawer.c`'s
`slide()`) — not an unusual duration by itself (Material Design drawers commonly use
~250ms), but this display does a **full-screen redraw every animation frame**
(`lvgl_port.c`: `full_refresh=1`, full 480×272 PSRAM framebuffer via
`esp_lcd_panel_draw_bitmap`), so the actual per-frame cost is display-throughput-bound —
220ms nominal can visually stutter rather than glide. Cut to 120ms rather than removing the
animation outright (user's choice between the two).

**Burger button — a real, separate bug, not a touch-hardware issue.** Checked three possible
causes before concluding: (1) the touch driver's pressure/Z threshold
(`Z1_TOUCH_THRESHOLD=50` out of 4095) is negligibly low, not a "press harder" gate; (2) the
Phase 6 wake-touch-swallow logic (`lvgl_port.c`) is real but only fires after the screen has
auto-dimmed from idle — doesn't explain this on an already-awake screen; (3) `header.c`'s
`HDR_BTN_W` was `20` pixels — but the file's **own header comment**, present since this
component was written, has always documented "burger btn (44 px)" and "title starts at
x=55". The code contradicted its own stated design. A 20px-wide tap target on a resistive
touchscreen, combined with LVGL's default 10px scroll-tolerance (a touch that drifts more
than 10px before release is treated as a drag, not a click, and silently drops the tap), is
a much better fit for "needs a harder/more precise press" than any pressure-threshold theory.
Restored `HDR_BTN_W` to the documented 44.

**Trade-off, disclosed up front:** widening the button pushes the title's start position from
x=31 to x=55, shrinking its available width from 101px to 77px
(`HDR_TITLE_MAX_X - HDR_TITLE_X`, `LV_LABEL_LONG_CLIP` mode — truncates silently, no
ellipsis). "DASHBOARD" (9 characters) is the longest title used anywhere and may not fit at
77px by rough character-width estimate — not verifiable without hardware. Made the change
anyway per explicit instruction ("check for 44px, if it changes design we revert back") —
reverting is a single-constant edit (`HDR_BTN_W` back to `20`) if it's seen clipping on a
real device.

**This is the second narrow, explicitly-requested exception to the Phase 5.2 header
freeze** (the first was the SD-icon opacity fix, same day). `navigation_drawer.c` is not, and
has never been, declared frozen — that change needed no special dispensation.

**Files modified:**
- `main/ui/components/navigation_drawer/navigation_drawer.c` — animation 220ms → 120ms
- `main/ui/components/header/header.c` — `HDR_BTN_W` 20 → 44

**Known limitations:**
- Not flashed/verified. Specifically needs checking: does "DASHBOARD" clip at the new title
  width, and does 120ms actually feel smooth (vs. still-stuttery, given the full-redraw
  cost is unchanged — only the nominal duration dropped).

**Next phase:** not yet scoped.

---

### ✅ Phase 6.4 — Burger Width Tuning, Instant Drawer, Touch-Passthrough Fix
**Status: COMPLETE**

**Reported (on real hardware, after Phase 6.3):** three things. (1) "DASHBOARD" was clipping
its last letter — the predicted risk from Phase 6.3's `HDR_BTN_W` 20→44 change, disclosed at
the time in TD-29, now confirmed. (2) The 120ms drawer animation still read as slow — asked to
try instant appearance instead. (3) A new bug: while the drawer is open, the screen behind it
still responds to touch in the dimmed area — it shouldn't.

**Burger button width, 44→30:** confirmed clipping means 44px was too wide a trade against
title space. Settled on 30px — splits the difference: still 50% wider than the original 20px
(the actual "needs a harder press" root cause from Phase 6.3), while giving the title 91px
(`HDR_TITLE_MAX_X - HDR_TITLE_X`, up from 77px at 44px), enough for "DASHBOARD" (9 chars) to
render without `LV_LABEL_LONG_CLIP` truncating it. Single-constant change, `header.c`'s
`HDR_BTN_W`; its layout-derivation comment block and top-of-file diagram both updated to match.
Third narrow, explicitly-requested exception to the Phase 5.2 header freeze (after the SD-icon
opacity fix and the 20→44 width change).

**Instant drawer, no animation:** `navigation_drawer.c`'s `slide()` no longer runs an
`lv_anim_t` — it sets the drawer's x-position directly and shows/hides the backdrop in the
same call. The 120ms (and, before that, 220ms) animated version is kept in an `#if 0` block,
not deleted, along with its `drawer_anim_done_cb` ready-callback (which had done the
backdrop-hide-on-close work; that logic now lives inline in the new `slide()` since there's no
animation to attach a ready-callback to).

**Touch passthrough — fixed defensively, root cause not fully confirmable in this
environment.** The nav drawer's backdrop is a full-screen, clickable `lv_obj_create()` on
`lv_layer_top()`, and LVGL's own indev hit-testing is specifically designed to check
`lv_layer_top()` ahead of the active screen for exactly this "modal blocks what's behind it"
case — by that design, this should not have been possible. Without access to this project's
vendored LVGL 8.4.0 source in this environment to trace the actual dispatch order on this
build, the fix was made at a lower level that is correct regardless of the underlying cause:
`lvgl_touch_read_cb` (`lvgl_port.c`) now checks the raw touch point itself, before LVGL ever
sees it. If the drawer is open and the touch lands at `x >= IVF_DRAWER_W` (in the dimmed area,
outside the drawer's own 200px column), the callback closes the drawer directly
(`ui_nav_drawer_close_from_touch()`, new in `ui.c`/`ui.h`) and reports the touch as released —
so LVGL's own object dispatch never runs for that press, and nothing on the screen underneath
can react to it. Taps within the drawer's own column are unaffected and still go through
normal LVGL handling (nav items, drawer background).

**Files modified:**
- `main/ui/components/header/header.c` — `HDR_BTN_W` 44 → 30; comment blocks updated
- `main/ui/components/navigation_drawer/navigation_drawer.c` — animated `slide()` replaced
  with an instant version; old version kept `#if 0`'d
- `main/ui/ui.h` / `main/ui/ui.c` — added `ui_nav_drawer_is_open()` and
  `ui_nav_drawer_close_from_touch()`, thin wrappers around the existing
  `navigation_drawer_is_open()`/`navigation_drawer_close()` so `lvgl_port.c` (which owns no
  `navigation_drawer_t` handle) can reach drawer state
- `main/lvgl_port/lvgl_port.c` — `lvgl_touch_read_cb` gates touches outside the drawer's width
  while it's open, closing it directly instead of handing the press to LVGL

**Known limitations:**
- Not flashed/verified. Needs confirming: does "DASHBOARD" now render fully at 30px; does the
  instant drawer feel right (vs. jarring) compared to the animated version; does the
  touch-passthrough fix actually resolve the reported symptom, and does the same issue also
  occur for taps landing directly on the drawer's own 200px column (not covered by this fix —
  those still rely on LVGL's normal top-layer dispatch, which is the part whose behavior on
  this build couldn't be independently confirmed).
- The touch-passthrough root cause is not fully understood — the fix sidesteps it rather than
  explaining it. See TD-30.

**Next phase:** not yet scoped.

---

### ✅ Phase 6.1 — SHT41 Temperature & Humidity Sensor Integration
**Status: COMPLETE · Build verified (`idf.py build`, exit 0, zero warnings/errors)**

> Note on numbering: this phase's task title used "Phase 6.1", which collides with the earlier
> **Phase 6.1 — Font Size, Brightness Floor, Light/Dark Theme** entry above. They are unrelated,
> independently-scoped pieces of work; this entry is the hardware-integration one requested here.

**Goal:** Replace the simulated temperature and humidity readings with real values from a
Sensirion SHT41 (I2C) sensor, using the CrowPanel's existing (until now unused) I2C pins.
VOC stays simulated. No UI layout, styling, screen architecture, History Manager API, or Alarm
Manager API changes — the existing Dashboard/`sensor_manager` data flow picks up the real values
automatically.

**Design decisions:**
- **Official Sensirion SHT4x protocol, not a generic SHT4x library port** — soft reset (`0x94`),
  high-repeatability measurement (`0xFD`), 8.3 ms max conversion wait (10 ms used for margin),
  6-byte read, Sensirion CRC-8 (poly `0x31`, init `0xFF`) validated independently for both the
  temperature and humidity ticks, then the exact datasheet §4.6 conversion formulas. Heater
  commands intentionally not implemented (out of scope for this phase per the brief).
- **Driver owns its I2C bus, defensively.** `sht41.c` installs `I2C_NUM_0` itself
  (`i2c_bus_ensure_init()`) rather than requiring an `app_main.c`/`sensor_manager.c` bus-init
  call. `i2c_driver_install()` returning `ESP_ERR_INVALID_STATE` (bus already installed) is
  treated as success, not an error — this is what lets a future TVOC sensor driver share the
  same port without a refactor, directly satisfying the "modular for a future TVOC sensor"
  requirement without inventing a separate shared-bus module for a single current consumer.
- **Integration lives in `sensor_manager.c`, not a new backend file.** `sensor_backend_sample()`
  (still `sensor_backend_sim.c`) continues to produce all three simulated values every cycle;
  `sensor_task()` then calls `sht41_read()` and, on success, overwrites just
  `fresh.temperature_c`/`fresh.humidity_pct` with the real reading before the existing
  mutex-protected write to `s_data`. This was chosen over creating a third
  `sensor_backend_*.c` variant (a "VOC-sim + T/H-hardware hybrid" backend) because the task's
  data-flow diagram explicitly shows SHT41 feeding `sensor_manager` directly, and it keeps the
  existing VOC-only backend-swap mechanism (`sensor_backend_sim.c` ↔ `sensor_backend_hw.c`,
  Phase 7) completely untouched for when ENS160 is added later.
- **Error handling favors "keep the last good reading" over "blank the dashboard."** A static
  `s_temp_hum_valid` flag tracks whether any SHT41 read has ever succeeded. A failure *after* a
  prior success re-uses the previous cycle's `s_data.temperature_c`/`humidity_pct` (logged via
  `ESP_LOGW`) instead of blanking to `"--"` on a transient I2C glitch — the existing shared
  `sensor_ok` flag would otherwise blank VOC too, which a one-off SHT41 hiccup shouldn't do. A
  failure *before* any success sets `sensor_ok=false`, reusing the existing "no reading yet"
  path (same one the gauge already uses at boot) — no new state machine needed.
- The driver itself retries 3× internally (5 ms backoff) before `sensor_manager.c` ever sees a
  failure, so a real fault has to persist across ~3 consecutive I2C transactions before it
  surfaces as a fallback/log event.

**Files created (2):**
- `main/sensors/sht41/sht41.h` — `sht41_init()` / `sht41_read(&t, &h)`, no globals, no LVGL dependency
- `main/sensors/sht41/sht41.c` — Sensirion command set, CRC-8, unit conversion, internal retry/timeout

**Files modified (3):**
- `main/sensors/sensor_manager.c` — `#include "sht41/sht41.h"`; `sht41_init()` called from
  `sensor_manager_init()` (logs a warning, does not fail init, if the sensor doesn't respond);
  `sensor_task()` overrides `fresh.temperature_c`/`humidity_pct` with `sht41_read()`'s result,
  falling back to the last valid value (or `sensor_ok=false`) on failure; new static
  `s_temp_hum_valid` bool
- `main/CMakeLists.txt` — added `"sensors/sht41/sht41.c"` to `SRCS` (no new `INCLUDE_DIRS` entry
  needed — `sensors` is already on the include path, so `sensor_manager.c` includes it as
  `"sht41/sht41.h"`)
- `main/board/board.h` — updated the I2C section comment to reflect Phase 6.1 (SHT41) instead of
  the old "SGP30/SHT31, Phase 7" placeholder text; pin values (GPIO 17/18) unchanged

**Files deliberately NOT modified:** `sensor_backend.h`, `sensor_backend_sim.c`,
`sensor_backend_hw.c` (VOC backend-swap mechanism untouched), `screen_dashboard.c/.h`,
`history_manager.c/.h`, `alarm_manager.c/.h`, `app_main.c`, any nav drawer / chart / logs /
settings file.

**I2C pins:** SDA = GPIO 17, SCL = GPIO 18, `I2C_NUM_0`, 100 kHz standard mode — the pins already
reserved for sensors in `board.h` since Phase 1 (see TD-31: not confirmed against the physical
schematic/silkscreen in this environment; no device available). **I2C address:** `0x44` (SHT4x
default, ADR pin low).

**Sensor polling interval:** unchanged — the existing 1 Hz `sensor_task()` in `sensor_manager.c`.
No new FreeRTOS task (per the brief). `sht41_read()` adds ~11–15 ms of blocking I2C work
(10 ms conversion wait + a few ms of I2C transaction time) inside that same 1000 ms cycle.

**Build status:** `idf.py build` — **exit code 0, zero warnings, zero errors** (full log
inspected, not just the exit code). Verified with the actual ESP-IDF v5.3.1 toolchain installed
in this environment (`C:\Espressif\frameworks\esp-idf-v5.3.1`), not just by code review — this
is a real compiled-and-linked confirmation that `sht41.c` and the `sensor_manager.c` changes are
syntactically and semantically correct against the real ESP-IDF headers (`driver/i2c.h`, etc.),
though it was not flashed to a physical device (none available in this environment).

**Memory impact (before → after, `idf.py size`; before = this same tree with the Phase 6.1
diff stashed out):**

| Region | Before | After | Δ |
|--------|-------:|------:|---:|
| Flash `.text` (code) | 362,934 B | 371,854 B | +8,920 B |
| Flash `.rodata`+`.appdesc` | 258,528 B | 263,612 B | +5,084 B |
| Total flash image | 707,992 B | 727,288 B | +19,296 B |
| DIRAM used | 140,595 B (41.14%) | 145,903 B (42.69%) | +5,308 B (+1.55 pp) |
| — DIRAM `.bss` | 70,448 B | 70,464 B | +16 B (`s_i2c_ready`, `s_temp_hum_valid` statics) |
| — DIRAM `.text` | 57,735 B | 62,995 B | +5,260 B (SHT41 driver + legacy `driver/i2c.c` linked in for the first time in this project) |
| — DIRAM `.data` | 12,412 B | 12,444 B | +32 B |
| IRAM | 16,383 B (99.99%, pre-existing) | 16,383 B (99.99%) | 0 B |
| App partition free | 32% (0x53200 B) | 31% (0x4e6a0 B) | −22,368 B |

The IRAM increase is 0 — the I2C driver's ISR handler is not IRAM-resident in this
configuration, so it lands in the DIRAM `.text` growth instead. App partition remains at 31%
free (1 MB partition) — no flash-size concern.

**CPU impact:** `sensor_task()` (core 0, priority 3) now blocks for ~11–15 ms per 1000 ms cycle
on I2C I/O (under 2% of that task's budget) — a small increase over the sim-only baseline, and
entirely on core 0, so it cannot contend with the LVGL task (core 1) or its 5 ms tick. No new
task, no change to any existing task's priority or core affinity.

**Error handling strategy:** see "Design decisions" above — 3× internal driver retry, then
sensor_manager falls back to the last known-good reading (logged via `ESP_LOGW`) or, if no
reading has ever succeeded, `sensor_ok=false` (the pre-existing mechanism that blanks the
dashboard to `"--"` and gates `history_manager_add_sample()`). Nothing in the path can block
indefinitely (every I2C call has a bounded 50 ms timeout) or crash `sensor_manager_init()`
(a failed `sht41_init()` is logged, not fatal).

**Known limitations:**
- Not flashed to a physical device in this environment — I2C pin assignment (GPIO 17/18) and the
  0x44 address are carried over from this project's own prior documentation, not independently
  re-verified against the CrowPanel schematic. See TD-31.
- `sensor_manager.c`'s fallback read of `s_data.temperature_c`/`humidity_pct` on an SHT41 failure
  happens outside `s_mutex` — safe today because `sensor_task()` is `s_data`'s sole writer, but
  worth a second look if a second writer is ever introduced. See TD-32.
- The shared `sensor_ok` flag still covers all three channels at once (unchanged from the
  pre-existing struct design) — a *sustained* SHT41 outage before any successful reading would
  still blank the (still-simulated, still-working) VOC gauge too. This is an accepted limitation
  of the existing `sensor_data_t` design, not a new one introduced here, and only manifests
  before the very first successful SHT41 read.
- ENS160 warm-up handling, `SENSOR_LEVEL_WARMING`, and the VOC/TVOC hardware path remain
  entirely out of scope for this phase (Phase 7, unstarted).

**Next phase:** Phase 7 — VOC/TVOC via ENS160, sharing the I2C bus `sht41.c` already installs
(AHT21 no longer needed there — see the updated Phase 7 entry below).

---

### ✅ Phase 6.1 follow-up — I2C pin correction (GPIO_D, not UART1) + AUX GPIO removal
**Status: COMPLETE · Rebuilt, exit code 0, zero warnings/errors**

**Context:** the initial Phase 6.1 write-up above used GPIO 17/18 as an unverified placeholder
(inherited from the Phase 1 `board.h` comment). Reviewing Elecrow's own product page for this
board ([elecrow.com](https://www.elecrow.com/wiki/esp32-display-432727-intelligent-touch-screen-wi-fi26ble-480272-hmi-display.html))
surfaced the actual pin map for every expansion connector:

| Port | Pins | Notes |
|------|------|-------|
| GPIO_D (HY2.0-4P) | IO37, IO38 | Elecrow: "can be used to simulate UART or IIC" |
| UART1 (HY2.0-4P) | RX=IO18, TX=IO17 | Not used by this firmware (console is over USB-C / UART0) |
| SPK/I2S (PH2.0-2P) | CTRL=3V3, MCLK=IO19, BCLK=IO35, SDIN=IO20 | Unused |
| SD card (SPI) | MOSI=11, MISO=13, CLK=12, CS=10 | Unused (no SD driver yet) |

GPIO 17/18 is actually the **UART1** header, not a documented I2C port — it happened to work as a
placeholder only because it's electrically free (ESP32-S3's GPIO matrix routes I2C to any pin),
not because it's the pins Elecrow intends for I2C. **GPIO_D (IO37/IO38) is Elecrow's own
documented I2C-capable port**, so the driver was corrected to use it.

**Complication found during the correction:** GPIO 38 was already committed in this project as
`LCD_AUX_GPIO` — a pin `display_driver_init()` configures as output and drives HIGH once at
boot, carried over from the official CrowPanel ESP-IDF reference example (previously cross-
checked "Matches exactly" in this doc's compatibility table). Using GPIO 38 for I2C SCL would
have fought that assignment (I2C reassigns the pin to the peripheral's open-drain clock
function, which toggles — incompatible with "stays HIGH forever"). The user confirmed via their
own copy of the board schematic that GPIO 38 isn't tied to anything requiring a permanent HIGH on
this board, so the `gpio_config()`/`gpio_set_level(LCD_AUX_GPIO, 1)` block was removed from
`display_driver_init()` entirely, freeing the pin for I2C. This was a deliberate, confirmed
trade — not a guess — but it wasn't independently re-derived from a schematic in this session (no
schematic file was available to Claude directly); if display behavior regresses, this removal is
the first thing to revert (the prior block is in `git log`, not kept as dead/commented code).

**Changes made:**
- `main/board/board.h` — `BOARD_I2C_SDA`/`BOARD_I2C_SCL` changed `17`/`18` → `37`/`38`;
  `BOARD_LCD_AUX` (GPIO 38) removed; comments updated to cite the Elecrow source and the AUX
  removal rationale
- `main/sensors/sht41/sht41.c` — `SHT41_I2C_SDA_GPIO`/`SHT41_I2C_SCL_GPIO` changed `17`/`18` →
  `37`/`38`; header comment updated
- `main/display/display_driver.c` — removed the `LCD_AUX_GPIO` `gpio_config()`/
  `gpio_set_level()` block from `display_driver_init()` (5 lines)
- `main/display/display_driver.h` — removed the now-unused `LCD_AUX_GPIO` define

**Files deliberately NOT touched:** `sht41.h` (no pin constants there), `sensor_manager.c` (no
pin knowledge — it only calls `sht41_init()`/`sht41_read()`), any UI/screen file.

**Still open, not resolved by this change:**
- **SDA/SCL polarity is a convention, not a verified fact.** Elecrow's docs say IO37/IO38 support
  I2C but don't say which one is SDA vs. SCL — this project picked IO37=SDA, IO38=SCL. If the
  sensor doesn't ACK on first bring-up, swap the two `#define`s in `sht41.c` before assuming a
  deeper fault.
- **The GPIO 38 / AUX removal rests on the user's schematic review, not on independently tracing
  the net in this session** — worth a specific look on the flash-and-look pass, since it touches
  a previously "verified against the official example" claim. See TD-33.
- GPIO 17/18 (UART1) remain fully free and unused — available for a future UART1 use case, but
  no longer relevant to I2C.

**Build status:** `idf.py build` — exit code 0, zero warnings, zero errors (rebuilt after this
change, full log inspected). Total flash image essentially unchanged (727,288 B → 727,240 B,
−48 B from the removed AUX code — noise-level, not a meaningful memory delta).

---

### ✅ Phase 6.2 — SHT41 Hardware Bring-up & Live Sensor Verification
**Status: COMPLETE · Build verified · Physical bring-up CONFIRMED on real hardware — flashed, SHT41 communicating correctly, Dashboard showing live values, display/touch unaffected by the GPIO 38 (AUX) removal**

**Goal:** Prepare the SHT41 integration for physical bring-up — verify the Phase 6.1/6.1-follow-up
pin/address decisions still hold, add enough `ESP_LOGI()`/`ESP_LOGE()` tracing to diagnose the
sensor over the serial monitor stage-by-stage, and make sure a non-responding sensor fails
loudly (in the log) but gracefully (in the running app), per the brief. No Dashboard, Chart,
History Manager, Alarm Manager, or Navigation Drawer changes — confirmed by diff, this touched
only `main/sensors/sht41/sht41.c` and `main/sensors/sensor_manager.c`.

**1. Review of the Phase 6.1 implementation:** re-read `sht41.c`/`sensor_manager.c` end to end.
Found one real gap: `sht41_init()` unconditionally logged `"SHT41 initialized"` at the end even
when the soft-reset probe had just failed (i.e., even when the sensor was *not* actually
detected) — the log message contradicted reality. Fixed as part of this phase (see below); this
was the main reason a review pass mattered here, not just a formality.

**2. I²C pins verified:** SDA=GPIO 37, SCL=GPIO 38 — the "GPIO_D" expansion header, confirmed in
the Phase 6.1 follow-up against Elecrow's own board documentation and the user's schematic
(GPIO 38 was freed from the previous `LCD_AUX_GPIO` drive-HIGH requirement). Unchanged this
phase — this phase's job was to verify, not re-derive, that decision.

**3. I²C address verified:** `0x44` — SHT4x family default (ADR pin low). No board-specific
override; this is a sensor-datasheet constant, not something the board schematic affects.

**4. Debug logging added — one line per trace point requested, at the point it actually happens:**

| Trace point | Location | Level | Example |
|---|---|---|---|
| I²C initialization | `i2c_bus_ensure_init()` | `ESP_LOGI` (start + success), `ESP_LOGE` (param/install failure) | `"Initializing I2C master: I2C0 SDA=GPIO37 SCL=GPIO38 100000 Hz"` → `"I2C master initialized successfully"` |
| Sensor detection | `sht41_init()` (soft-reset probe) | `ESP_LOGI` (probing + detected), `ESP_LOGE` (not detected, with a wiring hint) | `"SHT41 detected and reset OK (I2C0 SDA=GPIO37 SCL=GPIO38 addr=0x44)"` |
| Measurement command | `sht41_read_once()` | `ESP_LOGE` on failure (success is implied by reaching the next stage) | `"Measurement command (0xFD) failed: ESP_ERR_TIMEOUT"` |
| CRC validation | `sht41_read_once()` | `ESP_LOGE`, names *which* field failed (temperature vs. humidity) with the raw bytes and both the calculated and received CRC byte; a pass is folded into the one success line below rather than logged separately (it's implied — conversion never runs on a failed CRC) | `"CRC validation FAILED (temperature): raw=1A 2B 3C calc=0x9F recv=0x00"` |
| Temperature / humidity reading | `sht41_read_once()`, on success | `ESP_LOGI`, one consolidated line | `"SHT41 read OK: CRC valid, temperature=24.35 C, humidity=52.10 %RH"` |
| Retry attempts | `sht41_read()` | `ESP_LOGW` per attempt (unchanged from Phase 6.1), `ESP_LOGE` once if all `SHT41_READ_RETRIES` (3) are exhausted (new this phase) | `"read attempt 2/3 failed: ESP_ERR_TIMEOUT"` → `"SHT41 read failed after 3 attempts (last error: ESP_ERR_TIMEOUT)"` |
| First live data (dashboard transition) | `sensor_manager.c`'s `sensor_task()` | `ESP_LOGI`, once, on the first successful read after boot | `"SHT41 live data now available — dashboard will show real temperature/humidity from this cycle onward"` |

**Design choice — why one consolidated success line per cycle, not five:** the sensor task runs
at 1 Hz forever; logging every sub-step (command sent, raw bytes, CRC pass, temperature,
humidity) as five separate `ESP_LOGI` lines every single second would make the serial monitor
unreadable within minutes. Given `sdkconfig.defaults` only sets `CONFIG_LOG_DEFAULT_LEVEL_INFO`
(no `CONFIG_LOG_MAXIMUM_LEVEL` override), `ESP_LOGD` calls would compile out entirely in this
build — using them for the routine trace wasn't an option that would actually show up on the
monitor. So the routine (success) path is one `ESP_LOGI` line with everything needed to confirm
the cycle worked (CRC valid + both readings); the *failure* paths get one distinct `ESP_LOGE`
line per failure type (command, I²C read, CRC-temperature, CRC-humidity) so a fault is fully
diagnosable from a single log line without needing to correlate multiple lines. This satisfies
every named trace point in the brief while keeping the monitor usable during an actual bring-up
session.

**5. Init failure reporting (brief item 5):** `sht41_init()` now distinguishes two failure
classes clearly:
- I²C bus install failure (`i2c_param_config`/`i2c_driver_install` genuinely failing, not the
  tolerated `ESP_ERR_INVALID_STATE` "already installed" case) → `ESP_LOGE`, and `sht41_init()`
  returns the real error to `sensor_manager_init()`, which itself already logs a warning and
  continues (Phase 6.1 behavior, unchanged).
- Sensor not detected (soft-reset command gets no ACK) → `ESP_LOGE` with an explicit wiring hint
  (`"Check wiring (SDA=GPIO37, SCL=GPIO38) and sensor power."`) and a note that
  `sensor_manager` will keep retrying — but `sht41_init()` still returns `ESP_OK` here (unchanged
  contract from Phase 6.1), since the real recovery path is `sht41_read()`'s own retry loop
  running every sensor cycle, not a one-shot init check. The previous version's misleading
  unconditional `"SHT41 initialized"` log on this path is gone — it now only logs "detected and
  reset OK" when that's actually true.

**6. Sensor-not-detected behavior (brief item 6) — already correct from Phase 6.1, verified,
not changed:** `sensor_manager.c`'s `sensor_task()` already (a) logs the reason
(`ESP_LOGW`/`ESP_LOGE` from the driver, plus its own `"SHT41 read failed — no valid reading
yet"` warning), (b) never blocks or crashes (every I²C call has a bounded 50 ms timeout), and
(c) keeps the dashboard showing `"--"` until the first successful read (`sensor_ok=false` before
`s_temp_hum_valid` ever becomes true), then switches to holding the *last valid* reading on any
later transient failure rather than reverting to `"--"`. This phase's review confirmed this
logic was already correct and added the "first live data" log (above) as the one genuinely
missing piece — a clear signal, in the log, of exactly when the dashboard's numbers become real.

**7. Dashboard auto-update (brief item 7):** unchanged and re-confirmed by re-reading
`screen_dashboard_update()` — it calls `sensor_manager_get_data()` and renders whatever
`sensor_data_t` currently holds. Since Phase 6.1 already wired `sensor_task()` to overwrite
`fresh.temperature_c`/`humidity_pct` with `sht41_read()`'s result, no Dashboard code needed to
change for real values to appear — confirmed by inspection, not by a new capability.

**8/9. Architecture unchanged:** `sensor_manager.c`'s task/mutex/public-API shape, and
`history_manager.h`/`alarm_manager.h`'s APIs, are untouched — confirmed by diff (only the two
sensor files listed below changed).

**Files modified (2):**
- `main/sensors/sht41/sht41.c` — added the logging described above; fixed the misleading
  unconditional "initialized" log on a failed detection; split the CRC check into two named
  checks (temperature vs. humidity) instead of one combined condition, so a CRC failure log
  says which field failed
- `main/sensors/sensor_manager.c` — added the one "first live data" transition log in
  `sensor_task()`; no logic change

**Files deliberately NOT modified:** `sht41.h` (no new API — logging is internal to `sht41.c`),
`sensor_backend*.c`, `history_manager.c/.h`, `alarm_manager.c/.h`, `screen_dashboard.c/.h`, any
chart/logs/settings/nav-drawer file, `CMakeLists.txt` (no new files), `board.h` (pins unchanged
from the Phase 6.1 follow-up).

**I²C pins used:** SDA=GPIO 37, SCL=GPIO 38 (unchanged).  
**I²C frequency:** 100 kHz standard mode (unchanged).  
**Sensor address:** `0x44` (unchanged).

**Expected serial monitor output — sensor present and responding (confirmed to match the
pattern actually observed on real hardware; exact temperature/humidity values not recorded
here):**
```
I (xxx) sht41: Initializing I2C master: I2C0 SDA=GPIO37 SCL=GPIO38 100000 Hz
I (xxx) sht41: I2C master initialized successfully
I (xxx) sht41: Probing SHT41 at address 0x44 (soft reset command 0x94)
I (xxx) sht41: SHT41 detected and reset OK (I2C0 SDA=GPIO37 SCL=GPIO38 addr=0x44)
I (xxx) sensor_mgr: Sensor manager initialized
I (xxx) sensor_mgr: Sensor task running
I (xxx) sht41: SHT41 read OK: CRC valid, temperature=24.35 C, humidity=52.10 %RH
I (xxx) sensor_mgr: SHT41 live data now available — dashboard will show real temperature/humidity from this cycle onward
I (xxx) sht41: SHT41 read OK: CRC valid, temperature=24.36 C, humidity=52.05 %RH
... (one "SHT41 read OK" line every ~1 s thereafter)
```

**Expected serial monitor output — sensor absent/miswired:**
```
I (xxx) sht41: Initializing I2C master: I2C0 SDA=GPIO37 SCL=GPIO38 100000 Hz
I (xxx) sht41: I2C master initialized successfully
I (xxx) sht41: Probing SHT41 at address 0x44 (soft reset command 0x94)
E (xxx) sht41: SHT41 NOT detected at address 0x44 — soft reset failed: ESP_ERR_TIMEOUT. Check wiring (SDA=GPIO37, SCL=GPIO38) and sensor power. sensor_manager will keep retrying every sample cycle.
I (xxx) sensor_mgr: Sensor manager initialized
I (xxx) sensor_mgr: Sensor task running
W (xxx) sht41: read attempt 1/3 failed: ESP_ERR_TIMEOUT
W (xxx) sht41: read attempt 2/3 failed: ESP_ERR_TIMEOUT
W (xxx) sht41: read attempt 3/3 failed: ESP_ERR_TIMEOUT
E (xxx) sht41: SHT41 read failed after 3 attempts (last error: ESP_ERR_TIMEOUT)
W (xxx) sensor_mgr: SHT41 read failed — no valid reading yet
... (repeats every ~1 s; dashboard shows "--" for temperature/humidity, VOC gauge still works from simulation)
```

**Wiring summary (GPIO_D header, HY2.0-4P connector) — confirmed working:**

| SHT41 breakout pin | Connect to | Board signal |
|---|---|---|
| VDD | Board 3.3V | Powered and communicating — confirmed on hardware |
| GND | Board GND | — |
| SDA | GPIO 37 | GPIO_D pin — confirmed, no swap needed |
| SCL | GPIO 38 | GPIO_D pin (formerly `LCD_AUX_GPIO`) — confirmed, no swap needed |

Fallback checklist below is retained for reference (e.g. if a different SHT41 unit or a longer
cable run behaves differently), but this exact pin configuration is now hardware-confirmed
working, not just a predicted wiring plan:
1. VDD/GND connected and at the right voltage (SHT41 is 1.08–3.6V per datasheet; this board is
   3.3V logic, compatible).
2. SDA/SCL not swapped — flip `SHT41_I2C_SDA_GPIO`/`SHT41_I2C_SCL_GPIO` in `sht41.c` if a future
   wiring change causes no ACK.
3. Confirm IO37/IO38 are the physical pins actually reached by the GPIO_D connector on the
   specific board revision in use.

**Build status:** `idf.py build` — **exit code 0, zero warnings, zero errors** (ESP-IDF v5.3.1
toolchain in this environment, full log inspected). Flash image grew 727,240 B → 728,992 B
(+1,752 B — the new log format strings and a handful of extra log-call sites; app binary
0xB2010, 30% of the 1 MB app partition free, down from 31%).

**Hardware bring-up — CONFIRMED.** Flashed to the physical CrowPanel with an SHT41 wired to the
GPIO_D header. Result: sensor detected and communicating correctly over I2C at the configured
pins/address (no SDA/SCL swap or address change was needed), Dashboard automatically shows live
temperature/humidity (no Dashboard code change required — confirmed the existing
`sensor_manager_get_data()` → `screen_dashboard_update()` path just works once real data flows
in), and display/touch behavior is normal after the GPIO 38 (`LCD_AUX_GPIO`) removal — no
regression observed. Per-cycle numeric readings are not recorded here (not required for this
log); pin configuration and connectivity are the facts of record — see the summary below and
TD-31/TD-33 in ARCHITECTURE.md, both now marked resolved-and-confirmed.

**Confirmed configuration (hardware-verified):**

| Item | Value | Status |
|---|---|---|
| I2C port | `I2C_NUM_0` | Confirmed |
| SDA | GPIO 37 (GPIO_D header) | Confirmed — no swap needed |
| SCL | GPIO 38 (GPIO_D header, formerly `LCD_AUX_GPIO`) | Confirmed — no swap needed |
| I2C frequency | 100 kHz standard mode | Confirmed |
| SHT41 address | 0x44 | Confirmed |
| Pull-ups | ESP32-S3 internal (`GPIO_PULLUP_ENABLE`) | Confirmed sufficient — no external pull-ups needed |
| Display/touch after AUX removal | Normal | Confirmed — no regression |

---

*(Phase 4C and Phase 4D, as originally sketched in earlier revisions of this log, are both
superseded by Phase 5.3 and the Phase 5.4 review above.)*

---

### ⬜ Phase 5 — Data Logs Screen + History Integration (`record_manager` retired)
**Status: NOT STARTED**

**Goal:** Implement `screen_logs.c` with a scrollable `lv_table` showing recent sensor records.

**Architecture review finding (Phase 5.3):** the originally-planned standalone
`record_manager.c/.h` (1-minute snapshots, 1440-record/24h ring buffer) is **retired** —
`history_manager` (Phase 5.3) is explicitly the single source of truth for both Chart and Logs,
so a second ring buffer of the same readings would duplicate that responsibility. Logs should
read `history_manager_get_samples()` / `_get_range()` instead.

**Open gap:** `history_manager` currently stores only *hourly*-resolution records. If the Logs
table needs true 1-minute rows (not hourly), a bounded minute-resolution tier should be added
to `history_manager` itself when this phase starts — not a new standalone module.

**Design spec:**
- `lv_table` columns: Time | TVOC (ppb) | Temp (°C) | Hum (%) | Status
- Column widths: 60 / 60 / 52 / 44 / 56 = 272 px total
- Table fills 430 px content height (nav drawer model); vertical scroll enabled
- Status cell: coloured text ("GOOD" green / "WARN" amber / "ALARM" red)
- Data source: `history_manager` (see gap above re: resolution)

**Files to modify:**
- `main/ui/screens/screen_logs.c` — Full implementation; `screen_logs_refresh()` rebuilds table
  rows from `history_manager_get_samples()` / `_get_range()`
- `main/data/history_manager.c/.h` — only if per-minute resolution is required (see gap above)

**sdkconfig prerequisite:**
- Verify `CONFIG_LV_USE_TABLE=y`

**Acceptance criteria:**
- Table shows at least 24 rows of history
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

### ⬜ Phase 7 — Sensor Framework (ENS160 — scope reduced by Phase 6.1)
**Status: NOT STARTED**

**Goal:** Fill in `sensor_backend_hw.c` with a real ENS160 I2C TVOC read. Temperature/humidity
are no longer part of this phase — Phase 6.1 already replaced them with real SHT41 readings
(`main/sensors/sht41/`), so the AHT21 driver originally planned here is no longer needed; ENS160
compensation reads the already-real `temperature_c`/`humidity_pct` `sensor_manager.c` provides.
One `CMakeLists.txt` line swap still activates the VOC hardware path — zero changes to
`sensor_manager.c`, dashboard, alarm_manager, or any other module.

**Activation (one-line swap in `main/CMakeLists.txt`):**
```cmake
# "sensors/sensor_backend_sim.c"   ← comment out
  "sensors/sensor_backend_hw.c"    ← uncomment; ENS160 TODOs filled
```

**Hardware:**
- ENS160 module on I2C bus — shares `I2C_NUM_0` (SDA=GPIO 37, SCL=GPIO 38 — the "GPIO_D"
  expansion header), which `sht41.c` (Phase 6.1) already installs; `i2c_driver_install()`'s
  `ESP_ERR_INVALID_STATE` tolerance there exists specifically so this driver can call the same
  install path without a bus conflict
- ENS160 I2C address: 0x53 (ADDR pin low) or 0x52 (ADDR pin high) — confirm wiring

**Integration notes:**
- Feed the real `temperature_c`/`humidity_pct` (already available from `sensor_manager.c` via
  SHT41, Phase 6.1) to ENS160's compensation registers → read ENS160 TVOC
- ENS160 warm-up ~60 s before TVOC readings are valid — add `SENSOR_LEVEL_WARMING` state; badge shows `"Warming..."` during this period
- Check ENS160 data validity flag before accepting readings
- I2C transaction failure × 3 consecutive → set `sensor_ok=false` → dashboard shows `"--"` / `"ERROR"` badge

**Files to modify:**
- `main/sensors/sensor_backend_hw.c` — implement `sensor_backend_init()` (ENS160 mode; I2C bus already installed by `sht41.c`) and `sensor_backend_sample()` (ENS160 compensation using `sensor_manager.c`'s real temp/humidity → ENS160 TVOC read)
- `main/CMakeLists.txt` — swap sim → hw backend for the VOC path; add ENS160 driver source

**Files to create:**
- `main/sensors/ens160_driver.c/.h` — I2C mode set, TVOC read, compensation write, validity check

**Acceptance criteria:**
- Dashboard shows real TVOC (not sine wave); temperature/humidity already real since Phase 6.1
- ENS160 warm-up: badge shows `"Warming..."` then transitions to `"GOOD"`/`"WARN"`/`"ALARM"` when valid
- Sensor error displayed if I2C fails 3 consecutive times; recovers automatically on comms restore

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

**Goal:** Persist `history_manager`'s ring buffer to NVS flash (short-term) and export to SD
card CSV (long-term) so trend charts and logs survive a reboot.

**Design:**
- NVS persistence: flush the hourly ring buffer to NVS on a periodic cycle
- SD card: optional export button on Data Logs screen writes all records to `/sdcard/ivf_log_YYYYMMDD.csv`
- SD pins: SDMMC or SPI2 — confirm available GPIOs in `board.h`
- SNTP time sync: if WiFi available, sync RTC for accurate timestamps
- `history_manager` NVS persistence: save/restore the ring buffer on boot/shutdown (there is
  only one buffer to persist now — `record_manager.c` was retired in Phase 5.3)

**Files to modify:**
- `main/data/history_manager.c` — add NVS/SD persistence of ring buffer state
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
| `main/display/display_driver.c/.h` | ✅ Phase 2.1 complete, brightness added Phase 6, `LCD_AUX_GPIO` removed Phase 6.1 | ST7262 RGB565, PSRAM fb, SWAP_XY+MIRROR_Y hardware rotation; `display_set_backlight(bool)` replaced by `display_set_brightness(0-100)` via LEDC PWM; GPIO 38 "Aux" drive-HIGH step removed (confirmed unneeded on this board's schematic), freeing the pin for I2C — see TD-33 |
| `main/display/display_power.c/.h` | ✅ Phase 6 complete, new | Dim/wake/timeout state machine — 500 ms tick, dims to 5% after configured idle timeout, alarm-active gate, wake-on-touch |
| `main/touch/touch_driver.c/.h` | ✅ Phase 2.1 complete | map_x direct, map_y inverted — correct for left-edge-up. `*x` = portrait_Y, `*y` = portrait_X (by design) |
| `main/lvgl_port/lvgl_port.c/.h` | ✅ Phase 4A fix applied, wake-touch added Phase 6, drawer touch-gate added Phase 6.4 | `LV_DISP_ROT_NONE`, full-frame PSRAM draw buffer. Phase 4A: x/y swap in `lvgl_touch_read_cb`. Phase 6: swallows the touch that wakes a dimmed screen (reports `LV_INDEV_STATE_RELEASED`) instead of passing it through to a widget. Phase 6.4: also swallows touches outside the nav drawer's width while it's open, closing the drawer directly instead of relying on LVGL's own top-layer click dispatch |
| `main/board/board.h` | ✅ Phase 1 complete, I2C corrected Phase 6.1 follow-up | Central GPIO pin map; I2C pins are SDA=37, SCL=38 — the "GPIO_D" expansion header (corrected from an initial GPIO 17/18 placeholder after checking Elecrow's own board docs); `BOARD_LCD_AUX` (38) removed (`sensors/sht41/sht41.c` defines the same pin values locally, per this project's per-driver-owns-its-pins convention) |

### UI Framework
| File | Status | Notes |
|------|--------|-------|
| `main/ui/ui.h` | ✅ Phase 4B complete, theme macros Phase 6.1, drawer-state accessors Phase 6.4 | `IVF_HEADER_H=50`, `IVF_CONTENT_H=430`, `IVF_DRAWER_W`, `IVF_NAV_BTN_SIZE`; tab bar constants removed; 8 surface-color macros (`IVF_COLOR_BG` etc.) redefined as function calls (`ivf_color_bg()` etc.) for Light/Dark theme support — every screen using them becomes theme-aware with no source change of its own; `ui_nav_drawer_is_open()`/`ui_nav_drawer_close_from_touch()` added (Phase 6.4) so `lvgl_port.c` can read/close drawer state without owning the `navigation_drawer_t` handle |
| `main/ui/ui.c` | ✅ Phase 4.2.7 updated, Phase 6 timer added, Phase 6.1 theme resolvers, Phase 6.4 drawer accessors | LVGL timer `dash_timer_cb` (1 Hz, replaces `ui_refresh_task`); `ui_dashboard_refresh()` removed; `navigation_drawer_t` integration; `"TVOC Chart"` / `"Data Logs"` labels; `create_fab=false`; new 500 ms `power_timer_cb` → `display_power_tick()` (Phase 6); 8 `ivf_color_*()` functions + `s_dark_mode` loaded in `ui_init()` (Phase 6.1); `ui_nav_drawer_is_open()`/`ui_nav_drawer_close_from_touch()` implementations (Phase 6.4) |
| `main/ui/nav_drawer.h` | ✅ Phase 4B complete | Legacy nav drawer API — retained as header only; `nav_drawer.c` removed from build |
| `main/ui/nav_drawer.c` | ⛔ Phase 4.2.5 removed from build | Commented out in `CMakeLists.txt` — superseded by `navigation_drawer.c` (Phase 4.2.2) |
| `main/ui/assets/assets.h` | ✅ Phase 4.2.6 updated | Drawn icon API: leaf, wifi, sd_card, thermometer, humidity, clock, chart, **shield** |
| `main/ui/assets/assets.c` | ✅ Phase 4.2.6 updated | `assets_draw_humidity()` updated (16×22 teardrop); `assets_draw_shield()` added (28×32 geometric primitive) |
| `main/ui/components/status_badge/status_badge.h/.c` | ✅ Phase 4.1 complete | Pill badge GOOD/MODERATE/POOR/DANGER/ERROR + custom |
| `main/ui/components/icon_button/icon_button.h/.c` | ✅ Phase 4.1 complete | Circular FAB button with symbol, shadow, callback |
| `main/ui/components/card/card.h/.c` | ✅ Phase 4.1 complete | Card container; `card_get_obj()` for positioning, `card_get_content()` for widgets |
| `main/ui/components/circular_gauge/circular_gauge.h/.c` | ✅ Phase 4.2.4 updated | Progressive arc gauge; `circular_gauge_set_value_animated()`; font references fixed (TD-13 resolved) |
| `main/ui/components/voc_gauge/voc_gauge.h/.c` | ✅ Phase 4.2.5 updated | Product-specific TVOC gauge; 4-zone progressive arcs; badge thresholds; 500 ms animation; `VOC_GAUGE_NO_READING` sentinel; MODERATE badge uses dark text |
| `main/ui/components/header/header.h/.c` | ✅ Phase 4.2.6 updated, SD-opacity + burger-width fixed Phase 6.1/6.3/6.4 | 272×50 header; WiFi far right (`HDR_WIFI_X=244`); SD icon removed; `HDR_TIME_ROFS=32`; `HDR_TITLE_MAX_X=156`; title font `IVF_FONT_NORMAL`; time/date right-aligned; SD "absent" icon opacity 30%→100% (Phase 6.1, dark-theme visibility); `HDR_BTN_W` 20→44→30 (Phase 6.3 then 6.4 — 44 clipped "DASHBOARD" on real hardware, confirmed; 30 is the settled value, giving the title 91px — see TD-29) — three narrow, explicitly-requested exceptions to the Phase 5.2 header freeze |
| `main/ui/components/navigation_drawer/navigation_drawer.h/.c` | ✅ Phase 4.2.6 updated, animation shortened Phase 6.3, made instant Phase 6.4 | Full-screen height (480 px, y=0); `DRAWER_HEADER_H=148`; top section (circle+shield+badge+title+pill); `header_title/header_status/footer_version` cfg fields; version footer; `#include "assets.h"`; slide animation 220ms→120ms (Phase 6.3) then replaced with an instant jump (Phase 6.4, animated version kept `#if 0`'d) — never frozen |
| `main/ui/screens/screen_splash.c/.h` | ✅ Phase 2 complete | Portrait size fix |
| `main/ui/screens/screen_dashboard.c/.h` | ✅ Phase 4.2.6 complete · **FROZEN** | `header_t` + `card_t` + `voc_gauge_t`; title "DASHBOARD"; sparklines removed; `CARD_H=90`; `build_sensor_card()` simplified |
| `main/ui/screens/screen_chart.c/.h` | ✅ Phase 5.6 complete · **FROZEN** | `header_t` (frozen) + `card_t` + real bitmap icon assets (Phase 5.4.1); `chart_mode_t` + `apply_chart_mode()` central controller; Last 7 Days (default) / Today (simple 2-row dropdown, Phase 5.6) modes; live `history_manager` data; title shares Row A with the calendar button; mode-aware 4th stat card; real-calendar-date X-axis labels; axis tick labels fixed (removed `clip_corner`); no return chip (dropdown covers it). |
| `main/ui/screens/screen_logs.c/.h` | ✅ Phase 5.9 complete · **FROZEN** | `header_t` + `card_t` + `datalog_icon.c` bitmap; table card (header row + scrollable flex-column rows), 10/page, "Load More" pagination via `history_manager_get_latest_n()`; Export CSV is a visual placeholder; refresh-cadence/event-driven-row questions open (TD-23/TD-24) |
| `main/ui/screens/screen_settings.c/.h` | ✅ Phase 6.2 complete · **FROZEN** | `header_t` + `card_t`; scrollable-if-needed content (flex-column), currently fits without scrolling; uniform 12px body text; Brightness/Theme/Screen Timeout/4 ppb-threshold rows all as dropdowns via one shared value-picker overlay (brightness slider kept, `#if 0`'d, not deleted); collapsible Alert Settings section |

### Data / Sensors
| File | Status | Notes |
|------|--------|-------|
| `main/sensors/sensor_backend.h` | ✅ Phase 3B complete | Backend interface: `sensor_backend_init()` + `sensor_backend_sample()` — VOC path only as of Phase 6.1 |
| `main/sensors/sensor_backend_sim.c` | ✅ Phase 3B complete, unchanged by Phase 6.1 | Sine-wave simulation of all 3 channels — VOC output is still used; its temp/humidity output is now discarded by `sensor_manager.c` in favor of real SHT41 readings. Swap out (VOC only) in Phase 7 |
| `main/sensors/sensor_backend_hw.c` | ⬜ Phase 7 stub, scope reduced by Phase 6.1 | Real ENS160 TVOC only — fill TODOs in Phase 7 (AHT21 no longer needed here; temp/humidity already real via `sht41.c`) |
| `main/sensors/sht41/sht41.h/.c` | ✅ Phase 6.1 complete, new | Sensirion SHT41 I2C driver — soft reset, high-rep measurement, CRC-8, unit conversion, internal 3× retry; owns/installs `I2C_NUM_0` (idempotent, shareable with a future ENS160 driver) |
| `main/sensors/sensor_manager.c/.h` | ✅ Phase 5.3 updated, Phase 6 threshold reload, Phase 6.1 SHT41 integration | Pure framework: task, mutex, NVS, public API — calls sensor_backend_* for VOC (sim); calls `sht41_read()` directly for temp/humidity, falling back to the last valid value (or `sensor_ok=false` pre-first-reading) on failure; `sensor_task()` also feeds `history_manager_add_sample()` every 60th iteration (Phase 5.3); VOC warn/alarm sourced from `config_manager`, live-reloadable via `sensor_manager_reload_thresholds()` (Phase 6) |
| `main/data/alarm_manager.c/.h` | ✅ Phase 6 — VOC threshold now runtime-configurable | Debounce (3 samples) + 32-entry ring buffer, ack/ack-all API unchanged; `VOC_ALARM_PPB` `#define` replaced with runtime `s_voc_alarm_ppb` sourced from `config_manager`, live-reloadable via `alarm_manager_reload_thresholds()` — this is the real critical-alarm trigger point behind Settings' "High Alert Threshold". Temp/humidity thresholds still fixed constants. |
| `main/data/history_manager.c/.h` | ✅ Phase 5.3 complete, API revised Phase 5.4, extended Phase 5.8 | 90-day hourly ring buffer (PSRAM, ~59.1 KB with min/max), fed from `sensor_manager.c`; read by `screen_chart.c` since Phase 5.4 (`get_range`/`get_daily_aggregates`/`compute_stats`), and by `screen_logs.c` since Phase 5.8 (`get_latest_n`, O(count) newest-first pagination) |
| `main/data/calendar_util.c/.h` | ✅ Phase 5.8 complete, new | Shared, LVGL-independent boot-ts → calendar date/time conversion; used by Logs; Chart keeps its own private frozen copy (TD-20) |
| `main/data/record_manager.c/.h` | ⛔ Retired (Phase 5.3) | Superseded by `history_manager` — see Phase 5 note |
| `main/data/config_manager.c/.h` | ✅ Phase 6 complete, `dark_mode` + 15% floor Phase 6.1 | NVS namespace `ivf_cfg` (shared with sensor_manager's pre-existing keys); brightness (15% floor, enforced on set + on NVS load)/timeout/display-range/VOC-warn/VOC-alarm/dark_mode; passive — never calls into display/sensor/alarm managers itself, `screen_settings.c` orchestrates |
| `main/sensors/ens160_driver.c/.h` | ❌ Not created | Create in Phase 7 |
| `main/sensors/aht21_driver.c/.h` | ⛔ No longer planned | Superseded by Phase 6.1's `sht41.c` — temperature/humidity are already real; ENS160 (Phase 7) will read compensation values from `sensor_manager.c` directly instead |

### Application
| File | Status | Notes |
|------|--------|-------|
| `main/app_main.c` | ✅ Phase 5.3 updated, Phase 6 init order extended | `ui_refresh_task` removed; `app_main` returns after `ui_init()`; `history_manager_init()` called before `sensor_manager_init()` (Phase 5.3); `config_manager_init()` added right after NVS init (before anything reads a threshold/brightness value), `display_power_init()` added after `lvgl_port_init()` (Phase 6) |

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

7. ~~**SHT41 I2C pins/address**~~ ✅ **Resolved, confirmed on real hardware (Phase 6.2):**
   SDA=GPIO 37, SCL=GPIO 38 (the "GPIO_D" expansion header), address 0x44 — flashed and
   communicating correctly, no SDA/SCL swap or address change needed. See TD-31 in
   ARCHITECTURE.md.

8. ~~**`LCD_AUX_GPIO` (GPIO 38) removed**~~ ✅ **Resolved, confirmed on real hardware (Phase 6.2):**
   this pin was previously configured as output and driven HIGH once at boot in
   `display_driver_init()`, inherited from the official CrowPanel reference example. Removed to
   free GPIO 38 for I2C SCL, based on the user's own schematic review. Flashed and verified —
   display/touch behave normally with no regression. See TD-33 in ARCHITECTURE.md.

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
| 2026-07-01 | Phase 5.1 Chart UI Migration (header_t/card_t migration, pill period selector, calendar button, avg-line area fill via draw-part hook, 5 new drawn icons, illustrative sample data) | ⬜ not yet built — pending build + flash | 2 files modified; no files created |
| 2026-07-01 | Phase 5.2 Chart Visual Polish (icon legend, chart spacing/background/grid/axis polish, month axis labels, card shadow/spacing match Dashboard, unified 16×16 card icons) | ⬜ not yet built — pending build + flash | 2 files modified; no files created |
| 2026-07-01 | Phase 5.3 History Manager Backend (`history_manager.c/.h` new; `sensor_manager.c` + `app_main.c` wired; `record_manager.c` retired) | ⬜ not yet built — pending build + flash | 2 files created; 3 files modified |
| 2026-07-03 | Phase 5.4 Chart Mode Integration & History Binding (period selector removed; `chart_mode_t`/`apply_chart_mode()`; Last 7 Days + Selected Day modes; `history_manager` API revised — `get_range`/`get_daily_aggregates`/`compute_stats`; min/max schema widening) | ⬜ not yet built — pending build + flash | 2 files modified; no files created |
| 2026-07-03 | Phase 5.4.1 Real Bitmap Icons (5 pre-existing `*_icon.c` bitmap files fixed — hyphen→underscore identifiers — and wired into the build; `assets.c/.h`, `screen_chart.c`, `CMakeLists.txt` updated) | ⬜ not yet built — pending build + flash | 5 files modified (bitmap identifier fix) + `assets.c/.h`, `screen_chart.c`, `CMakeLists.txt`; no files created |
| 2026-07-06 | Phase 5.5 Real Calendar Date Picker (`lv_list` relative-day rows replaced with a real `lv_calendar` grid bounded to the 90-day retention window; dependency-free Hinnant calendar math; Selected-Day X-axis → fixed 4h ticks; Last-7-Days X-axis → real calendar dates) | ⬜ not yet built — pending build + flash | 1 file modified (`screen_chart.c`); no files created |
| 2026-07-06 | Phase 5.6 Picker Simplification + Axis Label Fix (calendar-grid picker replaced with a plain Today/7-Days dropdown; `CHART_MODE_SELECTED_DAY` → `CHART_MODE_TODAY`; removed `clip_corner` on `s_chart` which was hiding both axes' tick labels) | ⬜ not yet built — pending build + flash | 1 file modified (`screen_chart.c`); no files created |
| 2026-07-06 | Phase 5.7 Chart Screen Freeze (status declaration — Chart screen now FROZEN, same standing as Dashboard) | n/a | 0 files changed |
| 2026-07-06 | Phase 5.8 Logs Screen (`screen_logs.c` rewritten from stub; `history_manager_get_latest_n()` added; `calendar_util.c/.h` created; `datalog_icon.c` identifier bug fixed + wired; 10/page "Load More" pagination, hourly-resolution rows, Export CSV placeholder) | ⬜ not yet built — pending build + flash | 2 files created (`data/calendar_util.h/.c`); 6 files modified; no Dashboard/Chart changes |
| 2026-07-06 | Phase 5.9 Logs Screen Freeze (status declaration — Logs screen now FROZEN, same standing as Dashboard/Chart; refresh-cadence, event-driven-row, and real-data-timing questions documented as open, not answered) | n/a | 0 files changed |
| 2026-07-06 | Phase 6 Settings Screen (`screen_settings.c` rebuilt from stub; new `data/config_manager.c/.h` + `display/display_power.c/.h`; `display_driver.c` gains real PWM brightness; `sensor_manager.c`/`alarm_manager.c` gain live threshold reload — fixes a pre-existing gap where alarm_manager's real trigger threshold was never connected to Settings/NVS at all; `lvgl_port.c` gains wake-on-touch consumption) | ⬜ not yet built — pending build + flash | 2 files created (`data/config_manager.h/.c`, `display/display_power.h/.c`); 8 files modified |
| 2026-07-06 | Phase 6.1 Font Size, Brightness Floor, Light/Dark Theme (`screen_settings.c` body text unified to 12px; brightness floor 5%→15% enforced in 3 places; `ui.h`'s 8 surface-color macros redefined as theme-resolving function calls, making every screen — including frozen ones — theme-aware with zero source changes to them; Theme row now real, reboots via `esp_restart()` to apply) | ⬜ not yet built — pending build + flash | 0 files created; 4 files modified (`ui.h`, `ui.c`, `config_manager.h/.c`, `screen_settings.c`); no Dashboard/Chart/Logs source changes |
| 2026-07-06 | Phase 6.1 follow-ups (nav row heights 44→18px so Settings fits without scrolling; slider knob clipping fixed then slider replaced entirely by a Brightness dropdown, 15/25/50/75/100%, slider code kept `#if 0`'d not deleted; header.c SD-icon opacity fix for dark-theme visibility — one narrow exception to the Phase 5.2 header freeze) | ⬜ not yet built — pending build + flash | 0 files created; `screen_settings.c` + `header.c` modified |
| 2026-07-06 | Phase 6.2 Settings Screen Freeze (status declaration — Settings screen now FROZEN, same standing as Dashboard/Chart/Logs; display-range-not-consumed, temp/humidity-no-live-reload, and not-yet-flashed questions documented as open, not answered) | n/a | 0 files changed |
| 2026-07-06 | Phase 6.3 Navigation Drawer & Burger Button Responsiveness (investigated 3 candidate causes for "needs a harder press" before fixing; `navigation_drawer.c` slide 220ms→120ms; `header.c` `HDR_BTN_W` 20→44, restoring the file's own documented-but-never-implemented 44px design — second narrow exception to the Phase 5.2 header freeze; title-width trade-off disclosed, see TD-29) | ⬜ not yet built — pending build + flash | 0 files created; 2 files modified (`navigation_drawer.c`, `header.c`) |
| 2026-07-08 | Phase 6.4 Burger Width Tuning, Instant Drawer, Touch-Passthrough Fix (predicted TD-29 clipping confirmed on hardware; `header.c` `HDR_BTN_W` 44→30 — third narrow header-freeze exception; `navigation_drawer.c` slide animation replaced with instant jump, animated version kept `#if 0`'d; `lvgl_port.c` gates raw touch points outside the drawer's width while open, closing it directly instead of relying on LVGL's own top-layer dispatch; new `ui_nav_drawer_is_open()`/`ui_nav_drawer_close_from_touch()` in `ui.h`/`ui.c`) | ⬜ not yet built — pending build + flash | 0 files created; 5 files modified (`header.c`, `navigation_drawer.c`, `lvgl_port.c`, `ui.h`, `ui.c`) |
| 2026-07-10 | Phase 6.1 — SHT41 Temperature & Humidity Sensor Integration (new `sensors/sht41/sht41.h/.c`; `sensor_manager.c` calls `sht41_init()`/`sht41_read()`, overriding simulated temp/humidity, with last-known-good fallback on failure; `CMakeLists.txt` + `board.h` comment updated) | ✅ **First actual `idf.py build` run in this environment** — ESP-IDF v5.3.1 toolchain located and its broken Python venv repaired (missing packages installed; an `importlib.metadata` name-normalization mismatch for `ruamel.yaml`'s dist-info directory was worked around). 1408/1408 objects, **exit code 0, zero warnings, zero errors** (full log inspected). Re-verified with a stash/rebuild/pop cycle to get an exact before/after size delta (see Phase 6.1 write-up above) | 0xB1960 (713 KB, 31% free) — +19,296 B flash image vs. pre-Phase-6.1 baseline (0xACE00, 707,992 B) |
| 2026-07-10 | Phase 6.1 follow-up — I2C pin correction (GPIO 17/18 → GPIO 37/38, "GPIO_D" header, per Elecrow's own board docs) + `LCD_AUX_GPIO` (38) removed from `display_driver.c`/`.h` (confirmed against user's schematic to be unneeded, freeing the pin for I2C) | ✅ Rebuilt, exit code 0, zero warnings, zero errors | 0xB1930 (713 KB, 31% free) — −48 B vs. the initial Phase 6.1 build (0xB1960), noise-level |
| 2026-07-10 | Phase 6.2 — SHT41 Hardware Bring-up & Live Sensor Verification (bring-up debug logging added to `sht41.c`/`sensor_manager.c`: I2C init, sensor detection, measurement command, CRC validation (per-field), temperature/humidity reading, retry exhaustion, first-live-data transition; fixed a misleading "initialized" log that fired even when the sensor wasn't actually detected) | ✅ Rebuilt, exit code 0, zero warnings, zero errors. **Flashed and confirmed on real hardware** — SHT41 detected and communicating at SDA=GPIO37/SCL=GPIO38/0x44, Dashboard shows live values, display/touch unaffected by the GPIO 38 (AUX) removal | 728,992 B total image (+1,752 B vs. prior build — new log strings) |

---

## Sensor Thresholds (Default, stored in NVS `ivf_cfg`)

| Parameter | Warning | Alarm |
|-----------|---------|-------|
| TVOC | 250 ppb (Phase 6: was 300 — `config_manager`'s default now governs a fresh install, snapped to the Settings dropdown's `{0,250,500,750,1000}` set) | 500 ppb |
| Temperature | 26 °C | 28 °C |
| Humidity low | — | 35 % |
| Humidity high | 65 % | — |

Temperature/humidity thresholds are still `sensor_manager.c`'s own fixed fallback constants and
`alarm_manager.c`'s own hardcoded constants respectively (no Settings UI, no `config_manager`
entry — see TD-26). Only TVOC warn/alarm are Settings-editable and NVS-persisted as of Phase 6.
