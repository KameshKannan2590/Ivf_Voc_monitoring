#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *screen_splash_create(void);
void      screen_splash_start(void);   /* starts the progress animation */

#ifdef __cplusplus
}
#endif
