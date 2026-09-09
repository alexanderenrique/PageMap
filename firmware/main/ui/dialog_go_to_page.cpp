#include "dialog_go_to_page.hpp"

#include "lvgl.h"
#include <cstdlib>

namespace ui {

struct GoToCtx {
    app::AppController *controller = nullptr;
    lv_obj_t *textarea = nullptr;
    lv_obj_t *dialog = nullptr;
};

static void on_apply(lv_event_t *e)
{
    auto *ctx = static_cast<GoToCtx *>(lv_event_get_user_data(e));
    if (!ctx || !ctx->controller || !ctx->textarea) {
        return;
    }
    int page = atoi(lv_textarea_get_text(ctx->textarea)) - 1;
    auto *model = ctx->controller->active_document();
    if (model && page >= 0 && page < model->manifest().page_count) {
        ctx->controller->post_go_to_page(page);
    }
    lv_obj_t *dlg = ctx->dialog;
    delete ctx;
    lv_obj_delete(dlg);
}

static void on_cancel(lv_event_t *e)
{
    auto *ctx = static_cast<GoToCtx *>(lv_event_get_user_data(e));
    lv_obj_t *dlg = ctx ? ctx->dialog : nullptr;
    delete ctx;
    if (dlg) {
        lv_obj_delete(dlg);
    }
}

void dialog_go_to_page_show(app::AppController *controller)
{
    if (!controller || !controller->active_document()) {
        return;
    }
    const int page_count = controller->active_document()->manifest().page_count;

    auto *ctx = new GoToCtx();
    ctx->controller = controller;
    ctx->dialog = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ctx->dialog, 300, 200);
    lv_obj_center(ctx->dialog);

    lv_obj_t *lbl = lv_label_create(ctx->dialog);
    lv_label_set_text_fmt(lbl, "Go to page (1-%d)", page_count);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 8);

    ctx->textarea = lv_textarea_create(ctx->dialog);
    lv_obj_set_width(ctx->textarea, 200);
    lv_obj_align(ctx->textarea, LV_ALIGN_CENTER, 0, -10);
    lv_textarea_set_one_line(ctx->textarea, true);
    lv_textarea_set_accepted_chars(ctx->textarea, "0123456789");

    lv_obj_t *btn = lv_button_create(ctx->dialog);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 24, -12);
    lv_label_create(btn);
    lv_label_set_text(lv_obj_get_child(btn, 0), "Go");
    lv_obj_add_event_cb(btn, on_apply, LV_EVENT_CLICKED, ctx);

    lv_obj_t *cancel = lv_button_create(ctx->dialog);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_RIGHT, -24, -12);
    lv_label_create(cancel);
    lv_label_set_text(lv_obj_get_child(cancel, 0), "Cancel");
    lv_obj_add_event_cb(cancel, on_cancel, LV_EVENT_CLICKED, ctx);
}

}  // namespace ui
