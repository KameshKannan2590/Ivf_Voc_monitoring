#include "history_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include <float.h>

static const char *TAG = "history_mgr";

/* =======================================================================
 * history_manager.c — single circular buffer, queried by timestamp range
 *
 * One PSRAM-backed ring buffer of hourly records covers the full 90-day
 * horizon. Phase 5.4 removed the old "period" concept entirely — every
 * query takes a plain [from_ts, to_ts] range; daily aggregates are grouped
 * from the same buffer at read time, not stored separately.
 *
 * Two small accumulators sit in front of the ring buffer:
 *   - s_latest      : the single most recent raw (~1/minute) reading.
 *   - s_accum_*     : running sum + min/max of the in-progress hour; every
 *                     HISTORY_SAMPLES_PER_HOUR calls to
 *                     history_manager_add_sample() it is finalized into
 *                     one record and pushed into the ring buffer.
 * Neither accumulator stores individual per-minute records — only running
 * values — which is why the module's RAM footprint is dominated entirely
 * by the ring buffer.
 * ======================================================================= */

#define HISTORY_HOURLY_CAPACITY   (90 * 24)   /* 2160 — full 90-day buffer   */
#define HISTORY_SAMPLES_PER_HOUR  60          /* 1-minute samples per hour  */
#define HISTORY_SECONDS_PER_DAY   86400u
#define HISTORY_MAX_DAILY_BUCKETS 32          /* safety ceiling for get_daily_aggregates() */

/* ── Ring buffer state ────────────────────────────────────────────────────
 * s_head = index of the NEXT write slot. Valid entries (oldest-to-newest)
 * occupy the CAPACITY slots ending at s_head-1, wrapping as needed. */
static history_record_t *s_hourly = NULL;
static uint16_t          s_head   = 0;
static uint16_t          s_count  = 0;   /* valid entries, capped at CAPACITY */

/* ── In-progress hourly accumulator ──────────────────────────────────────── */
static uint32_t s_accum_start_ts = 0;
static uint16_t s_accum_count    = 0;
static float    s_accum_sum_voc  = 0.0f;
static float    s_accum_sum_temp = 0.0f;
static float    s_accum_sum_hum  = 0.0f;
static float    s_accum_min_voc  = 0.0f;
static float    s_accum_max_voc  = 0.0f;

/* ── Latest raw reading cache ────────────────────────────────────────────── */
static history_record_t s_latest      = {0};
static bool              s_has_latest = false;

static SemaphoreHandle_t s_mutex = NULL;

/* ── Helpers ──────────────────────────────────────────────────────────────── */

/* Index of the oldest valid entry. Caller must hold s_mutex. */
static uint16_t ring_start(void)
{
    return (uint16_t)((s_head + HISTORY_HOURLY_CAPACITY - s_count) % HISTORY_HOURLY_CAPACITY);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

esp_err_t history_manager_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    size_t bytes = (size_t)HISTORY_HOURLY_CAPACITY * sizeof(history_record_t);

    bool used_psram = true;
    s_hourly = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!s_hourly) {
        used_psram = false;
        s_hourly = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }
    if (!s_hourly) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes for history buffer", (unsigned)bytes);
        return ESP_ERR_NO_MEM;
    }

    s_head = 0;
    s_count = 0;
    s_accum_count = 0;
    s_accum_sum_voc = s_accum_sum_temp = s_accum_sum_hum = 0.0f;
    s_accum_min_voc = s_accum_max_voc = 0.0f;
    s_has_latest = false;
    memset(&s_latest, 0, sizeof(s_latest));

    ESP_LOGI(TAG, "History manager initialized: %d hourly slots (%u bytes) in %s",
             HISTORY_HOURLY_CAPACITY, (unsigned)bytes, used_psram ? "PSRAM" : "internal SRAM");
    return ESP_OK;
}

