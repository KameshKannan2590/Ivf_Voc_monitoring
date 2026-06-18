#pragma once

#include "sensors/sensor_manager.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALARM_MAX_COUNT  32   /* Circular history ring */

typedef enum {
    ALARM_VOC_HIGH     = 0,
    ALARM_TEMP_HIGH    = 1,
    ALARM_TEMP_LOW     = 2,
    ALARM_HUM_HIGH     = 3,
    ALARM_HUM_LOW      = 4,
    ALARM_SENSOR_ERROR = 5,
    ALARM_TYPE_COUNT
} alarm_type_t;

typedef struct {
    alarm_type_t type;
    float        measured_value;
    float        threshold_value;
    uint32_t     timestamp_ms;
    bool         active;
    bool         acknowledged;
} alarm_entry_t;

/**
 * Initialize alarm manager (loads history from NVS if available).
 */
esp_err_t alarm_manager_init(void);

/**
 * Called by sensor_manager each sampling cycle.
 * Evaluates thresholds; raises or clears alarms accordingly.
 */
void alarm_manager_check(const sensor_data_t *data);

/**
 * Returns number of currently active (unacknowledged) alarms.
 */
int alarm_manager_active_count(void);

/**
 * Copy alarm history into caller-supplied buffer (newest first).
 * @param buf     Destination array.
 * @param max_cnt Maximum entries to copy.
 * @return        Number of entries written.
 */
int alarm_manager_get_history(alarm_entry_t *buf, int max_cnt);

/**
 * Acknowledge a specific alarm entry (clears active flag).
 */
void alarm_manager_acknowledge(int index);

/**
 * Acknowledge all active alarms at once.
 */
void alarm_manager_acknowledge_all(void);

/**
 * Clear entire alarm history.
 */
void alarm_manager_clear_history(void);

/**
 * Human-readable alarm type string.
 */
const char *alarm_type_str(alarm_type_t type);

#ifdef __cplusplus
}
#endif
