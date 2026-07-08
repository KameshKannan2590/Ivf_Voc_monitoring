#include "calendar_util.h"

#include "esp_timer.h"
#include <stdio.h>

/* Placeholder "today" until RTC/SNTP exists (Phase 7) — matches the
 * approximate date this module was written. Only these three constants
 * need to change once a real time source is available. */
#define CAL_REF_YEAR  2026
#define CAL_REF_MONTH 7
#define CAL_REF_DAY   6

static const char *MONTH_ABBR[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static int64_t s_epoch_offset_s = 0;   /* unix_seconds_at(boot_ts=0) */

/* Howard Hinnant's public-domain days-since-1970-01-01 algorithm. */
static int64_t days_from_civil(int32_t y, uint32_t m, uint32_t d)
{
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);
    uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, uint16_t *y, uint8_t *m, uint8_t *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t  yr  = (int64_t)yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp  = (5 * doy + 2) / 153;
    *d = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
    *m = (uint8_t)(mp + (mp < 10 ? 3 : -9));
    yr += (*m <= 2);
    *y = (uint16_t)yr;
}

void calendar_util_init_reference(void)
{
    int64_t  ref_unix_s = days_from_civil(CAL_REF_YEAR, CAL_REF_MONTH, CAL_REF_DAY) * 86400;
    uint32_t boot_now_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    s_epoch_offset_s = ref_unix_s - (int64_t)boot_now_s;
}

void calendar_util_boot_ts_to_datetime(uint32_t boot_ts, calendar_datetime_t *out)
{
    int64_t unix_s = (int64_t)boot_ts + s_epoch_offset_s;
    int64_t days   = unix_s >= 0 ? unix_s / 86400 : (unix_s - 86399) / 86400;   /* floor div */
    int64_t sod    = unix_s - days * 86400;                                    /* [0, 86399] */

    uint16_t y; uint8_t m, d;
    civil_from_days(days, &y, &m, &d);
    out->year   = y;
    out->month  = m;
    out->day    = d;
    out->hour   = (uint8_t)(sod / 3600);
    out->minute = (uint8_t)((sod % 3600) / 60);
}

void calendar_util_format_datetime(uint32_t boot_ts, char *buf, size_t buf_len)
{
    calendar_datetime_t dt;
    calendar_util_boot_ts_to_datetime(boot_ts, &dt);

    uint8_t h12 = dt.hour % 12;
    if (h12 == 0) h12 = 12;

    snprintf(buf, buf_len, "%s %d, %d:%02u %s",
             MONTH_ABBR[dt.month - 1], (int)dt.day, (int)h12, dt.minute,
             dt.hour >= 12 ? "PM" : "AM");
}
