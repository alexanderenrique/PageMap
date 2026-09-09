#pragma once

#include "esp_err.h"
#include "reader/viewport.hpp"
#include <stdint.h>
#include <string>

namespace app {

struct AppConfig {
    int brightness_percent = 80;
    int sleep_timeout_sec = 300;
    reader::FitMode default_fit_mode = reader::FitMode::Width;
    bool debug_overlay = false;
    std::string last_document_id;
};

esp_err_t config_load(AppConfig *cfg);
esp_err_t config_save(const AppConfig &cfg);
esp_err_t config_save_brightness(int percent);
esp_err_t config_save_debug_overlay(bool enabled);

}  // namespace app
