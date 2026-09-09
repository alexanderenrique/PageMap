#pragma once

#include "lvgl.h"

namespace diag {

lv_obj_t *debug_overlay_create(lv_obj_t *parent);
void debug_overlay_update(lv_obj_t *label);

}  // namespace diag
