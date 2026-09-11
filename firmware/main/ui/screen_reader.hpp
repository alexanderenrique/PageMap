#pragma once

#include "app_controller.hpp"
#include "lvgl.h"

namespace ui {

lv_obj_t *screen_reader_create(app::AppController *controller);
void screen_reader_refresh(lv_obj_t *screen, app::AppController *controller);
void screen_reader_invalidate_canvas(lv_obj_t *screen);

}  // namespace ui
