#pragma once

#include "config.hpp"
#include "documents/catalog.hpp"
#include "documents/document_model.hpp"
#include "documents/progress_store.hpp"
#include "reader/tile_cache.hpp"
#include "reader/tile_manager.hpp"
#include "reader/viewport.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <functional>
#include <memory>
#include <string>

namespace app {

enum class AppCommandType : uint8_t {
    OpenDocument,
    CloseDocument,
    GoToPage,
    NextView,
    PreviousView,
    SetFitMode,
    SetZoom,
    PanBy,
    SetBrightness,
    ShowReaderChrome,
    SaveReadingState,
    RescanLibrary,
};

struct AppCommand {
    AppCommandType type;
    char document_id[128] = {};
    int page_index = 0;
    reader::FitMode fit_mode = reader::FitMode::Width;
    float scale = 1.0f;
    float focal_doc_x = 0.0f;
    float focal_doc_y = 0.0f;
    int focal_screen_x = 0;
    int focal_screen_y = 0;
    float pan_dx = 0.0f;
    float pan_dy = 0.0f;
    int brightness = 80;
    bool chrome_visible = true;
};

class AppController {
public:
    using LibraryRefreshCb = std::function<void()>;
    using ReaderRefreshCb = std::function<void()>;

    esp_err_t init(const AppConfig &cfg);
    void set_callbacks(LibraryRefreshCb lib_cb, ReaderRefreshCb reader_cb);

    esp_err_t post(const AppCommand &cmd);
    void post_open_document(const std::string &id);
    void post_close_document();
    void post_go_to_page(int page);
    void post_next_view();
    void post_previous_view();
    void post_set_fit_mode(reader::FitMode mode);
    void post_set_zoom(float scale, float focal_doc_x, float focal_doc_y, int focal_screen_x,
                       int focal_screen_y);
    void post_pan_by(float dx, float dy);
    void post_set_brightness(int percent);
    void post_toggle_chrome();
    void post_save_reading_state();
    void post_rescan_library();

    void process_commands();
    void controller_task_loop();

    docs::Catalog &catalog() { return catalog_; }
    docs::DocumentModel *active_document() { return active_model_.get(); }
    reader::Viewport &viewport() { return viewport_; }
    reader::TileCache &tile_cache() { return tile_cache_; }
    reader::TileManager &tile_manager() { return tile_manager_; }

    bool reader_open() const { return reader_open_; }
    bool chrome_visible() const { return chrome_visible_; }
    const AppConfig &config() const { return config_; }
    AppConfig &config_mut() { return config_; }

private:
    void handle_command(const AppCommand &cmd);
    void open_document_internal(const std::string &id);
    void save_progress_internal();
    void reload_current_page_geometry();

    AppConfig config_;
    docs::Catalog catalog_;
    std::unique_ptr<docs::DocumentModel> active_model_;
    reader::Viewport viewport_;
    reader::TileCache tile_cache_;
    reader::TileManager tile_manager_;
    QueueHandle_t cmd_queue_ = nullptr;
    bool reader_open_ = false;
    bool chrome_visible_ = false;
    std::string active_doc_id_;
    uint32_t active_doc_hash_ = 0;
    LibraryRefreshCb library_cb_;
    ReaderRefreshCb reader_cb_;
};

AppController &app_controller();

}  // namespace app
