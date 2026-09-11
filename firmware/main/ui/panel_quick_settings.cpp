#include "panel_quick_settings.hpp"
#include "config.hpp"
#include "diagnostics/ui_debug.hpp"
#include "theme.hpp"

#include "lvgl.h"

#include <algorithm>

static lv_obj_t *s_panel = nullptr;

namespace ui {

static const char *k_reading_map[] = {"Landscape", "Portrait", ""};

static void on_brightness_changed(lv_event_t *e)
{
    auto *ctrl = static_cast<app::AppController *>(lv_event_get_user_data(e));
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
    int val = lv_slider_get_value(slider);
    ctrl->post_set_brightness(val);
}

static void on_reading_changed(lv_event_t *e)
{
    auto *ctrl = static_cast<app::AppController *>(lv_event_get_user_data(e));
    auto *btnm = static_cast<lv_obj_t *>(lv_event_get_target(e));
    const uint32_t id = lv_buttonmatrix_get_selected_button(btnm);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }
    const auto orientation = (id == 0) ? app::ReadingOrientation::Landscape
                                       : app::ReadingOrientation::Portrait;
    ctrl->post_set_reading_orientation(orientation);
}

static void on_theme_toggle(lv_event_t *e)
{
    (void)e;
    theme_apply(theme_current() == ThemeMode::Light ? ThemeMode::Dark : ThemeMode::Light);
}

static void on_close(lv_event_t *e)
{
    (void)e;
    panel_quick_settings_close();
}

void panel_quick_settings_close()
{
    diag::ui_debug_note("quick settings close");
    if (s_panel) {
        lv_obj_delete(s_panel);
        s_panel = nullptr;
    }
}

static void on_debug_toggle(lv_event_t *e)
{
    auto *ctrl = static_cast<app::AppController *>(lv_event_get_user_data(e));
    lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ctrl->config_mut().debug_overlay = on;
    app::config_save_debug_overlay(on);
}

void panel_quick_settings_show(app::AppController *controller)
{
    diag::ui_debug_note("panel_quick_settings_show ctrl=%p existing=%p",
                        (void *)controller, (void *)s_panel);
    if (!controller) {
        diag::ui_debug_note("abort: controller is null");
        return;
    }
    if (s_panel) {
        lv_obj_delete(s_panel);
        s_panel = nullptr;
    }

    lv_obj_t *top = lv_layer_top();
    diag::ui_debug_note("layer_top=%p clickable=%d bg_opa=%u children=%u",
                        (void *)top,
                        top ? (int)lv_obj_has_flag(top, LV_OBJ_FLAG_CLICKABLE) : -1,
                        top ? (unsigned)lv_obj_get_style_bg_opa(top, LV_PART_MAIN) : 0,
                        top ? (unsigned)lv_obj_get_child_count(top) : 0);

    s_panel = lv_obj_create(top);
    const int hor = lv_display_get_horizontal_resolution(nullptr);
    const int panel_w = std::min(320, std::max(280, hor - 24));
    lv_obj_set_size(s_panel, panel_w, 330);
    lv_obj_center(s_panel);
    lv_obj_set_style_bg_color(s_panel, theme_color_bg(), 0);

    lv_obj_t *title = lv_label_create(s_panel);
    lv_label_set_text(title, "Quick Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *slider = lv_slider_create(s_panel);
    lv_obj_set_width(slider, panel_w - 60);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 40);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, controller->config().brightness_percent, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, on_brightness_changed, LV_EVENT_VALUE_CHANGED, controller);

    lv_obj_t *reading_lbl = lv_label_create(s_panel);
    lv_label_set_text(reading_lbl, "Orientation");
    lv_obj_set_style_text_color(reading_lbl, theme_color_muted(), 0);
    lv_obj_align(reading_lbl, LV_ALIGN_TOP_LEFT, 16, 78);

    lv_obj_t *reading = lv_buttonmatrix_create(s_panel);
    lv_buttonmatrix_set_map(reading, k_reading_map);
    lv_obj_set_size(reading, panel_w - 40, 42);
    lv_obj_align(reading, LV_ALIGN_TOP_MID, 0, 98);
    lv_obj_set_style_pad_all(reading, 4, 0);
    lv_obj_set_style_pad_column(reading, 6, 0);
    lv_obj_set_style_bg_color(reading, theme_color_surface(), 0);
    lv_obj_set_style_border_color(reading, theme_color_border(), 0);
    lv_obj_set_style_bg_color(reading, theme_color_accent(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(reading, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_buttonmatrix_set_button_ctrl_all(reading, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(reading, true);
    const uint32_t selected =
        controller->config().orientation == app::ReadingOrientation::Landscape ? 0 : 1;
    lv_buttonmatrix_set_button_ctrl(reading, selected, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_add_event_cb(reading, on_reading_changed, LV_EVENT_VALUE_CHANGED, controller);

    lv_obj_t *theme_btn = lv_button_create(s_panel);
    lv_obj_align(theme_btn, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_t *theme_lbl = lv_label_create(theme_btn);
    lv_label_set_text(theme_lbl, "Toggle theme");
    lv_obj_add_event_cb(theme_btn, on_theme_toggle, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *debug_sw = lv_switch_create(s_panel);
    lv_obj_align(debug_sw, LV_ALIGN_TOP_MID, 0, 198);
    if (controller->config().debug_overlay) {
        lv_obj_add_state(debug_sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(debug_sw, on_debug_toggle, LV_EVENT_VALUE_CHANGED, controller);
    lv_obj_t *dbg_lbl = lv_label_create(s_panel);
    lv_label_set_text(dbg_lbl, "Debug overlay");
    lv_obj_align_to(dbg_lbl, debug_sw, LV_ALIGN_OUT_LEFT_MID, -8, 0);

    lv_obj_t *note = lv_label_create(s_panel);
    lv_label_set_text(note, "Brightness uses a dim overlay\n(board backlight is on/off only)");
    lv_obj_set_style_text_font(note, &lv_font_montserrat_12, 0);
    lv_obj_align(note, LV_ALIGN_TOP_MID, 0, 236);

    lv_obj_t *close = lv_button_create(s_panel);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_t *close_lbl = lv_label_create(close);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_add_event_cb(close, on_close, LV_EVENT_CLICKED, nullptr);

    lv_obj_update_layout(s_panel);
    diag::ui_debug_watch(s_panel, "quick_settings");
    diag::ui_debug_watch(close, "settings_close");
    diag::ui_debug_note("quick settings shown %dx%d",
                        (int)lv_obj_get_width(s_panel), (int)lv_obj_get_height(s_panel));
}

}  // namespace ui
