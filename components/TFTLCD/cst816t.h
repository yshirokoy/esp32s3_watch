#ifndef CST816T_H
#define CST816T_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// CST816T touch pins
#define CST816T_SCL_GPIO 17
#define CST816T_SDA_GPIO 16
#define CST816T_RST_GPIO 15
#define CST816T_INT_GPIO 14

// Touch coordinate transform. Change these if the touch direction is wrong.
#define CST816T_SWAP_XY  0
#define CST816T_MIRROR_X 0
#define CST816T_MIRROR_Y 0

esp_err_t cst816t_init(void);
bool cst816t_read_touch(uint16_t *x, uint16_t *y, uint8_t *gesture);

// Start the dedicated touch polling task (20ms interval).
void cst816t_task_start(void);

// Thread-safe: read the latest touch state from LVGL indev callback.
// Returns true if currently pressed.
bool cst816t_get_latest(uint16_t *x, uint16_t *y);

void cst816t_create_slide_demo(void);

#endif
