#include "screen_library.hpp"
#include "diagnostics/ui_debug.hpp"
#include "panel_quick_settings.hpp"
#include "reader/jpeg_decoder.hpp"
#include "theme.hpp"

#include "esp_log.h"
#include "sdkconfig.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ui {

static const char *TAG = "library";

constexpr int kCoverW = 188;
constexpr int kCoverH = 244;
constexpr int kCaptionH = 54;
constexpr int kCoverCaptionGap = 8;

struct LibraryCard {
    std::string doc_id;
    std::vector<uint16_t> pixels;
    lv_image_dsc_t dsc{};
};

struct LibraryUi {
    lv_obj_t *grid = nullptr;
    std::vector<LibraryCard *> covers;
};

static void style_reset(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void format_display_title(const std::string &raw, char *out, size_t out_sz)
{
    std::string s = raw;
    for (char &c : s) {
        if (c == '-' || c == '_') {
            c = ' ';
        }
    }

    size_t i = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
    while (i < s.size() && s[i] == ' ') {
        ++i;
    }
    if (i >= s.size()) {
        i = 0;
    }

    snprintf(out, out_sz, "%s", s.c_str() + i);
    if (out[0] != '\0') {
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    }
}

static bool load_cover(const docs::CatalogEntry &entry, LibraryCard *card)
{
    const char *rel =
        entry.manifest.cover_rel.empty() ? "cover.jpg" : entry.manifest.cover_rel.c_str();
    char path[448];
    snprintf(path, sizeof(path), "%s/%s", entry.manifest.package_root.c_str(), rel);

    reader::JpegDecodeResult decoded{};
    if (reader::jpeg_decode_file(path, &decoded) != ESP_OK || decoded.rgb565.empty() ||
        decoded.width <= 0 || decoded.height <= 0) {
        ESP_LOGW(TAG, "cover unavailable: %s", path);
        return false;
    }

    card->pixels = std::move(decoded.rgb565);
    card->dsc = {};
    card->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    card->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    card->dsc.header.w = static_cast<uint32_t>(decoded.width);
    card->dsc.header.h = static_cast<uint32_t>(decoded.height);
    card->dsc.header.stride = static_cast<uint32_t>(decoded.width) * sizeof(uint16_t);
    card->dsc.data_size = static_cast<uint32_t>(card->pixels.size() * sizeof(uint16_t));
    card->dsc.data = reinterpret_cast<const uint8_t *>(card->pixels.data());
    return true;
}

static void on_doc_click(lv_event_t *e)
{
    lv_obj_t *card = lv_event_get_current_target_obj(e);
    auto *card_ctx = static_cast<LibraryCard *>(lv_obj_get_user_data(card));
    auto *ctrl = static_cast<app::AppController *>(lv_event_get_user_data(e));
    if (card_ctx && ctrl && !card_ctx->doc_id.empty()) {
        ctrl->post_open_document(card_ctx->doc_id);
    }
}

static void free_covers(LibraryUi *ui)
{
    if (!ui) {
        return;
    }
    for (LibraryCard *card : ui->covers) {
        delete card;
    }
    ui->covers.clear();
}

static void on_screen_deleted(lv_event_t *e)
{
    lv_obj_t *scr = lv_event_get_current_target_obj(e);
    auto *ui = static_cast<LibraryUi *>(lv_obj_get_user_data(scr));
    lv_obj_set_user_data(scr, nullptr);
    if (!ui) {
        return;
    }
    // Images still exist at LV_EVENT_DELETE; drop widgets first so they
    // no longer reference the RGB565 buffers.
    if (ui->grid) {
        lv_obj_clean(ui->grid);
    }
    free_covers(ui);
    delete ui;
}

static void on_settings_click(lv_event_t *e)
{
    auto *ctrl = static_cast<app::AppController *>(lv_event_get_user_data(e));
    lv_obj_t *btn = lv_event_get_target_obj(e);
    lv_area_t a{};
    if (btn) {
        lv_obj_get_coords(btn, &a);
    }
    diag::ui_debug_note("settings gear CLICKED btn=[%d,%d %dx%d]",
                        (int)a.x1, (int)a.y1,
                        (int)(a.x2 - a.x1 + 1), (int)(a.y2 - a.y1 + 1));
    panel_quick_settings_show(ctrl);
}

static void make_cover_frame(lv_obj_t *card, LibraryCard *card_ctx)
{
    lv_obj_t *frame = lv_obj_create(card);
    style_reset(frame);
    lv_obj_set_size(frame, kCoverW, kCoverH);
    lv_obj_set_style_bg_color(frame, theme_color_surface(), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(frame, 4, 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_border_color(frame, theme_color_border(), 0);
    lv_obj_set_style_clip_corner(frame, true, 0);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);

    if (card_ctx->dsc.data) {
        lv_obj_t *img = lv_image_create(frame);
        lv_obj_remove_style_all(img);
        lv_obj_set_size(img, kCoverW, kCoverH);
        lv_obj_center(img);
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
        lv_image_set_antialias(img, true);
        lv_image_set_src(img, &card_ctx->dsc);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_t *icon = lv_label_create(frame);
        lv_label_set_text(icon, LV_SYMBOL_FILE);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(icon, theme_color_muted(), 0);
        lv_obj_center(icon);
    }
}

static void make_caption(lv_obj_t *card, const docs::CatalogEntry &entry)
{
    lv_obj_t *caption = lv_obj_create(card);
    style_reset(caption);
    lv_obj_set_size(caption, kCoverW, kCaptionH);
    lv_obj_set_style_bg_color(caption, theme_color_surface(), 0);
    lv_obj_set_style_bg_opa(caption, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(caption, 6, 0);
    lv_obj_set_style_pad_left(caption, 8, 0);
    lv_obj_set_style_pad_right(caption, 8, 0);
    lv_obj_set_style_pad_top(caption, 6, 0);
    lv_obj_set_style_pad_bottom(caption, 8, 0);
    lv_obj_remove_flag(caption, LV_OBJ_FLAG_CLICKABLE);

    char title_buf[128];
    format_display_title(entry.manifest.title, title_buf, sizeof(title_buf));

    lv_obj_t *title = lv_label_create(caption);
    lv_label_set_text(title, title_buf);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, kCoverW - 16);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, theme_color_text(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    char meta[48];
    snprintf(meta, sizeof(meta), "%d / %d", entry.current_page + 1, entry.manifest.page_count);
    lv_obj_t *meta_lbl = lv_label_create(caption);
    lv_label_set_text(meta_lbl, meta);
    lv_obj_set_style_text_font(meta_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(meta_lbl, theme_color_muted(), 0);
    lv_obj_align(meta_lbl, LV_ALIGN_TOP_LEFT, 0, 20);

    lv_obj_t *track = lv_obj_create(caption);
    style_reset(track);
    lv_obj_set_size(track, kCoverW - 16, 3);
    lv_obj_set_style_bg_color(track, theme_color_muted(), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_30, 0);
    lv_obj_set_style_radius(track, 2, 0);
    lv_obj_align(track, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);

    int pct = static_cast<int>(entry.progress * 100.0f + 0.5f);
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    if (pct > 0) {
        lv_obj_t *fill = lv_obj_create(track);
        style_reset(fill);
        const int fill_w = ((kCoverW - 16) * pct) / 100;
        lv_obj_set_size(fill, fill_w < 2 ? 2 : fill_w, 3);
        lv_obj_set_style_bg_color(fill, theme_color_text(), 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_70, 0);
        lv_obj_set_style_radius(fill, 2, 0);
        lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void add_document_card(LibraryUi *ui, const docs::CatalogEntry &entry,
                             app::AppController *controller)
{
    auto *card_ctx = new LibraryCard();
    card_ctx->doc_id = entry.manifest.document_id;
    load_cover(entry, card_ctx);
    ui->covers.push_back(card_ctx);

    lv_obj_t *card = lv_obj_create(ui->grid);
    style_reset(card);
    lv_obj_set_size(card, kCoverW, kCoverH + kCoverCaptionGap + kCaptionH);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, kCoverCaptionGap, 0);
    lv_obj_set_style_opa(card, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(card, card_ctx);

    make_cover_frame(card, card_ctx);
    make_caption(card, entry);

    lv_obj_add_event_cb(card, on_doc_click, LV_EVENT_CLICKED, controller);
}

lv_obj_t *screen_library_create(app::AppController *controller)
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, theme_color_bg(), 0);
    lv_obj_set_style_text_color(scr, theme_color_text(), 0);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));

    lv_obj_t *header = lv_obj_create(scr);
    style_reset(header);
    lv_obj_set_size(header, LV_PCT(100), 48);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, theme_color_border(), 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Library");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, theme_color_text(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *settings_btn = lv_obj_create(header);
    style_reset(settings_btn);
    lv_obj_set_size(settings_btn, 44, 44);
    lv_obj_align(settings_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_radius(settings_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(settings_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(settings_btn, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(settings_btn, theme_color_text(), LV_STATE_PRESSED);
    lv_obj_add_flag(settings_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *settings_lbl = lv_label_create(settings_btn);
    lv_label_set_text(settings_lbl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(settings_lbl, theme_color_text(), 0);
    lv_obj_center(settings_lbl);
    lv_obj_add_event_cb(settings_btn, on_settings_click, LV_EVENT_CLICKED, controller);

    lv_obj_t *grid = lv_obj_create(scr);
    style_reset(grid);
    lv_obj_set_size(grid, LV_PCT(100), 432);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(grid, 20, 0);
    lv_obj_set_style_pad_row(grid, 18, 0);
    lv_obj_set_style_pad_column(grid, 18, 0);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_CLICKABLE);

    auto *ui = new LibraryUi();
    ui->grid = grid;
    lv_obj_set_user_data(scr, ui);
    lv_obj_add_event_cb(scr, on_screen_deleted, LV_EVENT_DELETE, nullptr);

    lv_obj_move_foreground(header);
    lv_obj_update_layout(scr);
    diag::ui_debug_watch(scr, "library");
    diag::ui_debug_watch(settings_btn, "settings_btn");
    diag::ui_debug_watch(grid, "library_grid");

    screen_library_refresh(scr, controller);
    return scr;
}

void screen_library_refresh(lv_obj_t *screen, app::AppController *controller)
{
    if (!screen || !controller) {
        return;
    }

    auto *ui = static_cast<LibraryUi *>(lv_obj_get_user_data(screen));
    if (!ui || !ui->grid) {
        return;
    }

    lv_obj_clean(ui->grid);
    free_covers(ui);

    const auto &entries = controller->catalog().entries();
    const auto &invalid = controller->catalog().invalid_entries();
    if (entries.empty()) {
        lv_obj_t *empty = lv_label_create(ui->grid);
        if (invalid.empty()) {
            lv_label_set_text(empty,
                              "No documents found.\n\nCopy packaged documents to:\n"
                              CONFIG_ESP_READER_DEFAULT_DOC_PATH);
        } else {
            char buf[192];
            snprintf(buf, sizeof(buf),
                     "No valid documents.\n%d package(s) failed validation.\n\nSee serial log.",
                     (int)invalid.size());
            lv_label_set_text(empty, buf);
        }
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(empty, theme_color_muted(), 0);
        lv_obj_set_width(empty, LV_PCT(100));
        return;
    }

    for (const auto &entry : entries) {
        add_document_card(ui, entry, controller);
    }
}

}  // namespace ui
