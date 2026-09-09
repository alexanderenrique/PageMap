#include "tile_manager.hpp"
#include "jpeg_decoder.hpp"
#include "metrics.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <unordered_set>

static const char *TAG = "tile_mgr";

namespace reader {

// POD-safe queue payload (no std::string in FreeRTOS queues).
struct TileIoMsg {
    TileKey key{};
    TileRequestPriority priority = TileRequestPriority::High;
    uint64_t generation = 0;
    char file_path[320] = {};
};

struct DecodeMsg {
    TileIoMsg meta{};
    uint8_t *bytes = nullptr;
    size_t len = 0;
};

static TileManagerConfig s_cfg{};
static QueueHandle_t s_io_queue = nullptr;
static QueueHandle_t s_decode_queue = nullptr;
static std::atomic<uint64_t> *s_generation = nullptr;

static int64_t now_us()
{
    return esp_timer_get_time();
}

esp_err_t TileManager::init(const TileManagerConfig &cfg)
{
    cfg_ = cfg;
    s_cfg = cfg;
    s_generation = &generation_;
    return ESP_OK;
}

void TileManager::set_page_info(const docs::PageInfo &page)
{
    current_page_ = page;
}

void TileManager::invalidate_generation()
{
    generation_.fetch_add(1);
}

void TileManager::collect_range(int level_index, int col0, int col1, int row0, int row1,
                                TileRequestPriority prio, std::vector<TileRequest> *out) const
{
    if (!out || !cfg_.model || level_index < 0 ||
        level_index >= static_cast<int>(current_page_.levels.size())) {
        return;
    }

    const docs::PageLevel &lv = current_page_.levels[level_index];
    col0 = std::clamp(col0, 0, lv.columns - 1);
    col1 = std::clamp(col1, 0, lv.columns - 1);
    row0 = std::clamp(row0, 0, lv.rows - 1);
    row1 = std::clamp(row1, 0, lv.rows - 1);

    for (int r = row0; r <= row1; ++r) {
        for (int c = col0; c <= col1; ++c) {
            TileKey key{
                .doc_hash = cfg_.doc_hash,
                .page_index = cfg_.viewport ? cfg_.viewport->state().page_index : 0,
                .level_index = level_index,
                .col = c,
                .row = r,
            };
            if (cfg_.cache && cfg_.cache->find(key)) {
                continue;
            }

            TileRequest req{};
            req.key = key;
            req.priority = prio;
            req.generation = generation_.load();
            req.file_path = cfg_.model->build_tile_path(current_page_, level_index, c, r);
            if (!req.file_path.empty()) {
                out->push_back(std::move(req));
            }
        }
    }
}

void TileManager::update_visible_requests(std::vector<TileRequest> *out_visible)
{
    if (!out_visible || !cfg_.viewport || current_page_.levels.empty()) {
        return;
    }

    out_visible->clear();
    const reader::Rect vis = cfg_.viewport->visible_doc_rect();
    const float display_scale = cfg_.viewport->doc_to_screen_scale();
    const int level = cfg_.model->select_level_for_scale(display_scale, current_page_);
    const docs::PageLevel &lv = current_page_.levels[level];

    const float tile = static_cast<float>(std::max(1, cfg_.tile_size));
    const float vis_lx0 = vis.x * lv.scale_from_master;
    const float vis_ly0 = vis.y * lv.scale_from_master;
    const float vis_lx1 = (vis.x + vis.w) * lv.scale_from_master;
    const float vis_ly1 = (vis.y + vis.h) * lv.scale_from_master;
    const int col0 = static_cast<int>(std::floor(vis_lx0 / tile));
    const int col1 = static_cast<int>(std::ceil(vis_lx1 / tile)) - 1;
    const int row0 = static_cast<int>(std::floor(vis_ly0 / tile));
    const int row1 = static_cast<int>(std::ceil(vis_ly1 / tile)) - 1;

    collect_range(level, col0, col1, row0, row1, TileRequestPriority::High, out_visible);
}

void TileManager::add_prefetch_border(const std::vector<TileRequest> &visible,
                                      std::vector<TileRequest> *out)
{
    if (!out || visible.empty() || current_page_.levels.empty()) {
        return;
    }

    std::unordered_set<std::string> seen;
    for (const auto &v : visible) {
        seen.insert(v.file_path);
    }
    for (const auto &v : *out) {
        seen.insert(v.file_path);
    }

    const int level = visible.front().key.level_index;
    int col0 = visible.front().key.col;
    int col1 = col0;
    int row0 = visible.front().key.row;
    int row1 = row0;
    for (const auto &v : visible) {
        col0 = std::min(col0, v.key.col);
        col1 = std::max(col1, v.key.col);
        row0 = std::min(row0, v.key.row);
        row1 = std::max(row1, v.key.row);
    }

    std::vector<TileRequest> border;
    collect_range(level, col0 - 1, col1 + 1, row0 - 1, row1 + 1, TileRequestPriority::Low, &border);
    for (auto &b : border) {
        if (seen.insert(b.file_path).second) {
            out->push_back(std::move(b));
        }
    }
}

void tile_manager_submit_io(const TileRequest &req)
{
    if (!s_io_queue) {
        return;
    }
    TileIoMsg msg{};
    msg.key = req.key;
    msg.priority = req.priority;
    msg.generation = req.generation;
    strncpy(msg.file_path, req.file_path.c_str(), sizeof(msg.file_path) - 1);
    xQueueSend(s_io_queue, &msg, 0);
}

void tile_manager_on_decoded(std::shared_ptr<DecodedTile> tile)
{
    if (s_cfg.cache && tile) {
        s_cfg.cache->insert(tile);
    }
}

static void tile_io_task(void *arg)
{
    (void)arg;
    TileIoMsg msg{};
    for (;;) {
        if (xQueueReceive(s_io_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (s_generation && msg.generation != s_generation->load()) {
            continue;  // stale after pan/zoom/page change
        }

        int64_t t0 = now_us();
        ESP_LOGD(TAG, "open %s", msg.file_path);
        FILE *f = fopen(msg.file_path, "rb");
        if (!f) {
            diag::metrics().tile_read_fail++;
            ESP_LOGW(TAG, "missing tile %s", msg.file_path);
            continue;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) {
            fclose(f);
            continue;
        }

        auto *decode = new DecodeMsg();
        decode->meta = msg;
        decode->len = static_cast<size_t>(sz);
        decode->bytes = static_cast<uint8_t *>(
            heap_caps_malloc(decode->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!decode->bytes) {
            decode->bytes = static_cast<uint8_t *>(malloc(decode->len));
        }
        if (!decode->bytes) {
            delete decode;
            fclose(f);
            continue;
        }
        if (fread(decode->bytes, 1, decode->len, f) != decode->len) {
            free(decode->bytes);
            delete decode;
            fclose(f);
            diag::metrics().tile_read_fail++;
            continue;
        }
        fclose(f);
        diag::metrics().sd_read_us += static_cast<uint64_t>(now_us() - t0);
        diag::metrics().sd_reads++;

        if (s_decode_queue) {
            if (xQueueSend(s_decode_queue, &decode, 0) != pdTRUE) {
                free(decode->bytes);
                delete decode;
            }
        } else {
            free(decode->bytes);
            delete decode;
        }
    }
}

static void decode_task(void *arg)
{
    (void)arg;
    DecodeMsg *payload = nullptr;
    for (;;) {
        if (xQueueReceive(s_decode_queue, &payload, portMAX_DELAY) != pdTRUE || !payload) {
            continue;
        }

        if (s_generation && payload->meta.generation != s_generation->load()) {
            free(payload->bytes);
            delete payload;
            continue;
        }

        int64_t t0 = now_us();
        JpegDecodeResult result{};
        esp_err_t err = jpeg_decode_buffer(payload->bytes, payload->len, &result);
        diag::metrics().decode_us += static_cast<uint64_t>(now_us() - t0);
        diag::metrics().decodes++;
        free(payload->bytes);

        if (err != ESP_OK) {
            diag::metrics().decode_fail++;
            delete payload;
            continue;
        }

        auto tile = std::make_shared<DecodedTile>();
        tile->key = payload->meta.key;
        tile->width = result.width;
        tile->height = result.height;
        tile->pixels = std::move(result.rgb565);
        tile->bind_image();
        tile_manager_on_decoded(tile);
        delete payload;
    }
}

esp_err_t tile_io_task_start()
{
    s_io_queue = xQueueCreate(16, sizeof(TileIoMsg));
    s_decode_queue = xQueueCreate(8, sizeof(DecodeMsg *));
    if (!s_io_queue || !s_decode_queue) {
        return ESP_ERR_NO_MEM;
    }
    // FatFS fopen + LFN heap free needs more than 6 KiB; overflow corrupts TLSF.
    xTaskCreatePinnedToCore(tile_io_task, "tile_io", 16384, nullptr, 4, nullptr, 0);
    return ESP_OK;
}

esp_err_t decode_task_start()
{
    xTaskCreatePinnedToCore(decode_task, "decode", 12288, nullptr, 4, nullptr, 1);
    return ESP_OK;
}

}  // namespace reader
