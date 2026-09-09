#pragma once

#include "app_controller.hpp"
#include "viewport.hpp"
#include "lvgl.h"

namespace reader {

void gesture_controller_attach(lv_obj_t *target, Viewport *viewport, app::AppController *controller);

}  // namespace reader
