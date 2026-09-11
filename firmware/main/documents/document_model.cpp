#include "document_model.hpp"

#include "cJSON.h"
#include "esp_log.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>

static const char *TAG = "doc_model";

namespace docs {

namespace {

constexpr char kTilesMagic[4] = {'T', 'P', 'K', '1'};
constexpr uint32_t kTilesBlobVersion = 1;

}  // namespace

void DocumentModel::clear_page_cache() const
{
    for (int i = 0; i < kPageCacheSlots; ++i) {
        page_cache_[i].index = -1;
        page_cache_[i].info = {};
    }
    page_cache_next_ = 0;
}

esp_err_t DocumentModel::load(const Manifest &manifest)
{
    if (!manifest_validate(manifest)) {
        return ESP_ERR_INVALID_ARG;
    }
    manifest_ = manifest;
    clear_page_cache();
    return ESP_OK;
}

std::string DocumentModel::page_dir(int page_index) const
{
    if (page_index < 0 || page_index >= static_cast<int>(manifest_.page_json_paths.size())) {
        return {};
    }
    std::string rel = manifest_.page_json_paths[page_index];
    auto pos = rel.find("/page.json");
    if (pos != std::string::npos) {
        rel = rel.substr(0, pos);
    }
    return manifest_.package_root + "/" + rel;
}

esp_err_t DocumentModel::load_tiles_bin_index(const std::string &path, PageInfo *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    out->tile_offsets.clear();
    out->tile_lengths.clear();

    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        ESP_LOGW(TAG, "missing tiles.bin %s", path.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    char magic[4] = {};
    uint32_t version = 0;
    uint32_t count = 0;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, kTilesMagic, 4) != 0 ||
        fread(&version, sizeof(version), 1, f) != 1 ||
        fread(&count, sizeof(count), 1, f) != 1) {
        fclose(f);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (version != kTilesBlobVersion || count > 100000) {
        fclose(f);
        ESP_LOGW(TAG, "bad tiles.bin header version=%u count=%u", version, count);
        return ESP_ERR_INVALID_RESPONSE;
    }

    out->tile_offsets.resize(count);
    out->tile_lengths.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t offset = 0;
        uint32_t length = 0;
        if (fread(&offset, sizeof(offset), 1, f) != 1 ||
            fread(&length, sizeof(length), 1, f) != 1) {
            fclose(f);
            out->tile_offsets.clear();
            out->tile_lengths.clear();
            return ESP_ERR_INVALID_RESPONSE;
        }
        out->tile_offsets[i] = offset;
        out->tile_lengths[i] = length;
    }
    fclose(f);
    return ESP_OK;
}

esp_err_t DocumentModel::load_page(int page_index, PageInfo *out) const
{
    if (!out || page_index < 0 || page_index >= manifest_.page_count) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < kPageCacheSlots; ++i) {
        if (page_cache_[i].index == page_index) {
            *out = page_cache_[i].info;
            return ESP_OK;
        }
    }

    std::string path = manifest_.package_root + "/" + manifest_.page_json_paths[page_index];
    FILE *f = fopen(path.c_str(), "r");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string json(static_cast<size_t>(sz), '\0');
    if (fread(json.data(), 1, json.size(), f) != json.size()) {
        fclose(f);
        return ESP_FAIL;
    }
    fclose(f);

    cJSON *root = cJSON_ParseWithLength(json.data(), json.size());
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *pn = cJSON_GetObjectItem(root, "page_number");
    cJSON *mw = cJSON_GetObjectItem(root, "master_width");
    cJSON *mh = cJSON_GetObjectItem(root, "master_height");
    if (!cJSON_IsNumber(pn) || !cJSON_IsNumber(mw) || !cJSON_IsNumber(mh)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    out->page_number = pn->valueint;
    out->master_width = mw->valueint;
    out->master_height = mh->valueint;
    out->tiles_rel.clear();
    out->tile_offsets.clear();
    out->tile_lengths.clear();

    cJSON *cb = cJSON_GetObjectItem(root, "content_box");
    if (cJSON_IsArray(cb) && cJSON_GetArraySize(cb) == 4) {
        for (int i = 0; i < 4; ++i) {
            out->content_box[i] = static_cast<float>(cJSON_GetArrayItem(cb, i)->valuedouble);
        }
    }

    cJSON *thumb = cJSON_GetObjectItem(root, "thumbnail");
    if (cJSON_IsString(thumb)) {
        out->thumbnail_rel = thumb->valuestring;
    }

    cJSON *tiles = cJSON_GetObjectItem(root, "tiles");
    if (cJSON_IsString(tiles) && tiles->valuestring && tiles->valuestring[0]) {
        out->tiles_rel = tiles->valuestring;
    }

    out->levels.clear();
    cJSON *levels = cJSON_GetObjectItem(root, "levels");
    if (cJSON_IsArray(levels)) {
        cJSON *lv;
        cJSON_ArrayForEach(lv, levels) {
            cJSON *id = cJSON_GetObjectItem(lv, "id");
            cJSON *w = cJSON_GetObjectItem(lv, "width");
            cJSON *h = cJSON_GetObjectItem(lv, "height");
            cJSON *cols = cJSON_GetObjectItem(lv, "columns");
            cJSON *rows = cJSON_GetObjectItem(lv, "rows");
            cJSON *sfm = cJSON_GetObjectItem(lv, "scale_from_master");
            cJSON *path_tmpl = cJSON_GetObjectItem(lv, "path");
            if (!cJSON_IsString(id) || !cJSON_IsNumber(w) || !cJSON_IsNumber(h) ||
                !cJSON_IsNumber(cols) || !cJSON_IsNumber(rows) || !cJSON_IsNumber(sfm)) {
                continue;
            }
            PageLevel pl{};
            pl.id = id->valuestring;
            pl.width = w->valueint;
            pl.height = h->valueint;
            pl.columns = cols->valueint;
            pl.rows = rows->valueint;
            pl.scale_from_master = static_cast<float>(sfm->valuedouble);
            if (cJSON_IsString(path_tmpl)) {
                pl.path_template = path_tmpl->valuestring;
            }
            out->levels.push_back(pl);
        }
    }

    cJSON_Delete(root);

    if (!out->tiles_rel.empty()) {
        const std::string blob = page_dir(page_index) + "/" + out->tiles_rel;
        if (load_tiles_bin_index(blob, out) != ESP_OK) {
            ESP_LOGW(TAG, "failed to index %s", blob.c_str());
            out->tiles_rel.clear();
            out->tile_offsets.clear();
            out->tile_lengths.clear();
        } else {
            size_t expected = 0;
            for (const auto &lv : out->levels) {
                expected += static_cast<size_t>(lv.columns) * static_cast<size_t>(lv.rows);
            }
            if (out->tile_offsets.size() != expected) {
                ESP_LOGW(TAG, "tiles.bin count %u != expected %u for page %d",
                         static_cast<unsigned>(out->tile_offsets.size()),
                         static_cast<unsigned>(expected), page_index);
            }
        }
    }

    page_cache_[page_cache_next_].index = page_index;
    page_cache_[page_cache_next_].info = *out;
    page_cache_next_ = (page_cache_next_ + 1) % kPageCacheSlots;
    return ESP_OK;
}

