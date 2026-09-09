#pragma once

#include "reader/viewport.hpp"
#include "esp_err.h"
#include <string>

namespace docs {

struct DocumentProgress {
    std::string document_id;
    int page_index = 0;
    reader::FitMode fit_mode = reader::FitMode::Width;
    float norm_x = 0.0f;
    float norm_y = 0.0f;
    float manual_scale = 1.0f;
    int64_t last_opened_ms = 0;
};

esp_err_t progress_store_init();
esp_err_t progress_store_get(const std::string &document_id, DocumentProgress *out);
esp_err_t progress_store_set(const DocumentProgress &progress, bool immediate = false);
esp_err_t progress_store_flush();

void progress_store_task(void *arg);

}  // namespace docs
