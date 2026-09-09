#include "dialog_document_info.hpp"

#include "lvgl.h"

namespace ui {

void dialog_document_info_show(app::AppController *controller)
{
    if (!controller || !controller->active_document()) {
        return;
    }
    const auto &manifest = controller->active_document()->manifest();

    lv_obj_t *dlg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(dlg, 380, 240);
    lv_obj_center(dlg);

    char buf[512];
    snprintf(buf, sizeof(buf),
             "%s\n\nID: %s\nPages: %d\nTile: %d px JPEG\nAuthors: %zu",
             manifest.title.c_str(),
             manifest.document_id.c_str(),
             manifest.page_count,
             manifest.tile_size,
             manifest.authors.size());

    lv_obj_t *lbl = lv_label_create(dlg);
    lv_label_set_text(lbl, buf);
    lv_obj_set_width(lbl, 340);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *close = lv_button_create(dlg);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_t *close_lbl = lv_label_create(close);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_add_event_cb(
        close,
        [](lv_event_t *e) {
            lv_obj_delete(lv_obj_get_parent(static_cast<lv_obj_t *>(lv_event_get_target(e))));
        },
        LV_EVENT_CLICKED, nullptr);
}

}  // namespace ui
