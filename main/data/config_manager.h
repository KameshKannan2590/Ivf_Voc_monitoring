#pragma once

/* =======================================================================
 * config_manager.h — Settings persistence (Phase 6)
 *
 * Single source of truth for every value the Settings screen exposes:
 * display brightness, screen timeout, the two display-range values, and
 * the two VOC alert thresholds. All of it lives in NVS namespace
 * "ivf_cfg" (the same namespace sensor_manager already used for raw
 * threshold reads) so this module doesn't create a second, competing
 * settings store — it's the missing write path plus a cached read path.
 *
 * Deliberately passive: this module only gets/sets/persists values. It
 * never calls into display_driver, sensor_manager, or alarm_manager —
 * screen_settings.c is responsible for calling this AND the relevant
 * *_reload_thresholds()/display_set_brightness() calls when a value
 * changes, exactly like every other screen->data-layer boundary in this
 * project.
 *
 * "TVOC High Threshold" / "Threshold (ppb)" (the two display-range
 * values) are persisted here but are NOT currently consumed by Dashboard
 * or Chart — both screens are FROZEN (Phase 4.2.6 / Phase 5.7) and their
 * gauge/chart scales stay hardcoded at 0-1000 until an explicit decision
 * is made to unfreeze them and wire this in. See TD-25.
 *
 * Usage
 *   config_manager_init();                              // once, at boot
 *   uint8_t pct = config_manager_get_brightness_pct();
 *   config_manager_set_brightness_pct(80);               // persists immediately
 * ======================================================================= */

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Screen-timeout dropdown values, in seconds. 0 = "None" (never dim). */
#define CONFIG_TIMEOUT_NONE  0
#define CONFIG_TIMEOUT_15S   15
#define CONFIG_TIMEOUT_30S   30
#define CONFIG_TIMEOUT_45S   45
#define CONFIG_TIMEOUT_60S   60

/* Backlight level applied automatically once the screen times out. Never
 * fully dark — floor of 15% so the user can still see where to tap to
 * wake it back up. Also the brightness slider's minimum (see
 * screen_settings.c) so a manual setting can't go dimmer than the
 * auto-dim level either. */
#define CONFIG_DIM_BRIGHTNESS_PCT 15

/**
 * Load every setting from NVS into an in-RAM cache, falling back to the
 * defaults below for anything not yet written. Must be called once at
 * boot, before any getter is used.
 */
esp_err_t config_manager_init(void);

/** Display backlight brightness, 0-100. Default 70. */
uint8_t config_manager_get_brightness_pct(void);
void    config_manager_set_brightness_pct(uint8_t pct);

/** Screen timeout in seconds; 0 = never dim. Default 30. */
uint16_t config_manager_get_timeout_sec(void);
void     config_manager_set_timeout_sec(uint16_t sec);

/** "Threshold (ppb)" — display-range reference marker. Default 500. Not
 *  yet consumed by any screen (Dashboard/Chart frozen) — see TD-25. */
int32_t config_manager_get_display_threshold_ppb(void);
void    config_manager_set_display_threshold_ppb(int32_t ppb);

/** "TVOC High Threshold" — display-range max scale. Default 1000. Not
 *  yet consumed by any screen (Dashboard/Chart frozen) — see TD-25. */
int32_t config_manager_get_display_max_ppb(void);
void    config_manager_set_display_max_ppb(int32_t ppb);

/** "TVOC Alert Threshold" (warning tier). Default 250 (NVS key "voc_warn",
 *  shared with sensor_manager's pre-existing threshold read). */
int32_t config_manager_get_voc_warn_ppb(void);
void    config_manager_set_voc_warn_ppb(int32_t ppb);

/** "High Alert Threshold" (critical tier, real alarm trigger point).
 *  Default 500 (NVS key "voc_alarm", shared with sensor_manager AND
 *  alarm_manager). */
int32_t config_manager_get_voc_alarm_ppb(void);
void    config_manager_set_voc_alarm_ppb(int32_t ppb);

/** Theme: false = Light (default), true = Dark. Applied at boot — this
 *  project styles every widget with one-shot lv_obj_set_style_*() calls
 *  made once when each screen is built (see ui.c: all screens are created
 *  once in ui_init()), so a live theme switch would mean re-styling every
 *  existing widget on every screen. Rather than that large a refactor, a
 *  theme change takes effect on the next boot — see screen_settings.c,
 *  which calls esp_restart() right after saving the new value. */
bool config_manager_get_dark_mode(void);
void config_manager_set_dark_mode(bool dark);

#ifdef __cplusplus
}
#endif
