#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "lvgl.h"

namespace hw {

struct DisplayHandles {
    esp_lcd_panel_handle_t panel = nullptr;
    lv_display_t *lv_display = nullptr;
};

esp_err_t display_port_init(DisplayHandles *out);
void display_port_deinit();

}  // namespace hw
