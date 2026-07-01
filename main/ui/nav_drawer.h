#pragma once

#include "lvgl.h"
#include "ui.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize nav drawer on lv_layer_top() — call once from ui_init()
 * after all screens are created. */
void nav_drawer_init(void);

void nav_drawer_open(void);
void nav_drawer_close(void);
void nav_drawer_toggle(void);

/* Highlight the active screen row — call from ui_goto_screen(). */
void nav_drawer_set_active(screen_id_t screen);

bool nav_drawer_is_open(void);

#ifdef __cplusplus
}
#endif
