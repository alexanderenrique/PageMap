#pragma once

#include "esp_err.h"
#include <string>
#include <vector>

namespace docs {

struct Manifest {
    int format_version = 0;
    std::string document_id;
    std::string title;
    std::vector<std::string> authors;
    int page_count = 0;
    std::string cover_rel;
    int tile_size = 256;
    std::string tile_format;
    std::vector<std::string> page_json_paths;
    std::string package_root;
};

esp_err_t manifest_parse_file(const char *path, Manifest *out);
bool manifest_validate(const Manifest &m);

}  // namespace docs
