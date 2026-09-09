#include "theme.hpp"

namespace ui {

static ThemeMode s_mode = ThemeMode::Light;

void theme_apply(ThemeMode mode)
{
    s_mode = mode;
    lv_obj_t *scr = lv_screen_active();
    if (!scr) {
        return;
    }
    lv_obj_set_style_bg_color(scr, theme_color_bg(), 0);
    lv_obj_set_style_text_color(scr, theme_color_text(), 0);
}

ThemeMode theme_current()
{
    return s_mode;
}

lv_color_t theme_color_bg()
{
    return s_mode == ThemeMode::Dark ? lv_color_hex(0x1A1A1A) : lv_color_hex(0xF4F4F4);
}

lv_color_t theme_color_text()
{
    return s_mode == ThemeMode::Dark ? lv_color_hex(0xEEEEEE) : lv_color_hex(0x202020);
}

lv_color_t theme_color_muted()
{
    return s_mode == ThemeMode::Dark ? lv_color_hex(0x9A9A9A) : lv_color_hex(0x6B6B6B);
}

lv_color_t theme_color_surface()
{
    return s_mode == ThemeMode::Dark ? lv_color_hex(0x2A2A2A) : lv_color_hex(0xFFFFFF);
}

lv_color_t theme_color_border()
{
    return s_mode == ThemeMode::Dark ? lv_color_hex(0x3A3A3A) : lv_color_hex(0xD6D6D6);
}

lv_color_t theme_color_accent()
{
    return lv_color_hex(0x2563EB);
}

}  // namespace ui
