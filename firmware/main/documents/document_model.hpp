#pragma once

#include "manifest.hpp"
#include "esp_err.h"
#include <string>
#include <vector>

namespace docs {

struct PageLevel {
    std::string id;
    int width = 0;
    int height = 0;
    int columns = 0;
    int rows = 0;
    float scale_from_master = 1.0f;
    std::string path_template;
};

struct PageInfo {
    int page_number = 0;
    int master_width = 0;
    int master_height = 0;
    float content_box[4] = {0, 0, 0, 0};
    std::string thumbnail_rel;
    std::vector<PageLevel> levels;
};

class DocumentModel {
public:
    esp_err_t load(const Manifest &manifest);
    const Manifest &manifest() const { return manifest_; }

    esp_err_t load_page(int page_index, PageInfo *out) const;
    /** Choose pyramid level whose scale_from_master best matches display_scale. */
    int select_level_for_scale(float display_scale, const PageInfo &page) const;
    std::string build_tile_path(int page_index, int level_index, int col, int row) const;
    std::string build_tile_path(const PageInfo &page, int page_index, int level_index, int col,
                                int row) const;
    std::string build_tile_path(const PageInfo &page, int level_index, int col, int row) const;

private:
    Manifest manifest_;
    mutable int cached_page_index_ = -1;
    mutable PageInfo cached_page_{};
    std::string page_dir(int page_index) const;
};

}  // namespace docs
