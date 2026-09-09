#pragma once

#include "app_controller.hpp"
#include "lvgl.h"

namespace ui {

lv_obj_t *screen_library_create(app::AppController *controller);
void screen_library_refresh(lv_obj_t *screen, app::AppController *controller);

}  // namespace ui
