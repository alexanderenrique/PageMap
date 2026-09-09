#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace reader {

struct TileKey {
    uint32_t doc_hash = 0;
    int page_index = 0;
    int level_index = 0;
    int col = 0;
    int row = 0;

    bool operator==(const TileKey &o) const
    {
        return doc_hash == o.doc_hash && page_index == o.page_index &&
               level_index == o.level_index && col == o.col && row == o.row;
    }
};

struct TileKeyHash {
    size_t operator()(const TileKey &k) const
    {
        size_t h = std::hash<uint32_t>{}(k.doc_hash);
        h ^= std::hash<int>{}(k.page_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.level_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.col) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.row) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

inline uint32_t hash_document_id(const std::string &doc_id)
{
    // FNV-1a 32-bit
    uint32_t hash = 2166136261u;
    for (unsigned char c : doc_id) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

}  // namespace reader
