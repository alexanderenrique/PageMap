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
#include <mutex>
#include <unordered_set>

static const char *TAG = "tile_mgr";

namespace reader {

// POD-safe queue payload (no std::string in FreeRTOS queues).
struct TileIoMsg {
    TileKey key{};
    TileRequestPriority priority = TileRequestPriority::High;
    uint64_t generation = 0;
    uint32_t byte_offset = 0;
    uint32_t byte_length = 0;
    char file_path[320] = {};
};

struct DecodeMsg {
    TileIoMsg meta{};
    uint8_t *bytes = nullptr;
    size_t len = 0;
};

static TileManagerConfig s_cfg{};
static QueueHandle_t s_io_high = nullptr;
static QueueHandle_t s_io_low = nullptr;
static QueueHandle_t s_decode_queue = nullptr;
static std::atomic<uint64_t> *s_generation = nullptr;
static std::mutex s_inflight_mu;
static std::unordered_set<TileKey, TileKeyHash> s_inflight;
static void (*s_decoded_notify)() = nullptr;

static bool inflight_try_add(const TileKey &key)
{
    std::lock_guard<std::mutex> lock(s_inflight_mu);
    return s_inflight.insert(key).second;
}

static void inflight_remove(const TileKey &key)
{
    std::lock_guard<std::mutex> lock(s_inflight_mu);
    s_inflight.erase(key);
}

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

void TileManager::collect_range(const docs::PageInfo &page, int page_index, int level_index,
                                int col0, int col1, int row0, int row1, TileRequestPriority prio,
                                std::vector<TileRequest> *out) const
{
    if (!out || !cfg_.model || level_index < 0 ||
        level_index >= static_cast<int>(page.levels.size())) {
        return;
    }

    const docs::PageLevel &lv = page.levels[level_index];
    if (lv.columns <= 0 || lv.rows <= 0) {
        return;
    }
    col0 = std::clamp(col0, 0, lv.columns - 1);
    col1 = std::clamp(col1, 0, lv.columns - 1);
    row0 = std::clamp(row0, 0, lv.rows - 1);
    row1 = std::clamp(row1, 0, lv.rows - 1);

    for (int r = row0; r <= row1; ++r) {
        for (int c = col0; c <= col1; ++c) {
            TileKey key{
                .doc_hash = cfg_.doc_hash,
                .page_index = page_index,
                .level_index = level_index,
                .col = c,
                .row = r,
            };
            if (cfg_.cache && cfg_.cache->contains(key)) {
                continue;
            }

            TileRequest req{};
            req.key = key;
            req.priority = prio;
            req.generation = generation_.load();
            const docs::TileSpan span =
                cfg_.model->resolve_tile_span(page, page_index, level_index, c, r);
            req.file_path = span.path;
            req.byte_offset = span.offset;
            req.byte_length = span.length;
            if (!req.file_path.empty()) {
                out->push_back(std::move(req));
            }
        }
    }
}

void TileManager::collect_visible_rect(const docs::PageInfo &page, int page_index, const Rect &vis,
                                       float display_scale, TileRequestPriority prio,
                                       std::vector<TileRequest> *out) const
{
    if (!out || !cfg_.model || page.levels.empty()) {
        return;
    }

    const int level = cfg_.model->select_level_for_scale(display_scale, page);
    const docs::PageLevel &lv = page.levels[level];
    const float tile = static_cast<float>(std::max(1, cfg_.tile_size));
    const float vis_lx0 = vis.x * lv.scale_from_master;
    const float vis_ly0 = vis.y * lv.scale_from_master;
    const float vis_lx1 = (vis.x + vis.w) * lv.scale_from_master;
    const float vis_ly1 = (vis.y + vis.h) * lv.scale_from_master;
    const int col0 = static_cast<int>(std::floor(vis_lx0 / tile));
    const int col1 = static_cast<int>(std::ceil(vis_lx1 / tile)) - 1;
    const int row0 = static_cast<int>(std::floor(vis_ly0 / tile));
    const int row1 = static_cast<int>(std::ceil(vis_ly1 / tile)) - 1;
    collect_range(page, page_index, level, col0, col1, row0, row1, prio, out);
}

void TileManager::update_visible_requests(std::vector<TileRequest> *out_visible)
{
    if (!out_visible || !cfg_.viewport || current_page_.levels.empty()) {
        return;
    }

    out_visible->clear();
    collect_visible_rect(current_page_, cfg_.viewport->state().page_index,
                         cfg_.viewport->visible_doc_rect(), cfg_.viewport->doc_to_screen_scale(),
                         TileRequestPriority::High, out_visible);
}

void TileManager::add_prefetch_border(const std::vector<TileKey> &vis_keys,
                                      std::vector<TileRequest> *out)
{
    if (!out || vis_keys.empty() || current_page_.levels.empty()) {
        return;
    }

    std::unordered_set<TileKey, TileKeyHash> seen(vis_keys.begin(), vis_keys.end());
    for (const auto &v : *out) {
        seen.insert(v.key);
    }

    const int level = vis_keys.front().level_index;
    const int page_index = vis_keys.front().page_index;
    int col0 = vis_keys.front().col;
    int col1 = col0;
    int row0 = vis_keys.front().row;
    int row1 = row0;
    for (const auto &k : vis_keys) {
        col0 = std::min(col0, k.col);
        col1 = std::max(col1, k.col);
        row0 = std::min(row0, k.row);
        row1 = std::max(row1, k.row);
    }

    std::vector<TileRequest> border;
    collect_range(current_page_, page_index, level, col0 - 1, col1 + 1, row0 - 1, row1 + 1,
                  TileRequestPriority::Low, &border);
    for (auto &b : border) {
        if (seen.insert(b.key).second) {
            out->push_back(std::move(b));
        }
    }
}

void TileManager::add_neighbor_page_prefetch(std::vector<TileRequest> *out)
{
    if (!out || !cfg_.viewport || !cfg_.model) {
        return;
    }

    const FitMode mode = cfg_.viewport->fit_mode();
    if (mode == FitMode::Manual) {
        return;
    }

    const int cur = cfg_.viewport->state().page_index;
    const int count = cfg_.viewport->page_count_hint();
    const float vp_w = static_cast<float>(cfg_.viewport->width());
    const float vp_h = static_cast<float>(cfg_.viewport->height());
    if (vp_w <= 0.0f || vp_h <= 0.0f) {
        return;
    }

    std::unordered_set<TileKey, TileKeyHash> seen;
    for (const auto &r : *out) {
        seen.insert(r.key);
    }

    const int deltas[] = {1, -1};
    for (int delta : deltas) {
        const int page_index = cur + delta;
        if (page_index < 0 || page_index >= count) {
            continue;
        }

        docs::PageInfo page{};
        if (cfg_.model->load_page(page_index, &page) != ESP_OK || page.levels.empty()) {
            continue;
        }

        const float pw = std::max(1.0f, static_cast<float>(page.master_width));
        const float ph = std::max(1.0f, static_cast<float>(page.master_height));
        float scale = 1.0f;
        float vx = 0.0f;
        float vy = 0.0f;

        if (mode == FitMode::Page) {
            scale = std::min(vp_w / pw, vp_h / ph);
            vx = (vp_w - pw * scale) * 0.5f;
            vy = (vp_h - ph * scale) * 0.5f;
        } else {
            float src_w = page.content_box[2] - page.content_box[0];
            if (src_w <= 1.0f) {
                src_w = pw;
            }
            scale = vp_w / src_w;
            vx = -page.content_box[0] * scale;
            if (delta > 0) {
                vy = -page.content_box[1] * scale;
            } else {
                vy = -(ph * scale - vp_h);
            }
            const float scaled_h = ph * scale;
            if (scaled_h <= vp_h) {
                vy = (vp_h - scaled_h) * 0.5f;
            } else {
                vy = std::clamp(vy, vp_h - scaled_h, 0.0f);
            }
        }

        const float inv = 1.0f / std::max(scale, 0.0001f);
        const Rect vis{
            .x = (-vx) * inv,
            .y = (-vy) * inv,
            .w = vp_w * inv,
            .h = vp_h * inv,
        };

        std::vector<TileRequest> neighbor;
        collect_visible_rect(page, page_index, vis, scale, TileRequestPriority::Low, &neighbor);
        for (auto &n : neighbor) {
            if (seen.insert(n.key).second) {
                out->push_back(std::move(n));
            }
        }
    }
}

void tile_manager_submit_io(const TileRequest &req)
{
    QueueHandle_t q = (req.priority == TileRequestPriority::High) ? s_io_high : s_io_low;
    if (!q) {
        return;
    }
    if (!inflight_try_add(req.key)) {
        return;
    }
    TileIoMsg msg{};
    msg.key = req.key;
    msg.priority = req.priority;
    msg.generation = req.generation;
    msg.byte_offset = req.byte_offset;
    msg.byte_length = req.byte_length;
    strncpy(msg.file_path, req.file_path.c_str(), sizeof(msg.file_path) - 1);
    if (xQueueSend(q, &msg, 0) != pdTRUE) {
        inflight_remove(req.key);
    }
}

void tile_manager_on_decoded(std::shared_ptr<DecodedTile> tile)
{
    if (s_cfg.cache && tile) {
        s_cfg.cache->insert(tile);
    }
    if (tile) {
        inflight_remove(tile->key);
    }
    if (s_decoded_notify) {
        s_decoded_notify();
    }
}

void tile_manager_set_decoded_notify(void (*fn)())
{
    s_decoded_notify = fn;
}

static bool receive_io_msg(TileIoMsg *msg)
{
    if (xQueueReceive(s_io_high, msg, 0) == pdTRUE) {
        return true;
    }
    if (xQueueReceive(s_io_low, msg, 0) == pdTRUE) {
        return true;
    }
    if (xQueueReceive(s_io_high, msg, pdMS_TO_TICKS(2)) == pdTRUE) {
        return true;
    }
    return xQueueReceive(s_io_low, msg, pdMS_TO_TICKS(2)) == pdTRUE;
}

/** Keep one open tiles.bin handle so packed pages avoid fopen-per-tile. */
struct BlobFdCache {
    char path[320] = {};
    FILE *fp = nullptr;
};

static BlobFdCache s_blob_fd{};

static void blob_fd_close()
{
    if (s_blob_fd.fp) {
        fclose(s_blob_fd.fp);
        s_blob_fd.fp = nullptr;
        s_blob_fd.path[0] = '\0';
    }
}

static FILE *blob_fd_open(const char *path)
{
    if (s_blob_fd.fp && strncmp(s_blob_fd.path, path, sizeof(s_blob_fd.path)) == 0) {
        return s_blob_fd.fp;
    }
    blob_fd_close();
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return nullptr;
    }
    strncpy(s_blob_fd.path, path, sizeof(s_blob_fd.path) - 1);
    s_blob_fd.fp = fp;
    return fp;
}

