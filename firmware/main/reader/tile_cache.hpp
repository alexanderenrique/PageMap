#pragma once

#include "tile_key.hpp"

#include "lvgl.h"

#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace reader {

struct DecodedTile {
    TileKey key;
    int width = 0;
    int height = 0;
    std::vector<uint16_t> pixels;
    lv_image_dsc_t img_dsc{};

    void bind_image();
};

class TileCache {
public:
    TileCache() : TileCache(16) {}
    explicit TileCache(size_t budget_tiles);

    void set_budget(size_t budget_tiles);
    std::shared_ptr<DecodedTile> find(const TileKey &key);
    void insert(std::shared_ptr<DecodedTile> tile);
    void clear();
    void invalidate_document(uint32_t doc_hash);

    size_t size() const;
    size_t budget() const { return budget_; }
    uint64_t hits() const { return hits_; }
    uint64_t misses() const { return misses_; }

private:
    void evict_if_needed();

    size_t budget_;
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;
    mutable std::mutex mutex_;
    std::list<TileKey> lru_;
    std::unordered_map<TileKey, std::list<TileKey>::iterator, TileKeyHash> lru_index_;
    std::unordered_map<TileKey, std::shared_ptr<DecodedTile>, TileKeyHash> map_;
};

extern TileCache *g_tile_cache_for_debug;

}  // namespace reader
