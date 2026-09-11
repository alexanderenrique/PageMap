#include "app_controller.hpp"
#include "config.hpp"
#include "documents/progress_store.hpp"
#include "hardware/backlight.hpp"
#include "hardware/board_config.hpp"
#include "hardware/display_port.hpp"
#include "hardware/expander.hpp"
#include "hardware/storage_mount.hpp"
#include "hardware/touch_port.hpp"
#include "reader/tile_cache.hpp"
#include "reader/tile_manager.hpp"
#include "diagnostics/ui_debug.hpp"
#include "ui/panel_quick_settings.hpp"
#include "ui/screen_library.hpp"
#include "ui/screen_reader.hpp"
#include "ui/theme.hpp"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "nvs_flash.h"

#include "esp_heap_caps.h"
#include "esp_system.h"

static const char *TAG = "app_main";

#define ESP_READER_LV_LOCK() lvgl_port_lock(0)
#define ESP_READER_LV_UNLOCK() lvgl_port_unlock()

namespace {

reader::TileCache *g_tile_cache = nullptr;
lv_obj_t *g_library_screen = nullptr;
lv_obj_t *g_reader_screen = nullptr;

size_t auto_tile_cache_budget()
{
    const size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    // Each 256x256 RGB565 tile is 128 KiB. Reserve ~3 MB for framebuffers/LVGL/stacks.
    const size_t reserve = 3 * 1024 * 1024;
    if (free_psram <= reserve) {
        return 8;
    }
    size_t tiles = (free_psram - reserve) / (128 * 1024);
    if (tiles < 8) {
        tiles = 8;
    }
    if (tiles > 48) {
        tiles = 48;
    }
    return tiles;
}

void log_memory(const char *where)
{
    ESP_LOGI(TAG, "[%s] internal free=%u largest=%u | psram free=%u largest=%u",
             where,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

void show_library(app::AppController &ctrl)
{
    ESP_READER_LV_LOCK();
    if (g_reader_screen) {
        lv_obj_delete(g_reader_screen);
        g_reader_screen = nullptr;
    }
    if (!g_library_screen) {
        g_library_screen = ui::screen_library_create(&ctrl);
    } else {
        ui::screen_library_refresh(g_library_screen, &ctrl);
    }
    lv_screen_load(g_library_screen);
    ESP_READER_LV_UNLOCK();
}

void show_reader(app::AppController &ctrl)
{
    ESP_READER_LV_LOCK();
    if (g_reader_screen) {
        lv_obj_delete(g_reader_screen);
    }
    g_reader_screen = ui::screen_reader_create(&ctrl);
    lv_screen_load(g_reader_screen);
    ESP_READER_LV_UNLOCK();
}

void apply_display_orientation(const app::AppConfig &cfg)
{
    const bool portrait = cfg.orientation == app::ReadingOrientation::Portrait;
    hw::display_port_set_portrait(portrait);
    hw::touch_port_set_portrait(portrait);
}

void recreate_ui_for_orientation()
{
    app::AppController &ctrl = app::app_controller();
    ESP_READER_LV_LOCK();
    ui::panel_quick_settings_close();
    apply_display_orientation(ctrl.config());

    lv_obj_t *old_lib = g_library_screen;
    lv_obj_t *old_reader = g_reader_screen;
    g_library_screen = nullptr;
    g_reader_screen = nullptr;

    if (ctrl.reader_open()) {
        g_reader_screen = ui::screen_reader_create(&ctrl);
        lv_screen_load(g_reader_screen);
    } else {
        g_library_screen = ui::screen_library_create(&ctrl);
        lv_screen_load(g_library_screen);
    }

    if (old_lib) {
        lv_obj_delete(old_lib);
    }
    if (old_reader) {
        lv_obj_delete(old_reader);
    }
    ESP_READER_LV_UNLOCK();
}

void on_library_refresh()
{
    show_library(app::app_controller());
}

void on_reader_refresh()
{
    app::AppController &ctrl = app::app_controller();
    if (!ctrl.reader_open()) {
        show_library(ctrl);
        return;
    }
    ESP_READER_LV_LOCK();
    if (g_reader_screen) {
        ui::screen_reader_refresh(g_reader_screen, &ctrl);
    } else {
        show_reader(ctrl);
    }
    ESP_READER_LV_UNLOCK();
}

void on_reader_pan()
{
    if (!g_reader_screen) {
        return;
    }
    ESP_READER_LV_LOCK();
    ui::screen_reader_invalidate_canvas(g_reader_screen);
    ESP_READER_LV_UNLOCK();
}

void app_controller_task(void *arg)
{
    auto *ctrl = static_cast<app::AppController *>(arg);
    ctrl->controller_task_loop();
}

}  // namespace

extern "C" void app_main(void)  // NOLINT(readability-identifier-naming)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    app::AppConfig cfg{};
    config_load(&cfg);

    ESP_ERROR_CHECK(hw::expander().init());
    ESP_ERROR_CHECK(hw::backlight().init());
    hw::backlight().set_brightness_percent(cfg.brightness_percent);

    hw::DisplayHandles disp{};
    ESP_ERROR_CHECK(hw::display_port_init(&disp));
    err = hw::touch_port_init(disp.lv_display);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed (%s); continuing without touch", esp_err_to_name(err));
    }
    ESP_READER_LV_LOCK();
    apply_display_orientation(cfg);
    diag::ui_debug_install(disp.lv_display, hw::touch_indev());
    ESP_READER_LV_UNLOCK();

    err = hw::storage_mount_init();
    if (err == ESP_OK) {
        docs::progress_store_init();
        xTaskCreatePinnedToCore(docs::progress_store_task, "persist", 4096, nullptr, 2, nullptr, 0);
    } else {
        ESP_LOGW(TAG, "SD not mounted; library will show empty/placeholder state");
    }

    reader::tile_io_task_start();
    reader::decode_task_start();

    log_memory("pre-controller");
    const size_t cache_tiles = auto_tile_cache_budget();
    ESP_LOGI(TAG, "tile cache budget=%u tiles (~%u KiB)", (unsigned)cache_tiles,
             (unsigned)(cache_tiles * 128));

    app::AppController &ctrl = app::app_controller();
    ESP_ERROR_CHECK(ctrl.init(cfg));
    ctrl.tile_cache().set_budget(cache_tiles);
    g_tile_cache = &ctrl.tile_cache();
    reader::g_tile_cache_for_debug = g_tile_cache;

    ctrl.set_callbacks(on_library_refresh, on_reader_refresh, on_reader_pan, recreate_ui_for_orientation);

    xTaskCreatePinnedToCore(app_controller_task, "app_ctrl", 16384, &ctrl, 5, nullptr, 1);

    ui::theme_apply(ui::ThemeMode::Light);
    show_library(ctrl);

    if (!cfg.last_document_id.empty() && hw::storage_is_mounted()) {
        ctrl.post_open_document(cfg.last_document_id);
    }

    log_memory("ready");
    ESP_LOGI(TAG, "PageMap ready");
}
