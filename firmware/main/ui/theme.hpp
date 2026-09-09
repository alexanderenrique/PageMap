#pragma once

#include "lvgl.h"

namespace ui {

enum class ThemeMode {
    Light,
    Dark,
};

void theme_apply(ThemeMode mode);
ThemeMode theme_current();
lv_color_t theme_color_bg();
lv_color_t theme_color_text();
lv_color_t theme_color_muted();
lv_color_t theme_color_surface();
lv_color_t theme_color_border();
lv_color_t theme_color_accent();

}  // namespace ui