void history_manager_add_sample(float voc_ppb, float temperature_c, float humidity_pct)
{
    if (!s_hourly || !s_mutex) return;

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_latest.timestamp_s   = now;
    s_latest.avg_voc_ppb   = voc_ppb;
    s_latest.min_voc_ppb   = voc_ppb;
    s_latest.max_voc_ppb   = voc_ppb;
    s_latest.temperature_c = temperature_c;
    s_latest.humidity_pct  = humidity_pct;
    s_latest.alarm_state   = 0;
    s_has_latest = true;

    if (s_accum_count == 0) {
        s_accum_start_ts = now;
        s_accum_min_voc  = voc_ppb;
        s_accum_max_voc  = voc_ppb;
    } else {
        if (voc_ppb < s_accum_min_voc) s_accum_min_voc = voc_ppb;
        if (voc_ppb > s_accum_max_voc) s_accum_max_voc = voc_ppb;
    }
    s_accum_sum_voc  += voc_ppb;
    s_accum_sum_temp += temperature_c;
    s_accum_sum_hum  += humidity_pct;
    s_accum_count++;

    if (s_accum_count >= HISTORY_SAMPLES_PER_HOUR) {
        s_hourly[s_head].timestamp_s   = s_accum_start_ts;
        s_hourly[s_head].avg_voc_ppb   = s_accum_sum_voc  / s_accum_count;
        s_hourly[s_head].min_voc_ppb   = s_accum_min_voc;
        s_hourly[s_head].max_voc_ppb   = s_accum_max_voc;
        s_hourly[s_head].temperature_c = s_accum_sum_temp / s_accum_count;
        s_hourly[s_head].humidity_pct  = s_accum_sum_hum  / s_accum_count;
        s_hourly[s_head].alarm_state   = 0;

        s_head = (uint16_t)((s_head + 1) % HISTORY_HOURLY_CAPACITY);
        if (s_count < HISTORY_HOURLY_CAPACITY) s_count++;

        s_accum_count    = 0;
        s_accum_sum_voc  = 0.0f;
        s_accum_sum_temp = 0.0f;
        s_accum_sum_hum  = 0.0f;
        s_accum_min_voc  = 0.0f;
        s_accum_max_voc  = 0.0f;
    }

    xSemaphoreGive(s_mutex);
}

uint16_t history_manager_get_range(uint32_t from_ts, uint32_t to_ts,
                                    history_record_t *out, uint16_t max_count)
{
    if (!s_hourly || !out || max_count == 0 || from_ts > to_ts) return 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint16_t start  = ring_start();
    uint16_t copied = 0;
    for (uint16_t i = 0; i < s_count && copied < max_count; i++) {
        const history_record_t *r = &s_hourly[(start + i) % HISTORY_HOURLY_CAPACITY];
        if (r->timestamp_s >= from_ts && r->timestamp_s <= to_ts) {
            out[copied++] = *r;
        }
    }
    xSemaphoreGive(s_mutex);

    return copied;
}

uint16_t history_manager_get_latest_n(uint16_t skip, uint16_t count, history_record_t *out)
{
    if (!s_hourly || !out || count == 0) return 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint16_t available = (skip < s_count) ? (uint16_t)(s_count - skip) : 0;
    uint16_t n = (count < available) ? count : available;
    for (uint16_t i = 0; i < n; i++) {
        /* Newest valid entry sits at (s_head - 1); walk backward from there. */
        uint16_t idx = (uint16_t)((s_head + HISTORY_HOURLY_CAPACITY - 1 - skip - i) % HISTORY_HOURLY_CAPACITY);
        out[i] = s_hourly[idx];
    }
    xSemaphoreGive(s_mutex);

    return n;
}

