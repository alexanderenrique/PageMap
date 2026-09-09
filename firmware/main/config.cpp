#include "config.hpp"

#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include <cstring>

namespace app {

static const char *NVS_NS = "pagemap";

esp_err_t config_load(AppConfig *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }

    int32_t brightness = cfg->brightness_percent;
    nvs_get_i32(h, "brightness", &brightness);
    cfg->brightness_percent = static_cast<int>(brightness);

    int32_t sleep_sec = cfg->sleep_timeout_sec;
    nvs_get_i32(h, "sleep_sec", &sleep_sec);
    cfg->sleep_timeout_sec = static_cast<int>(sleep_sec);

    int32_t fit = static_cast<int32_t>(cfg->default_fit_mode);
    nvs_get_i32(h, "fit_mode", &fit);
    cfg->default_fit_mode = static_cast<reader::FitMode>(fit);

    uint8_t dbg = cfg->debug_overlay ? 1 : 0;
    nvs_get_u8(h, "debug_ov", &dbg);
    cfg->debug_overlay = dbg != 0;

#if CONFIG_ESP_READER_DEBUG_OVERLAY
    if (!cfg->debug_overlay) {
        cfg->debug_overlay = true;
    }
#endif

    char doc_id[128] = {};
    size_t len = sizeof(doc_id);
    if (nvs_get_str(h, "last_doc", doc_id, &len) == ESP_OK) {
        cfg->last_document_id = doc_id;
    }

    nvs_close(h);
    return ESP_OK;
}

esp_err_t config_save(const AppConfig &cfg)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), "config", "open");

    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_i32(h, "brightness", cfg.brightness_percent));
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_i32(h, "sleep_sec", cfg.sleep_timeout_sec));
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_i32(h, "fit_mode", static_cast<int32_t>(cfg.default_fit_mode)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_u8(h, "debug_ov", cfg.debug_overlay ? 1 : 0));
    if (!cfg.last_document_id.empty()) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(h, "last_doc", cfg.last_document_id.c_str()));
    }
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t config_save_brightness(int percent)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), "config", "open");
    esp_err_t err = nvs_set_i32(h, "brightness", percent);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t config_save_debug_overlay(bool enabled)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), "config", "open");
    esp_err_t err = nvs_set_u8(h, "debug_ov", enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

}  // namespace app
