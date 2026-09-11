#pragma once

#include "manifest.hpp"
#include "esp_err.h"
#include <cstdint>
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

/** Byte span of one JPEG tile on disk (loose file or packed tiles.bin). */
struct TileSpan {
    std::string path;
    uint32_t offset = 0;
    uint32_t length = 0;  // 0 means read the entire file (v1 loose JPEG)
};

struct PageInfo {
    int page_number = 0;
    int master_width = 0;
    int master_height = 0;
    float content_box[4] = {0, 0, 0, 0};
    std::string thumbnail_rel;
    /** Relative to the page directory; empty for loose-tile (v1) packages. */
    std::string tiles_rel;
    std::vector<PageLevel> levels;
    /** Absolute offsets/lengths into tiles.bin; empty when tiles_rel is empty. */
    std::vector<uint32_t> tile_offsets;
    std::vector<uint32_t> tile_lengths;
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

    /** Resolve SD path + optional byte range for a tile. */
    TileSpan resolve_tile_span(const PageInfo &page, int page_index, int level_index, int col,
                               int row) const;

    static int linear_tile_index(const PageInfo &page, int level_index, int col, int row);

private:
    static constexpr int kPageCacheSlots = 3;

    struct PageCacheSlot {
        int index = -1;
        PageInfo info{};
    };

    Manifest manifest_;
    mutable PageCacheSlot page_cache_[kPageCacheSlots]{};
    mutable int page_cache_next_ = 0;
    void clear_page_cache() const;
    std::string page_dir(int page_index) const;
    static esp_err_t load_tiles_bin_index(const std::string &path, PageInfo *out);
};

}  // namespace docs