uint16_t history_manager_get_daily_aggregates(uint32_t from_ts, uint32_t to_ts,
                                               history_record_t *out, uint16_t max_days)
{
    if (!s_hourly || !out || max_days == 0 || from_ts > to_ts) return 0;
    if (max_days > HISTORY_MAX_DAILY_BUCKETS) max_days = HISTORY_MAX_DAILY_BUCKETS;

    typedef struct {
        uint32_t count;
        float    sum_voc, min_voc, max_voc, sum_temp, sum_hum;
        bool     seen;
    } bucket_t;

    bucket_t buckets[HISTORY_MAX_DAILY_BUCKETS];
    memset(buckets, 0, sizeof(buckets));
    for (uint16_t i = 0; i < max_days; i++) {
        buckets[i].min_voc = FLT_MAX;
        buckets[i].max_voc = -FLT_MAX;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint16_t start = ring_start();
    for (uint16_t i = 0; i < s_count; i++) {
        const history_record_t *r = &s_hourly[(start + i) % HISTORY_HOURLY_CAPACITY];
        if (r->timestamp_s < from_ts || r->timestamp_s > to_ts) continue;

        /* Bucket 0 = the 24h ending at to_ts, bucket 1 = the previous 24h, etc. —
         * relative to to_ts, not wall-clock midnight (no RTC yet). */
        uint32_t age = to_ts - r->timestamp_s;
        uint16_t day_index = (uint16_t)(age / HISTORY_SECONDS_PER_DAY);
        if (day_index >= max_days) continue;

        bucket_t *b = &buckets[day_index];
        b->count++;
        b->sum_voc  += r->avg_voc_ppb;
        b->sum_temp += r->temperature_c;
        b->sum_hum  += r->humidity_pct;
        if (r->min_voc_ppb < b->min_voc) b->min_voc = r->min_voc_ppb;
        if (r->max_voc_ppb > b->max_voc) b->max_voc = r->max_voc_ppb;
        b->seen = true;
    }
    xSemaphoreGive(s_mutex);

    /* Emit oldest-to-newest: highest day_index (oldest) down to 0 (most recent). */
    uint16_t n = 0;
    for (int16_t day_index = (int16_t)max_days - 1; day_index >= 0; day_index--) {
        bucket_t *b = &buckets[day_index];
        if (!b->seen) continue;

        out[n].timestamp_s   = to_ts - (uint32_t)day_index * HISTORY_SECONDS_PER_DAY;
        out[n].avg_voc_ppb   = b->sum_voc / b->count;
        out[n].min_voc_ppb   = b->min_voc;
        out[n].max_voc_ppb   = b->max_voc;
        out[n].temperature_c = b->sum_temp / b->count;
        out[n].humidity_pct  = b->sum_hum / b->count;
        out[n].alarm_state   = 0;
        n++;
    }

    return n;
}

uint16_t history_manager_get_count_in_range(uint32_t from_ts, uint32_t to_ts)
{
    if (!s_hourly || from_ts > to_ts) return 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint16_t start = ring_start();
    uint16_t count = 0;
    for (uint16_t i = 0; i < s_count; i++) {
        const history_record_t *r = &s_hourly[(start + i) % HISTORY_HOURLY_CAPACITY];
        if (r->timestamp_s >= from_ts && r->timestamp_s <= to_ts) count++;
    }
    xSemaphoreGive(s_mutex);

    return count;
}

void history_manager_compute_stats(const history_record_t *records, uint16_t count,
                                    float threshold_ppb, history_stats_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!records || count == 0) return;

    float    sum   = 0.0f;
    float    min_v = records[0].min_voc_ppb;
    float    max_v = records[0].max_voc_ppb;
    uint16_t over  = 0;

    for (uint16_t i = 0; i < count; i++) {
        sum += records[i].avg_voc_ppb;
        if (records[i].min_voc_ppb < min_v) min_v = records[i].min_voc_ppb;
        if (records[i].max_voc_ppb > max_v) max_v = records[i].max_voc_ppb;
        if (records[i].max_voc_ppb > threshold_ppb) over++;
    }

    out->avg_voc_ppb          = sum / count;
    out->min_voc_ppb          = min_v;
    out->max_voc_ppb          = max_v;
    out->over_threshold_count = over;
    out->sample_count         = count;
}

bool history_manager_get_latest(history_record_t *out)
{
    if (!out || !s_mutex) return false;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool has = s_has_latest;
    if (has) *out = s_latest;
    xSemaphoreGive(s_mutex);

    return has;
}

void history_manager_clear(void)
{
    if (!s_mutex) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_head  = 0;
    s_count = 0;
    s_accum_count    = 0;
    s_accum_sum_voc  = 0.0f;
    s_accum_sum_temp = 0.0f;
    s_accum_sum_hum  = 0.0f;
    s_accum_min_voc  = 0.0f;
    s_accum_max_voc  = 0.0f;
    s_has_latest = false;
    memset(&s_latest, 0, sizeof(s_latest));
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "History cleared");
}
