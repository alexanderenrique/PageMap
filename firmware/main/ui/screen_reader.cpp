#include "screen_reader.hpp"
#include "diagnostics/debug_overlay.hpp"
#include "diagnostics/ui_debug.hpp"
#include "dialog_document_info.hpp"
#include "dialog_go_to_page.hpp"
#include "hardware/backlight.hpp"
#include "hardware/board_config.hpp"
#include "panel_quick_settings.hpp"
#include "reader/gesture_controller.hpp"
#include "reader/page_canvas.hpp"
#include "reader/tile_key.hpp"
#include "theme.hpp"

#include <algorithm>

namespace ui {

struct ReaderUi {
    lv_obj_t *canvas = nullptr;
    lv_obj_t *top_bar = nullptr;
    lv_obj_t *bottom_bar = nullptr;
    lv_obj_t *title_lbl = nullptr;
    lv_obj_t *page_lbl = nullptr;
    lv_obj_t *debug_lbl = nullptr;
};

static void on_back(lv_event_t *e)
{
    auto *ctrl = static_cast<app::AppController *>(lv_event_get_user_data(e));
    ctrl->post_close_document();
}

static void on_fit_page(lv_event_t *e)
{
    auto *ctrl = static_cast<app::AppController *>(lv_event_get_user_data(e));
    ctrl->post_set_fit_mode(reader::FitMode::Page);
}

static void on_fit_width(lv_event_t *e)
{
    auto *ctrl = static_cast<app::AppController *>(lv_event_get_user_data(e));
    ctrl->post_set_fit_mode(reader::FitMode::Width);
}

static void on_prev_page(lv_event_t *e)
{
    auto *ctrl = static_cast<app::AppController *>(lv_event_get_user_data(e));
    ctrl->post_previous_view();
}

static void on_next_page(lv_event_t *e)
{
    auto *ctrl = static_cast<app::AppController *>(lv_event_get_user_data(e));
    ctrl->post_next_view();
}

static void on_brightness(lv_event_t *e)
{
    auto *ctrl = static_cast<app::AppController *>(lv_event_get_user_data(e));
    diag::ui_debug_note("reader brightness/settings CLICKED");
    panel_quick_settings_show(ctrl);
}

static void zoom_at_center(app::AppController *ctrl, float factor)
{
    auto &vp = ctrl->viewport();
    const int sx = board::LCD_H_RES / 2;
    const int sy = board::LCD_V_RES / 2;
    float doc_x = 0, doc_y = 0;
    vp.screen_to_doc(static_cast<float>(sx), static_cast<float>(sy), &doc_x, &doc_y);
    const float scale = std::clamp(vp.state().display_scale * factor, 0.05f, 8.0f);
    ctrl->post_set_zoom(scale, doc_x, doc_y, sx, sy);
}

static void on_zoom_in(lv_event_t *e)
{
    zoom_at_center(static_cast<app::AppController *>(lv_event_get_user_data(e)), 1.25f);
}

static void on_zoom_out(lv_event_t *e)
{
    zoom_at_center(static_cast<app::AppController *>(lv_event_get_user_data(e)), 0.8f);
}

static void on_goto_page(lv_event_t *e)
{
    dialog_go_to_page_show(static_cast<app::AppController *>(lv_event_get_user_data(e)));
}

static void on_doc_info(lv_event_t *e)
{
    dialog_document_info_show(static_cast<app::AppController *>(lv_event_get_user_data(e)));
}

static void set_chrome_visible(ReaderUi *ui, bool visible)
{
    lv_opa_t opa = visible ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_obj_set_style_bg_opa(ui->top_bar, visible ? LV_OPA_80 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(ui->bottom_bar, visible ? LV_OPA_80 : LV_OPA_TRANSP, 0);
    if (ui->title_lbl) {
        lv_obj_set_style_opa(ui->title_lbl, opa, 0);
    }
    if (ui->page_lbl) {
        lv_obj_set_style_opa(ui->page_lbl, opa, 0);
    }
}

lv_obj_t *screen_reader_create(app::AppController *controller)
{
    auto *ui = new ReaderUi();
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_user_data(scr, ui);

    ui->canvas = reader::page_canvas_create(scr);

    ui->top_bar = lv_obj_create(scr);
    lv_obj_set_size(ui->top_bar, LV_PCT(100), 48);
    lv_obj_align(ui->top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(ui->top_bar, theme_color_bg(), 0);
    lv_obj_set_style_border_width(ui->top_bar, 0, 0);
    lv_obj_set_style_radius(ui->top_bar, 0, 0);

    lv_obj_t *back = lv_button_create(ui->top_bar);
    lv_obj_set_size(back, 40, 36);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, controller);

    ui->title_lbl = lv_label_create(ui->top_bar);
    lv_label_set_text(ui->title_lbl, "Document");
    lv_label_set_long_mode(ui->title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ui->title_lbl, 420);
    lv_obj_align(ui->title_lbl, LV_ALIGN_CENTER, 0, 0);

    ui->page_lbl = lv_label_create(ui->top_bar);
    lv_label_set_text(ui->page_lbl, "1/1");
    lv_obj_align(ui->page_lbl, LV_ALIGN_RIGHT_MID, -52, 0);

    lv_obj_t *menu = lv_button_create(ui->top_bar);
    lv_obj_set_size(menu, 40, 36);
    lv_obj_align(menu, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_label_create(menu);
    lv_label_set_text(lv_obj_get_child(menu, 0), LV_SYMBOL_LIST);
    lv_obj_add_event_cb(menu, on_doc_info, LV_EVENT_CLICKED, controller);

    ui->bottom_bar = lv_obj_create(scr);
    lv_obj_set_size(ui->bottom_bar, LV_PCT(100), 52);
    lv_obj_align(ui->bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ui->bottom_bar, theme_color_bg(), 0);
    lv_obj_set_style_border_width(ui->bottom_bar, 0, 0);

    lv_obj_t *prev = lv_button_create(ui->bottom_bar);
    lv_obj_align(prev, LV_ALIGN_LEFT_MID, 8, 0);
    lv_label_create(prev);
    lv_label_set_text(lv_obj_get_child(prev, 0), LV_SYMBOL_PREV);
    lv_obj_add_event_cb(prev, on_prev_page, LV_EVENT_CLICKED, controller);

    lv_obj_t *next = lv_button_create(ui->bottom_bar);
    lv_obj_align(next, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_label_create(next);
    lv_label_set_text(lv_obj_get_child(next, 0), LV_SYMBOL_NEXT);
    lv_obj_add_event_cb(next, on_next_page, LV_EVENT_CLICKED, controller);

    lv_obj_t *zoom_out = lv_button_create(ui->bottom_bar);
    lv_obj_align(zoom_out, LV_ALIGN_CENTER, -140, 0);
    lv_label_create(zoom_out);
    lv_label_set_text(lv_obj_get_child(zoom_out, 0), LV_SYMBOL_MINUS);
    lv_obj_add_event_cb(zoom_out, on_zoom_out, LV_EVENT_CLICKED, controller);

    lv_obj_t *zoom_in = lv_button_create(ui->bottom_bar);
    lv_obj_align(zoom_in, LV_ALIGN_CENTER, -100, 0);
    lv_label_create(zoom_in);
    lv_label_set_text(lv_obj_get_child(zoom_in, 0), LV_SYMBOL_PLUS);
    lv_obj_add_event_cb(zoom_in, on_zoom_in, LV_EVENT_CLICKED, controller);

    lv_obj_t *fit_w = lv_button_create(ui->bottom_bar);
    lv_obj_align(fit_w, LV_ALIGN_CENTER, -40, 0);
    lv_label_create(fit_w);
    lv_label_set_text(lv_obj_get_child(fit_w, 0), "W");
    lv_obj_add_event_cb(fit_w, on_fit_width, LV_EVENT_CLICKED, controller);

    lv_obj_t *fit_p = lv_button_create(ui->bottom_bar);
    lv_obj_align(fit_p, LV_ALIGN_CENTER, 10, 0);
    lv_label_create(fit_p);
    lv_label_set_text(lv_obj_get_child(fit_p, 0), "P");
    lv_obj_add_event_cb(fit_p, on_fit_page, LV_EVENT_CLICKED, controller);

    lv_obj_t *goto_btn = lv_button_create(ui->bottom_bar);
    lv_obj_align(goto_btn, LV_ALIGN_CENTER, 60, 0);
    lv_label_create(goto_btn);
    lv_label_set_text(lv_obj_get_child(goto_btn, 0), "#");
    lv_obj_add_event_cb(goto_btn, on_goto_page, LV_EVENT_CLICKED, controller);

    lv_obj_t *bright = lv_button_create(ui->bottom_bar);
    lv_obj_align(bright, LV_ALIGN_CENTER, 110, 0);
    lv_label_create(bright);
    lv_label_set_text(lv_obj_get_child(bright, 0), LV_SYMBOL_IMAGE);
    lv_obj_add_event_cb(bright, on_brightness, LV_EVENT_CLICKED, controller);

    if (controller->config().debug_overlay) {
        ui->debug_lbl = diag::debug_overlay_create(scr);
    }

    reader::gesture_controller_attach(ui->canvas, &controller->viewport(), controller);
    diag::ui_debug_watch(scr, "reader");
    screen_reader_refresh(scr, controller);
    return scr;
}

void screen_reader_refresh(lv_obj_t *screen, app::AppController *controller)
{
    if (!screen || !controller) {
        return;
    }
    auto *ui = static_cast<ReaderUi *>(lv_obj_get_user_data(screen));
    auto *model = controller->active_document();
    if (!model || !ui) {
        return;
    }

    if (ui->title_lbl) {
        lv_label_set_text(ui->title_lbl, model->manifest().title.c_str());
    }

    char page_buf[32];
    snprintf(page_buf, sizeof(page_buf), "%d / %d",
             controller->viewport().state().page_index + 1,
             model->manifest().page_count);
    if (ui->page_lbl) {
        lv_label_set_text(ui->page_lbl, page_buf);
    }

    reader::page_canvas_bind(
        ui->canvas,
        &controller->viewport(),
        model,
        &controller->tile_cache(),
        &controller->tile_manager(),
        reader::hash_document_id(model->manifest().document_id),
        model->manifest().tile_size);

    reader::page_canvas_set_dim_overlay(ui->canvas, hw::backlight().dim_overlay_alpha());
    reader::page_canvas_refresh(ui->canvas);

    set_chrome_visible(ui, controller->chrome_visible());

    if (ui->debug_lbl) {
        diag::debug_overlay_update(ui->debug_lbl);
    }
}

}  // namespace ui
