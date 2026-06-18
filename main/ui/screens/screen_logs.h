#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *screen_logs_create(void);
void      screen_logs_refresh(void);

#ifdef __cplusplus
}
#endif
