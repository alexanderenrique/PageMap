#include "tile_cache.hpp"

namespace reader {

TileCache *g_tile_cache_for_debug = nullptr;

void DecodedTile::bind_image()
{
    img_dsc = {};
    img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    img_dsc.header.w = static_cast<uint32_t>(width);
    img_dsc.header.h = static_cast<uint32_t>(height);
    img_dsc.header.stride = static_cast<uint32_t>(width) * sizeof(uint16_t);
    img_dsc.data_size = static_cast<uint32_t>(pixels.size() * sizeof(uint16_t));
    img_dsc.data = reinterpret_cast<const uint8_t *>(pixels.data());
}

TileCache::TileCache(size_t budget_tiles) : budget_(budget_tiles) {}

void TileCache::set_budget(size_t budget_tiles)
{
    std::lock_guard<std::mutex> lock(mutex_);
    budget_ = budget_tiles;
    evict_if_needed();
}

std::shared_ptr<DecodedTile> TileCache::find(const TileKey &key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = map_.find(key);
    if (it == map_.end()) {
        misses_++;
        return nullptr;
    }
    hits_++;
    auto lru_it = lru_index_[key];
    lru_.splice(lru_.begin(), lru_, lru_it);
    return it->second;
}

void TileCache::insert(std::shared_ptr<DecodedTile> tile)
{
    if (!tile) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const TileKey key = tile->key;
    if (map_.count(key)) {
        map_[key] = tile;
        auto lru_it = lru_index_[key];
        lru_.splice(lru_.begin(), lru_, lru_it);
        return;
    }
    map_[key] = tile;
    lru_.push_front(key);
    lru_index_[key] = lru_.begin();
    evict_if_needed();
}

void TileCache::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    map_.clear();
    lru_.clear();
    lru_index_.clear();
}

void TileCache::invalidate_document(uint32_t doc_hash)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = map_.begin(); it != map_.end();) {
        if (it->first.doc_hash == doc_hash) {
            lru_index_.erase(it->first);
            lru_.remove(it->first);
            it = map_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t TileCache::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return map_.size();
}

void TileCache::evict_if_needed()
{
    while (map_.size() > budget_ && !lru_.empty()) {
        TileKey victim = lru_.back();
        lru_.pop_back();
        lru_index_.erase(victim);
        map_.erase(victim);
    }
}

}  // namespace reader
