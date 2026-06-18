#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *screen_dashboard_create(void);
void      screen_dashboard_update(void);

void dashboard_set_time(const char *time_str);
void dashboard_set_date(const char *date_str);

#ifdef __cplusplus
}
#endif
