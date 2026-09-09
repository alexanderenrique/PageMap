#include "progress_store.hpp"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#include "esp_vfs.h"
#include "sys/stat.h"

static const char *TAG = "progress";
static const char *PROGRESS_DIR = "/sdcard/reader-state";
static const char *PROGRESS_PATH = "/sdcard/reader-state/progress.json";
static const char *PROGRESS_TMP = "/sdcard/reader-state/progress.json.tmp";

namespace docs {

static std::mutex s_mutex;
static std::map<std::string, DocumentProgress> s_cache;
static QueueHandle_t s_queue = nullptr;
static bool s_dirty = false;
static int64_t s_last_change_ms = 0;

static int64_t now_ms()
{
    return esp_timer_get_time() / 1000;
}

static esp_err_t read_json_file(const char *path, std::string *out)
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
    fread(out->data(), 1, out->size(), f);
    fclose(f);
    return ESP_OK;
}

static esp_err_t write_atomic(const std::map<std::string, DocumentProgress> &data)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *docs = cJSON_AddObjectToObject(root, "documents");
    for (const auto &kv : data) {
        const auto &p = kv.second;
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "page_index", p.page_index);
        cJSON_AddNumberToObject(item, "fit_mode", static_cast<int>(p.fit_mode));
        cJSON_AddNumberToObject(item, "norm_x", p.norm_x);
        cJSON_AddNumberToObject(item, "norm_y", p.norm_y);
        cJSON_AddNumberToObject(item, "manual_scale", p.manual_scale);
        cJSON_AddNumberToObject(item, "last_opened_ms", static_cast<double>(p.last_opened_ms));
        cJSON_AddItemToObject(docs, p.document_id.c_str(), item);
    }

    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return ESP_ERR_NO_MEM;
    }

    FILE *f = fopen(PROGRESS_TMP, "w");
    if (!f) {
        cJSON_free(printed);
        return ESP_FAIL;
    }
    size_t len = strlen(printed);
    if (fwrite(printed, 1, len, f) != len) {
        fclose(f);
        cJSON_free(printed);
        remove(PROGRESS_TMP);
        return ESP_FAIL;
    }
    fflush(f);
    fclose(f);
    cJSON_free(printed);

    if (rename(PROGRESS_TMP, PROGRESS_PATH) != 0) {
        remove(PROGRESS_TMP);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t progress_store_init()
{
    mkdir(PROGRESS_DIR, 0755);

    std::lock_guard<std::mutex> lock(s_mutex);
    std::string json;
    if (read_json_file(PROGRESS_PATH, &json) == ESP_OK) {
        cJSON *root = cJSON_ParseWithLength(json.data(), json.size());
        if (root) {
            cJSON *docs = cJSON_GetObjectItem(root, "documents");
            if (cJSON_IsObject(docs)) {
                cJSON *item = docs->child;
                while (item) {
                    DocumentProgress p{};
                    p.document_id = item->string;
                    p.page_index = cJSON_GetObjectItem(item, "page_index")->valueint;
                    p.fit_mode = static_cast<reader::FitMode>(
                        cJSON_GetObjectItem(item, "fit_mode")->valueint);
                    p.norm_x = static_cast<float>(cJSON_GetObjectItem(item, "norm_x")->valuedouble);
                    p.norm_y = static_cast<float>(cJSON_GetObjectItem(item, "norm_y")->valuedouble);
                    p.manual_scale = static_cast<float>(
                        cJSON_GetObjectItem(item, "manual_scale")->valuedouble);
                    p.last_opened_ms = static_cast<int64_t>(
                        cJSON_GetObjectItem(item, "last_opened_ms")->valuedouble);
                    s_cache[p.document_id] = p;
                    item = item->next;
                }
            }
            cJSON_Delete(root);
        }
    }

    s_queue = xQueueCreate(4, sizeof(bool));
    return ESP_OK;
}

esp_err_t progress_store_get(const std::string &document_id, DocumentProgress *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_cache.find(document_id);
    if (it == s_cache.end()) {
        return ESP_ERR_NOT_FOUND;
    }
    *out = it->second;
    return ESP_OK;
}

esp_err_t progress_store_set(const DocumentProgress &progress, bool immediate)
{
    DocumentProgress p = progress;
    p.last_opened_ms = now_ms();
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_cache[p.document_id] = p;
        s_dirty = true;
        s_last_change_ms = now_ms();
    }
    if (immediate && s_queue) {
        bool flag = true;
        xQueueSend(s_queue, &flag, 0);
    }
    return ESP_OK;
}

esp_err_t progress_store_flush()
{
    std::map<std::string, DocumentProgress> snapshot;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_dirty) {
            return ESP_OK;
        }
        snapshot = s_cache;
        s_dirty = false;
    }
    return write_atomic(snapshot);
}

void progress_store_task(void *arg)
{
    (void)arg;
    bool wake = false;
    const int debounce_ms = CONFIG_ESP_READER_PROGRESS_DEBOUNCE_MS;
    for (;;) {
        if (xQueueReceive(s_queue, &wake, pdMS_TO_TICKS(debounce_ms)) == pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(debounce_ms));
        }
        int64_t elapsed = now_ms() - s_last_change_ms;
        if (s_dirty && elapsed >= debounce_ms) {
            esp_err_t err = progress_store_flush();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "flush failed: %s", esp_err_to_name(err));
            }
        }
    }
}

}  // namespace docs
