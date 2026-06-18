#pragma once
#include "sensor_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called once from sensor_manager_init() before the sensor task starts. */
void sensor_backend_init(void);

/* Produce one sensor reading. Called at 1 Hz inside sensor_task().
 * Must populate every field of *out; set sensor_ok=false on I2C/comm error. */
void sensor_backend_sample(sensor_data_t *out);

#ifdef __cplusplus
}
#endif