int DocumentModel::select_level_for_scale(float display_scale, const PageInfo &page) const
{
    if (page.levels.empty()) {
        return 0;
    }

    // Prefer a level whose source density meets or slightly exceeds the display scale
    // (mild downscaling preferred over upscaling).
    int best = 0;
    float best_score = 1e9f;
    for (int i = 0; i < static_cast<int>(page.levels.size()); ++i) {
        const auto &lv = page.levels[i];
        const float ratio = lv.scale_from_master / std::max(display_scale, 0.0001f);
        float score = std::abs(std::log2(std::max(ratio, 0.0001f)));
        if (ratio < 1.0f) {
            score += 0.35f;  // penalize upscaling
        }
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

int DocumentModel::linear_tile_index(const PageInfo &page, int level_index, int col, int row)
{
    if (level_index < 0 || level_index >= static_cast<int>(page.levels.size())) {
        return -1;
    }
    int index = 0;
    for (int i = 0; i < level_index; ++i) {
        index += page.levels[i].columns * page.levels[i].rows;
    }
    const PageLevel &lv = page.levels[level_index];
    if (col < 0 || row < 0 || col >= lv.columns || row >= lv.rows) {
        return -1;
    }
    return index + row * lv.columns + col;
}

TileSpan DocumentModel::resolve_tile_span(const PageInfo &page, int page_index, int level_index,
                                          int col, int row) const
{
    TileSpan span{};
    if (!page.tiles_rel.empty() && !page.tile_offsets.empty()) {
        const int idx = linear_tile_index(page, level_index, col, row);
        if (idx < 0 || idx >= static_cast<int>(page.tile_offsets.size())) {
            return span;
        }
        span.path = page_dir(page_index) + "/" + page.tiles_rel;
        span.offset = page.tile_offsets[static_cast<size_t>(idx)];
        span.length = page.tile_lengths[static_cast<size_t>(idx)];
        return span;
    }
    span.path = build_tile_path(page, page_index, level_index, col, row);
    span.offset = 0;
    span.length = 0;
    return span;
}

std::string DocumentModel::build_tile_path(const PageInfo &page, int page_index, int level_index,
                                          int col, int row) const
{
    if (level_index < 0 || level_index >= static_cast<int>(page.levels.size())) {
        return {};
    }

    const PageLevel &lv = page.levels[level_index];
    if (lv.path_template.empty()) {
        return {};
    }
    std::string rel = lv.path_template;
    auto replace_token = [&](const char *token, int value) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", value);
        size_t pos = 0;
        while ((pos = rel.find(token, pos)) != std::string::npos) {
            rel.replace(pos, strlen(token), buf);
            pos += strlen(buf);
        }
    };
    replace_token("{column}", col);
    replace_token("{row}", row);

    return page_dir(page_index) + "/" + rel;
}

std::string DocumentModel::build_tile_path(const PageInfo &page, int level_index, int col,
                                          int row) const
{
    const int page_index = std::max(0, page.page_number - 1);
    return build_tile_path(page, page_index, level_index, col, row);
}

std::string DocumentModel::build_tile_path(int page_index, int level_index, int col, int row) const
{
    PageInfo page{};
    if (load_page(page_index, &page) != ESP_OK) {
        return {};
    }
    return build_tile_path(page, page_index, level_index, col, row);
}

}  // namespace docs
