#pragma once

#include <stdbool.h>
#include "bsp/m5stack_tab5.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The speaker amplifier, LCD, touch and USB 5 V enables share one IO expander,
 * and each change is a read-modify-write of its output register. Two tasks
 * changing different pins at once can undo each other, so every change after
 * start-up goes through this lock. */
void tab5_board_io_lock(void);
void tab5_board_io_unlock(void);
esp_err_t tab5_feature_enable(bsp_feature_t feature, bool enable);

#ifdef __cplusplus
}
#endif
