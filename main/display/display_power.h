#pragma once

/* =======================================================================
 * display_power.h — screen dim/wake/timeout state machine (Phase 6)
 *
 * Owns exactly one behavior: after config_manager's configured timeout
 * elapses with no touch activity, dim the backlight to
 * CONFIG_DIM_BRIGHTNESS_PCT (5%); restore it to the configured brightness
 * the moment the user touches the screen again. Two hard rules layered on
 * top of that:
 *
 *   - Never dim while alarm_manager_active_count() > 0 — an unacknowledged
 *     alarm/error keeps the screen at full configured brightness no matter
 *     how long it's been idle, and immediately un-dims if it was already
 *     dimmed when the alarm fired.
 *   - Timeout = CONFIG_TIMEOUT_NONE (0) means "never dim" outright.
 *
 * This module does not read touch hardware itself — lvgl_port.c's touch
 * read callback calls display_power_notify_touch() on every real press and
 * checks display_power_is_dimmed() to decide whether to swallow that same
 * press (see lvgl_port.c: the touch that wakes the screen is consumed, it
 * does not also activate whatever LVGL widget is underneath — same
 * phone-lock-screen pattern most touchscreens use).
 *
 * Usage
 *   display_power_init();                 // once, after config_manager_init()
 *   // in the touch read callback:
 *   if (display_power_is_dimmed()) { display_power_wake(); swallow this read; }
 *   else if (pressed) { display_power_notify_touch(); }
 *   // in a periodic (e.g. 500 ms) timer:
 *   display_power_tick();
 *   // after Settings changes brightness/timeout live:
 *   display_power_reload_settings();
 * ======================================================================= */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Reads current brightness/timeout from config_manager and applies them. */
void display_power_init(void);

/** Call on every real (non-swallowed) touch press. Resets the idle clock. */
void display_power_notify_touch(void);

/** True if the backlight is currently dimmed to CONFIG_DIM_BRIGHTNESS_PCT. */
bool display_power_is_dimmed(void);

/** Restores the configured brightness, clears the dimmed flag, resets the
 *  idle clock — call this instead of notify_touch() for the specific touch
 *  that wakes the screen (see lvgl_port.c). */
void display_power_wake(void);

/** Call periodically (a few times a second is plenty) — checks the alarm
 *  gate and elapsed idle time, dims/wakes the backlight as needed. */
void display_power_tick(void);

/** Call after config_manager_set_brightness_pct()/set_timeout_sec() so a
 *  live Settings change takes effect immediately instead of on next boot. */
void display_power_reload_settings(void);

#ifdef __cplusplus
}
#endif
