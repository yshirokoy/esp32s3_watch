#ifndef GAME_2048_H
#define GAME_2048_H

#include "lvgl.h"

lv_obj_t *game_2048_create(lv_obj_t *parent);
void      game_2048_start(void);
bool      game_2048_is_active(void);
void      game_2048_set_lock_cb(void (*cb)(bool lock));

#endif
