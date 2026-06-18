#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *screen_chart_create(void);
void      screen_chart_refresh(void);

#ifdef __cplusplus
}
#endif
