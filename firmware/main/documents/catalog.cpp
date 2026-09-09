#include "catalog.hpp"
#include "progress_store.hpp"

#include "dirent.h"
#include "esp_log.h"
#include "sys/stat.h"

#include <cstring>

static const char *TAG = "catalog";

namespace docs {

esp_err_t Catalog::scan(const char *root_path)
{
    entries_.clear();
    invalid_entries_.clear();
    if (!root_path) {
        return ESP_ERR_INVALID_ARG;
    }

    DIR *dir = opendir(root_path);
    if (!dir) {
        ESP_LOGW(TAG, "cannot open %s", root_path);
        return ESP_ERR_NOT_FOUND;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') {
            continue;
        }

        char manifest_path[384];
        snprintf(manifest_path, sizeof(manifest_path), "%s/%s/manifest.json", root_path, ent->d_name);

        struct stat st{};
        if (stat(manifest_path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        CatalogEntry entry{};
        entry.manifest.package_root = std::string(root_path) + "/" + ent->d_name;
        esp_err_t err = manifest_parse_file(manifest_path, &entry.manifest);
        if (err != ESP_OK || !manifest_validate(entry.manifest)) {
            entry.invalid = true;
            entry.error = err != ESP_OK ? "parse failed" : "validation failed";
            entry.manifest.title = ent->d_name;
            ESP_LOGW(TAG, "skip %s: %s", ent->d_name, entry.error.c_str());
            invalid_entries_.push_back(std::move(entry));
            continue;
        }

        DocumentProgress prog{};
        if (progress_store_get(entry.manifest.document_id, &prog) == ESP_OK) {
            entry.current_page = prog.page_index;
            if (entry.manifest.page_count > 0) {
                entry.progress = static_cast<float>(prog.page_index + 1) /
                                 static_cast<float>(entry.manifest.page_count);
            }
        }

        entries_.push_back(std::move(entry));
    }
    closedir(dir);

    ESP_LOGI(TAG, "catalog: %zu valid, %zu invalid", entries_.size(), invalid_entries_.size());
    return ESP_OK;
}

const CatalogEntry *Catalog::find_by_id(const std::string &document_id) const
{
    for (const auto &e : entries_) {
        if (e.manifest.document_id == document_id) {
            return &e;
        }
    }
    return nullptr;
}

}  // namespace docs
