#pragma once

#include "manifest.hpp"
#include "esp_err.h"
#include <string>
#include <vector>

namespace docs {

struct CatalogEntry {
    Manifest manifest;
    float progress = 0.0f;
    int current_page = 0;
    bool invalid = false;
    std::string error;
};

class Catalog {
public:
    esp_err_t scan(const char *root_path);
    const std::vector<CatalogEntry> &entries() const { return entries_; }
    const std::vector<CatalogEntry> &invalid_entries() const { return invalid_entries_; }
    const CatalogEntry *find_by_id(const std::string &document_id) const;

private:
    std::vector<CatalogEntry> entries_;
    std::vector<CatalogEntry> invalid_entries_;
};

}  // namespace docs
