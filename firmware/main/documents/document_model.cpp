#include "document_model.hpp"

#include "cJSON.h"
#include "esp_log.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>

static const char *TAG = "doc_model";

namespace docs {

esp_err_t DocumentModel::load(const Manifest &manifest)
{
    if (!manifest_validate(manifest)) {
        return ESP_ERR_INVALID_ARG;
    }
    manifest_ = manifest;
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

esp_err_t DocumentModel::load_page(int page_index, PageInfo *out) const
{
    if (!out || page_index < 0 || page_index >= manifest_.page_count) {
        return ESP_ERR_INVALID_ARG;
    }

    if (cached_page_index_ == page_index) {
        *out = cached_page_;
        return ESP_OK;
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
            cJSON *path = cJSON_GetObjectItem(lv, "path");
            if (!cJSON_IsString(id) || !cJSON_IsNumber(w) || !cJSON_IsNumber(h) ||
                !cJSON_IsNumber(cols) || !cJSON_IsNumber(rows) || !cJSON_IsNumber(sfm) ||
                !cJSON_IsString(path)) {
                continue;
            }
            PageLevel pl{};
            pl.id = id->valuestring;
            pl.width = w->valueint;
            pl.height = h->valueint;
            pl.columns = cols->valueint;
            pl.rows = rows->valueint;
            pl.scale_from_master = static_cast<float>(sfm->valuedouble);
            pl.path_template = path->valuestring;
            out->levels.push_back(pl);
        }
    }

    cJSON_Delete(root);
    cached_page_index_ = page_index;
    cached_page_ = *out;
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

std::string DocumentModel::build_tile_path(const PageInfo &page, int page_index, int level_index,
                                          int col, int row) const
{
    if (level_index < 0 || level_index >= static_cast<int>(page.levels.size())) {
        return {};
    }

    const PageLevel &lv = page.levels[level_index];
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