static void tile_io_task(void *arg)
{
    (void)arg;
    TileIoMsg msg{};
    for (;;) {
        if (!receive_io_msg(&msg)) {
            continue;
        }

        if (s_generation && msg.generation != s_generation->load()) {
            inflight_remove(msg.key);
            continue;  // stale after pan/zoom/page change
        }

        int64_t t0 = now_us();
        const bool packed = msg.byte_length > 0;
        ESP_LOGD(TAG, "open %s%s", msg.file_path, packed ? " (span)" : "");

        FILE *f = nullptr;
        bool owns_file = false;
        if (packed) {
            f = blob_fd_open(msg.file_path);
        } else {
            f = fopen(msg.file_path, "rb");
            owns_file = true;
        }
        if (!f) {
            diag::metrics().tile_read_fail++;
            ESP_LOGW(TAG, "missing tile %s", msg.file_path);
            inflight_remove(msg.key);
            continue;
        }

        size_t read_len = 0;
        long seek_off = 0;
        if (packed) {
            seek_off = static_cast<long>(msg.byte_offset);
            read_len = msg.byte_length;
            if (fseek(f, seek_off, SEEK_SET) != 0) {
                diag::metrics().tile_read_fail++;
                inflight_remove(msg.key);
                continue;
            }
        } else {
            if (fseek(f, 0, SEEK_END) != 0) {
                fclose(f);
                inflight_remove(msg.key);
                continue;
            }
            long sz = ftell(f);
            if (sz <= 0) {
                fclose(f);
                inflight_remove(msg.key);
                continue;
            }
            if (fseek(f, 0, SEEK_SET) != 0) {
                fclose(f);
                inflight_remove(msg.key);
                continue;
            }
            read_len = static_cast<size_t>(sz);
        }

        auto *decode = new DecodeMsg();
        decode->meta = msg;
        decode->len = read_len;
        decode->bytes = static_cast<uint8_t *>(
            heap_caps_malloc(decode->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!decode->bytes) {
            decode->bytes = static_cast<uint8_t *>(malloc(decode->len));
        }
        if (!decode->bytes) {
            delete decode;
            if (owns_file) {
                fclose(f);
            }
            inflight_remove(msg.key);
            continue;
        }
        if (fread(decode->bytes, 1, decode->len, f) != decode->len) {
            free(decode->bytes);
            delete decode;
            if (owns_file) {
                fclose(f);
            }
            diag::metrics().tile_read_fail++;
            inflight_remove(msg.key);
            continue;
        }
        if (owns_file) {
            fclose(f);
        }
        diag::metrics().sd_read_us += static_cast<uint64_t>(now_us() - t0);
        diag::metrics().sd_reads++;

        if (s_generation && msg.generation != s_generation->load()) {
            free(decode->bytes);
            delete decode;
            inflight_remove(msg.key);
            continue;
        }

        if (!s_decode_queue || xQueueSend(s_decode_queue, &decode, portMAX_DELAY) != pdTRUE) {
            free(decode->bytes);
            delete decode;
            inflight_remove(msg.key);
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
            inflight_remove(payload->meta.key);
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
            inflight_remove(payload->meta.key);
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
    s_io_high = xQueueCreate(24, sizeof(TileIoMsg));
    s_io_low = xQueueCreate(16, sizeof(TileIoMsg));
    s_decode_queue = xQueueCreate(8, sizeof(DecodeMsg *));
    if (!s_io_high || !s_io_low || !s_decode_queue) {
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
