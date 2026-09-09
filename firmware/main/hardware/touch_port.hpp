#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

namespace hw {

esp_err_t touch_port_init(lv_display_t *display);
esp_lcd_touch_handle_t touch_handle();
lv_indev_t *touch_indev();

}  // namespace hw
