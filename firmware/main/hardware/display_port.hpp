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

lv_display_t *display_lv_display();

/** Call with the LVGL lock held. Portrait uses 90° software rotation. */
void display_port_set_portrait(bool portrait);

int display_logical_width();
int display_logical_height();

}  // namespace hw
