#include "app_controller.hpp"

#include "hardware/backlight.hpp"
#include "hardware/board_config.hpp"
#include "reader/tile_key.hpp"
#include "sdkconfig.h"

#include "esp_log.h"
#include <cstring>

static const char *TAG = "app_ctrl";

namespace app {

AppController &app_controller()
{
    static AppController instance;
    return instance;
}

esp_err_t AppController::init(const AppConfig &cfg)
{
    config_ = cfg;
    hw::backlight().set_brightness_percent(cfg.brightness_percent);

    tile_cache_.set_budget(CONFIG_ESP_READER_TILE_CACHE_BUDGET);
    viewport_.set_viewport_size(board::READER_VIEWPORT_W, board::READER_VIEWPORT_H);

    cmd_queue_ = xQueueCreate(16, sizeof(AppCommand));
    if (!cmd_queue_) {
        return ESP_ERR_NO_MEM;
    }

    const char *doc_path = CONFIG_ESP_READER_DEFAULT_DOC_PATH;
    catalog_.scan(doc_path);

    reader::TileManagerConfig tcfg{
        .cache = &tile_cache_,
        .tile_size = 256,
    };
    tile_manager_.init(tcfg);

    return ESP_OK;
}

void AppController::set_callbacks(LibraryRefreshCb lib_cb, ReaderRefreshCb reader_cb)
{
    library_cb_ = std::move(lib_cb);
    reader_cb_ = std::move(reader_cb);
}

esp_err_t AppController::post(const AppCommand &cmd)
{
    if (!cmd_queue_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(cmd_queue_, &cmd, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void AppController::post_open_document(const std::string &id)
{
    AppCommand cmd{.type = AppCommandType::OpenDocument};
    strncpy(cmd.document_id, id.c_str(), sizeof(cmd.document_id) - 1);
    post(cmd);
}

void AppController::post_close_document()
{
    post(AppCommand{.type = AppCommandType::CloseDocument});
}

void AppController::post_go_to_page(int page)
{
    AppCommand cmd{.type = AppCommandType::GoToPage, .page_index = page};
    post(cmd);
}

void AppController::post_next_view()
{
    post(AppCommand{.type = AppCommandType::NextView});
}

void AppController::post_previous_view()
{
    post(AppCommand{.type = AppCommandType::PreviousView});
}

void AppController::post_set_fit_mode(reader::FitMode mode)
{
    AppCommand cmd{.type = AppCommandType::SetFitMode, .fit_mode = mode};
    post(cmd);
}

void AppController::post_set_zoom(float scale, float focal_doc_x, float focal_doc_y,
                                  int focal_screen_x, int focal_screen_y)
{
    AppCommand cmd{.type = AppCommandType::SetZoom};
    cmd.scale = scale;
    cmd.focal_doc_x = focal_doc_x;
    cmd.focal_doc_y = focal_doc_y;
    cmd.focal_screen_x = focal_screen_x;
    cmd.focal_screen_y = focal_screen_y;
    post(cmd);
}

void AppController::post_pan_by(float dx, float dy)
{
    AppCommand cmd{.type = AppCommandType::PanBy, .pan_dx = dx, .pan_dy = dy};
    post(cmd);
}

void AppController::post_rescan_library()
{
    post(AppCommand{.type = AppCommandType::RescanLibrary});
}

void AppController::post_set_brightness(int percent)
{
    AppCommand cmd{.type = AppCommandType::SetBrightness, .brightness = percent};
    post(cmd);
}

void AppController::post_toggle_chrome()
{
    AppCommand cmd{
        .type = AppCommandType::ShowReaderChrome,
        .chrome_visible = !chrome_visible_,
    };
    post(cmd);
}

void AppController::post_save_reading_state()
{
    post(AppCommand{.type = AppCommandType::SaveReadingState});
}

void AppController::open_document_internal(const std::string &id)
{
    const docs::CatalogEntry *entry = catalog_.find_by_id(id);
    if (!entry) {
        ESP_LOGW(TAG, "document not found: %s", id.c_str());
        return;
    }

    active_model_ = std::make_unique<docs::DocumentModel>();
    if (active_model_->load(entry->manifest) != ESP_OK) {
        active_model_.reset();
        return;
    }

    active_doc_id_ = id;
    active_doc_hash_ = reader::hash_document_id(id);
    reader_open_ = true;
    chrome_visible_ = false;

    docs::PageInfo page{};
    active_model_->load_page(0, &page);

    docs::DocumentProgress prog{};
    if (docs::progress_store_get(id, &prog) == ESP_OK) {
        viewport_.set_page(prog.page_index, page.master_width, page.master_height, page.content_box);
        viewport_.set_fit_mode(prog.fit_mode);
        if (prog.fit_mode == reader::FitMode::Manual) {
            viewport_.set_manual_scale(prog.manual_scale, prog.norm_x * page.master_width,
                                       prog.norm_y * page.master_height, board::LCD_H_RES / 2,
                                       board::LCD_V_RES / 2);
        }
    } else {
        viewport_.set_fit_mode(config_.default_fit_mode);
        viewport_.set_page(0, page.master_width, page.master_height, page.content_box);
        if (config_.default_fit_mode == reader::FitMode::Page) {
            viewport_.apply_fit_page();
        } else {
            viewport_.apply_fit_width(true);
        }
    }

    viewport_.set_page_count(entry->manifest.page_count);

    reader::TileManagerConfig tcfg{
        .doc_hash = active_doc_hash_,
        .model = active_model_.get(),
        .viewport = &viewport_,
        .cache = &tile_cache_,
        .tile_size = entry->manifest.tile_size,
    };
    tile_manager_.init(tcfg);
    tile_manager_.invalidate_generation();

    config_.last_document_id = id;
    config_save(config_);

    if (reader_cb_) {
        reader_cb_();
    }
}

void AppController::save_progress_internal()
{
    if (!reader_open_ || active_doc_id_.empty()) {
        return;
    }

    docs::PageInfo page{};
    if (!active_model_ || active_model_->load_page(viewport_.state().page_index, &page) != ESP_OK) {
        return;
    }

    float norm_x = 0, norm_y = 0;
    viewport_.screen_to_doc(0, 0, &norm_x, &norm_y);
    if (page.master_width > 0) {
        norm_x /= page.master_width;
    }
    if (page.master_height > 0) {
        norm_y /= page.master_height;
    }

    docs::DocumentProgress prog{
        .document_id = active_doc_id_,
        .page_index = viewport_.state().page_index,
        .fit_mode = viewport_.fit_mode(),
        .norm_x = norm_x,
        .norm_y = norm_y,
        .manual_scale = viewport_.state().display_scale,
    };
    docs::progress_store_set(prog, false);
}

void AppController::reload_current_page_geometry()
{
    if (!active_model_) {
        return;
    }
    docs::PageInfo page{};
    if (active_model_->load_page(viewport_.state().page_index, &page) != ESP_OK) {
        return;
    }
    const auto mode = viewport_.fit_mode();
    const float prev_scale = viewport_.state().display_scale;
    viewport_.set_page(viewport_.state().page_index, page.master_width, page.master_height,
                       page.content_box);
    if (mode == reader::FitMode::Manual) {
        // Keep scale; re-clamp with new page size.
        viewport_.set_manual_scale(prev_scale, page.master_width * 0.5f, 0.0f, board::LCD_H_RES / 2,
                                   0);
    }
    tile_manager_.set_page_info(page);
}

void AppController::handle_command(const AppCommand &cmd)
{
    switch (cmd.type) {
    case AppCommandType::OpenDocument:
        open_document_internal(cmd.document_id);
        break;
    case AppCommandType::CloseDocument:
        save_progress_internal();
        docs::progress_store_flush();
        reader_open_ = false;
        active_model_.reset();
        if (library_cb_) {
            library_cb_();
        }
        break;
    case AppCommandType::GoToPage:
        if (reader_open_) {
            docs::PageInfo page{};
            if (active_model_->load_page(cmd.page_index, &page) == ESP_OK) {
                viewport_.set_page(cmd.page_index, page.master_width, page.master_height,
                                   page.content_box);
                tile_manager_.set_page_info(page);
                tile_manager_.invalidate_generation();
                if (reader_cb_) {
                    reader_cb_();
                }
            }
        }
        break;
    case AppCommandType::NextView:
        if (reader_open_) {
            const int before = viewport_.state().page_index;
            viewport_.next_view();
            if (viewport_.state().page_index != before) {
                reload_current_page_geometry();
            }
            tile_manager_.invalidate_generation();
            if (reader_cb_) {
                reader_cb_();
            }
        }
        break;
    case AppCommandType::PreviousView:
        if (reader_open_) {
            const int before = viewport_.state().page_index;
            viewport_.previous_view();
            if (viewport_.state().page_index != before) {
                reload_current_page_geometry();
            }
            tile_manager_.invalidate_generation();
            if (reader_cb_) {
                reader_cb_();
            }
        }
        break;
    case AppCommandType::SetFitMode:
        if (reader_open_) {
            viewport_.set_fit_mode(cmd.fit_mode);
            tile_manager_.invalidate_generation();
            if (reader_cb_) {
                reader_cb_();
            }
        }
        break;
    case AppCommandType::SetZoom:
        if (reader_open_) {
            viewport_.set_manual_scale(cmd.scale, cmd.focal_doc_x, cmd.focal_doc_y,
                                       cmd.focal_screen_x, cmd.focal_screen_y);
            tile_manager_.invalidate_generation();
            if (reader_cb_) {
                reader_cb_();
            }
        }
        break;
    case AppCommandType::PanBy:
        if (reader_open_) {
            viewport_.pan_by(cmd.pan_dx, cmd.pan_dy);
            if (reader_cb_) {
                reader_cb_();
            }
        }
        break;
    case AppCommandType::SetBrightness:
        config_.brightness_percent = cmd.brightness;
        hw::backlight().set_brightness_percent(cmd.brightness);
        config_save_brightness(cmd.brightness);
        if (reader_cb_) {
            reader_cb_();
        }
        break;
    case AppCommandType::ShowReaderChrome:
        chrome_visible_ = cmd.chrome_visible;
        if (reader_cb_) {
            reader_cb_();
        }
        break;
    case AppCommandType::SaveReadingState:
        save_progress_internal();
        break;
    case AppCommandType::RescanLibrary:
        catalog_.scan(CONFIG_ESP_READER_DEFAULT_DOC_PATH);
        if (library_cb_) {
            library_cb_();
        }
        break;
    default:
        break;
    }
}

void AppController::process_commands()
{
    AppCommand cmd{};
    while (cmd_queue_ && xQueueReceive(cmd_queue_, &cmd, 0) == pdTRUE) {
        handle_command(cmd);
    }
}

void AppController::controller_task_loop()
{
    for (;;) {
        AppCommand cmd{};
        if (xQueueReceive(cmd_queue_, &cmd, portMAX_DELAY) == pdTRUE) {
            handle_command(cmd);
        }
    }
}

}  // namespace app
