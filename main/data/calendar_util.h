#pragma once

/* =======================================================================
 * calendar_util.h — dependency-free Gregorian calendar math (Phase 5.8)
 *
 * There is still no RTC/SNTP (Phase 7), so every history_manager timestamp
 * is boot-relative seconds, not wall-clock. Any screen that wants to show
 * a human-readable date/time from one of those timestamps needs the same
 * boot-relative-seconds -> calendar-date conversion. This module is that
 * one shared implementation — Howard Hinnant's public-domain
 * days_from_civil()/civil_from_days() algorithm, zero platform/timezone
 * dependency, zero LVGL dependency (plain scalars in, plain struct out).
 *
 * NOTE: screen_chart.c has its own private, near-identical copy of this
 * exact logic, predating this module and now FROZEN (see WORKLOG.md
 * Phase 5.7) — it was deliberately left untouched rather than refactored
 * to use this shared module, to keep the Chart freeze literal (zero byte
 * changes). New consumers (Logs, and anything future) should use this
 * module instead of writing a third copy. Tracked as TD-20.
 *
 * Usage
 *   calendar_util_init_reference();               // once, at screen-create time
 *   char buf[32];
 *   calendar_util_format_datetime(record.timestamp_s, buf, sizeof(buf));
 * ======================================================================= */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;
    uint8_t  month;    /* 1-12 */
    uint8_t  day;      /* 1-31 */
    uint8_t  hour;     /* 0-23 */
    uint8_t  minute;   /* 0-59 */
} calendar_datetime_t;

/**
 * Anchors "today" to the current boot time, via a fixed placeholder
 * reference date (see calendar_util.c). Call once, before the first
 * conversion — safe to call once per screen (each caller's anchor will be
 * computed microseconds apart from any other caller's, which is
 * inconsequential for a coarse placeholder pending real RTC/SNTP).
 */
void calendar_util_init_reference(void);

/** Converts a boot-relative seconds timestamp to a calendar date/time. */
void calendar_util_boot_ts_to_datetime(uint32_t boot_ts, calendar_datetime_t *out);

/** Formats e.g. "24 May, 8:25 AM" into buf. */
void calendar_util_format_datetime(uint32_t boot_ts, char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
