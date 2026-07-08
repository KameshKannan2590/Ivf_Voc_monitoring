#pragma once

/* =======================================================================
 * history_manager.h — Historical sensor data store (Phase 5.3, API revised Phase 5.4)
 *
 * The single source of truth for TVOC / temperature / humidity history.
 * Chart, Logs, statistics, and CSV export (later phases) must all read
 * through this API — none of them may read sensor state directly for
 * historical purposes.
 *
 * Independence:
 *   - Zero dependency on LVGL, or on any screen/UI module.
 *   - Zero dependency on sensor_manager.h or alarm_manager.h — callers pass
 *     plain scalar readings into history_manager_add_sample(); this module
 *     has no compile-time knowledge of sensor_data_t or alarm_entry_t.
 *     (sensor_manager.c is the only caller today; see sensor_task() in
 *     sensor_manager.c for the integration point.)
 *   - Zero notion of "Chart" or any UI mode. Phase 5.4 removed the old
 *     history_period_t (7D/30D/90D) enum — it modeled the Chart screen's
 *     now-deleted period selector directly, which is exactly the kind of
 *     UI-shaped coupling this module must not have. All queries take plain
 *     timestamp ranges instead.
 *
 * Storage model (see history_manager.c for the full write-up):
 *   Samples are pushed at ~1-minute intervals. Every 60 samples are
 *   averaged (with running min/max) into one hourly record in a single
 *   circular buffer sized for 90 days (2160 hourly records, ~59 KB,
 *   allocated from PSRAM). There is only ever this one buffer — daily
 *   aggregates (history_manager_get_daily_aggregates()) are computed
 *   on-demand from it, not stored separately.
 *
 * Timestamps are seconds since boot (esp_timer_get_time() / 1e6). Real
 * wall-clock timestamps arrive once an RTC/SNTP source exists (Phase 7+).
 * Until then, "daily" bucketing means fixed 24-hour windows counting
 * backward from the caller-supplied `to_ts`, not wall-clock midnight.
 *
 * Usage
 *   history_manager_init();                          // once, at boot
 *   history_manager_add_sample(voc, temp_c, hum_pct); // ~once per minute
 *
 *   history_record_t buf[24];
 *   uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000ULL);
 *   uint16_t n = history_manager_get_range(now - 86400, now, buf, 24);   // last 24h, hourly
 *   uint16_t n7 = history_manager_get_daily_aggregates(now - 7*86400, now, buf, 7); // last 7 days
 *   history_stats_t stats;
 *   history_manager_compute_stats(buf, n7, VOC_WARNING_THRESHOLD_PPB, &stats);
 * ======================================================================= */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Shared threshold constant ───────────────────────────────────────────
 * Single source of truth for the "elevated VOC" statistic (the Chart
 * screen's ">150 ppb Days/Hours" card). Deliberately distinct from
 * sensor_manager.c's configurable warn (300 ppb) / alarm (500 ppb)
 * thresholds — this is a fixed, lower "early warning" tier. Any future
 * consumer (Logs, CSV export, Alarm Manager) should reuse this constant
 * rather than hardcoding 150 again. */
#define VOC_WARNING_THRESHOLD_PPB 150.0f

/* ── History record — one bucket, hourly or daily ─────────────────────────
 * The same struct is used for both a single hourly record (from
 * history_manager_get_range()) and a daily rollup (from
 * history_manager_get_daily_aggregates()) — only the bucket width differs.
 * Add new fields at the end to keep this struct easy to extend. */

typedef struct {
    uint32_t timestamp_s;      /* bucket start (hour-start or day-start), boot-relative */
    float    avg_voc_ppb;
    float    min_voc_ppb;
    float    max_voc_ppb;
    float    temperature_c;    /* bucket average */
    float    humidity_pct;     /* bucket average */
    uint8_t  alarm_state;      /* reserved for Phase 8 alarm_manager integration; always 0 today */
} history_record_t;

/* ── Statistics result ────────────────────────────────────────────────────
 * Produced by history_manager_compute_stats() from an array of
 * history_record_t already fetched via get_range()/get_daily_aggregates() —
 * the reducer itself doesn't know or care which. */

typedef struct {
    float    avg_voc_ppb;
    float    min_voc_ppb;
    float    max_voc_ppb;
    uint16_t over_threshold_count;   /* # of input records whose max_voc_ppb > threshold */
    uint16_t sample_count;           /* # of input records the stats were computed over */
} history_stats_t;

/* ── Public API ──────────────────────────────────────────────────────────── */

/**
 * Allocate the history buffer (PSRAM, falls back to internal SRAM) and
 * reset all counters. Must be called once at boot, before the first
 * history_manager_add_sample() call. Safe to call only once.
 */
esp_err_t history_manager_init(void);

/**
 * Record one sensor reading. Intended to be called at a steady ~1-minute
 * cadence (see sensor_task() in sensor_manager.c, which decimates its 1 Hz
 * loop down to this rate). Every 60 calls are averaged (with running
 * min/max) into one hourly record; the timestamp is stamped internally.
 */
void history_manager_add_sample(float voc_ppb, float temperature_c, float humidity_pct);

/**
 * Copy up to `max_count` hourly records whose timestamp falls within
 * [from_ts, to_ts] (inclusive) into `out`, ordered oldest-to-newest.
 * Returns the number of records copied. This is the "raw hourly" query —
 * e.g. a single day's ≤24 hourly points.
 */
uint16_t history_manager_get_range(uint32_t from_ts, uint32_t to_ts,
                                    history_record_t *out, uint16_t max_count);

/**
 * Copy up to `count` hourly records, most-recent-first (out[0] = newest),
 * skipping the newest `skip` records first — e.g. skip=0 for the first
 * page, skip=10 for the next-older page of 10, etc. Returns the number of
 * records actually copied, which is less than `count` once the tail of
 * stored history is reached (0 once `skip` runs past everything stored).
 * O(count) — walks the ring buffer backward from the newest write, unlike
 * get_range()'s O(stored count) forward scan. Built for the Logs screen's
 * "Load More" pagination (Phase 5.8).
 */
uint16_t history_manager_get_latest_n(uint16_t skip, uint16_t count,
                                       history_record_t *out);

/**
 * Copy up to `max_days` daily rollups into `out`, ordered oldest-to-newest.
 * Each output record aggregates the hourly records falling into one
 * 24-hour bucket counting backward from `to_ts` (bucket 0 = the 24h ending
 * at `to_ts`, bucket 1 = the previous 24h, etc. — not wall-clock-aligned;
 * see the file header note on timestamps). `max_days` is capped at 32
 * internally. Days with no underlying hourly data are omitted, so the
 * returned count may be less than `max_days` shortly after boot.
 */
uint16_t history_manager_get_daily_aggregates(uint32_t from_ts, uint32_t to_ts,
                                               history_record_t *out, uint16_t max_days);

/**
 * Number of hourly records currently stored within [from_ts, to_ts].
 * Useful to detect partial history without fetching the records themselves.
 */
uint16_t history_manager_get_count_in_range(uint32_t from_ts, uint32_t to_ts);

/**
 * Reduce an already-fetched array of records (hourly or daily — same
 * struct) into avg/min/max plus a count of records whose max_voc_ppb
 * exceeds `threshold_ppb`. Pure function: no locking, no history_manager
 * state touched. `*out` is zeroed if `records` is NULL or `count` is 0.
 */
void history_manager_compute_stats(const history_record_t *records, uint16_t count,
                                    float threshold_ppb, history_stats_t *out);

/**
 * Copy the single most recent reading passed to history_manager_add_sample()
 * into `*out`. This is the raw per-minute sample (avg == min == max == the
 * instantaneous reading), not an hourly average.
 * Returns false (and leaves *out untouched) if no sample has been added yet.
 */
bool history_manager_get_latest(history_record_t *out);

/**
 * Discard all stored history and the in-progress hourly accumulator.
 * The underlying buffer allocation is kept (not freed) for reuse.
 */
void history_manager_clear(void);

#ifdef __cplusplus
}
#endif
