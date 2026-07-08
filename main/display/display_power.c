#include "display_power.h"
#include "display_driver.h"
#include "data/config_manager.h"
#include "data/alarm_manager.h"

#include "esp_timer.h"

static uint16_t s_timeout_sec    = 0;
static uint8_t  s_brightness_pct = 100;
static bool     s_dimmed         = false;
static int64_t  s_last_activity_us = 0;

static void apply_awake_brightness(void)
{
    display_set_brightness(s_brightness_pct);
}

void display_power_init(void)
{
    s_brightness_pct   = config_manager_get_brightness_pct();
    s_timeout_sec      = config_manager_get_timeout_sec();
    s_dimmed           = false;
    s_last_activity_us = esp_timer_get_time();
    apply_awake_brightness();
}

void display_power_notify_touch(void)
{
    s_last_activity_us = esp_timer_get_time();
}

bool display_power_is_dimmed(void)
{
    return s_dimmed;
}

void display_power_wake(void)
{
    s_dimmed           = false;
    s_last_activity_us = esp_timer_get_time();
    apply_awake_brightness();
}

void display_power_tick(void)
{
    if (s_timeout_sec == CONFIG_TIMEOUT_NONE) {
        if (s_dimmed) display_power_wake();
        return;
    }

    if (alarm_manager_active_count() > 0) {
        /* Never dim during an unacknowledged alarm; if it fired while
         * already dimmed, wake immediately. Keep resetting the idle clock
         * so the timeout counts from when the alarm clears, not from
         * whenever the user last touched the screen before it fired. */
        if (s_dimmed) display_power_wake();
        else          s_last_activity_us = esp_timer_get_time();
        return;
    }

    if (!s_dimmed) {
        int64_t idle_us = esp_timer_get_time() - s_last_activity_us;
        if (idle_us >= (int64_t)s_timeout_sec * 1000000LL) {
            s_dimmed = true;
            display_set_brightness(CONFIG_DIM_BRIGHTNESS_PCT);
        }
    }
}

void display_power_reload_settings(void)
{
    s_brightness_pct = config_manager_get_brightness_pct();
    s_timeout_sec    = config_manager_get_timeout_sec();
    /* If currently dimmed, stay at the dim level until the next wake —
     * only the awake brightness changes live. */
    if (!s_dimmed) apply_awake_brightness();
}
