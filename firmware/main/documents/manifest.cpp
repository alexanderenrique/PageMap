#include "manifest.hpp"

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"

#include <cstdio>
#include <cstring>

static const char *TAG = "manifest";

namespace docs {

static esp_err_t read_file(const char *path, std::string *out)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    out->resize(static_cast<size_t>(sz));
    if (fread(out->data(), 1, out->size(), f) != out->size()) {
        fclose(f);
        return ESP_FAIL;
    }
    fclose(f);
    return ESP_OK;
}

esp_err_t manifest_parse_file(const char *path, Manifest *out)
{
    if (!path || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    std::string json;
    ESP_RETURN_ON_ERROR(read_file(path, &json), TAG, "read");

    cJSON *root = cJSON_ParseWithLength(json.data(), json.size());
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed for %s", path);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *fmt = cJSON_GetObjectItem(root, "format");
    if (!cJSON_IsString(fmt) || strcmp(fmt->valuestring, "esp-docpack") != 0) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_SUPPORTED;
    }

    cJSON *fv = cJSON_GetObjectItem(root, "format_version");
    cJSON *did = cJSON_GetObjectItem(root, "document_id");
    cJSON *title = cJSON_GetObjectItem(root, "title");
    cJSON *pc = cJSON_GetObjectItem(root, "page_count");
    cJSON *ts = cJSON_GetObjectItem(root, "tile_size");
    cJSON *tf = cJSON_GetObjectItem(root, "tile_format");
    if (!cJSON_IsNumber(fv) || !cJSON_IsString(did) || !cJSON_IsString(title) ||
        !cJSON_IsNumber(pc) || !cJSON_IsNumber(ts) || !cJSON_IsString(tf)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    out->format_version = fv->valueint;
    out->document_id = did->valuestring;
    out->title = title->valuestring;
    out->page_count = pc->valueint;
    out->tile_size = ts->valueint;
    out->tile_format = tf->valuestring;

    cJSON *cover = cJSON_GetObjectItem(root, "cover");
    if (cJSON_IsString(cover)) {
        out->cover_rel = cover->valuestring;
    }

    cJSON *authors = cJSON_GetObjectItem(root, "authors");
    if (cJSON_IsArray(authors)) {
        cJSON *a;
        cJSON_ArrayForEach(a, authors) {
            if (cJSON_IsString(a)) {
                out->authors.push_back(a->valuestring);
            }
        }
    }

    cJSON *pages = cJSON_GetObjectItem(root, "pages");
    if (cJSON_IsArray(pages)) {
        cJSON *p;
        cJSON_ArrayForEach(p, pages) {
            if (cJSON_IsString(p)) {
                out->page_json_paths.push_back(p->valuestring);
            }
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

bool manifest_validate(const Manifest &m)
{
    if (m.format_version != 1 && m.format_version != 2) {
        ESP_LOGW(TAG, "unsupported format_version %d", m.format_version);
        return false;
    }
    if (m.document_id.empty() || m.title.empty() || m.page_count <= 0) {
        return false;
    }
    if (static_cast<int>(m.page_json_paths.size()) != m.page_count) {
        ESP_LOGW(TAG, "page path count mismatch");
        return false;
    }
    return true;
}

}  // namespace docs
