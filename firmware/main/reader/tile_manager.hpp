#pragma once

#include "document_model.hpp"
#include "tile_cache.hpp"
#include "tile_key.hpp"
#include "viewport.hpp"

#include "esp_err.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace reader {

enum class TileRequestPriority : uint8_t {
    High = 0,
    Low = 1,
};

struct TileRequest {
    TileKey key;
    std::string file_path;
    /** When non-zero, read only this byte range (packed tiles.bin). */
    uint32_t byte_offset = 0;
    uint32_t byte_length = 0;
    TileRequestPriority priority = TileRequestPriority::High;
    uint64_t generation = 0;
};

struct TileManagerConfig {
    uint32_t doc_hash = 0;
    docs::DocumentModel *model = nullptr;
    Viewport *viewport = nullptr;
    TileCache *cache = nullptr;
    int tile_size = 256;
};

class TileManager {
public:
    esp_err_t init(const TileManagerConfig &cfg);
    void set_page_info(const docs::PageInfo &page);
    void invalidate_generation();

    void update_visible_requests(std::vector<TileRequest> *out_visible);
    void add_prefetch_border(const std::vector<TileKey> &vis_keys, std::vector<TileRequest> *out);
    void add_neighbor_page_prefetch(std::vector<TileRequest> *out);

    uint64_t generation() const { return generation_; }

private:
    void collect_range(const docs::PageInfo &page, int page_index, int level_index, int col0,
                       int col1, int row0, int row1, TileRequestPriority prio,
                       std::vector<TileRequest> *out) const;
    void collect_visible_rect(const docs::PageInfo &page, int page_index, const Rect &vis,
                              float display_scale, TileRequestPriority prio,
                              std::vector<TileRequest> *out) const;

    TileManagerConfig cfg_;
    docs::PageInfo current_page_;
    std::atomic<uint64_t> generation_{0};
};

esp_err_t tile_io_task_start();
esp_err_t decode_task_start();
void tile_manager_submit_io(const TileRequest &req);
void tile_manager_on_decoded(std::shared_ptr<DecodedTile> tile);
void tile_manager_set_decoded_notify(void (*fn)());

}  // namespace reader
