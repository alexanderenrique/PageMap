#include "jpeg_decoder.hpp"

#include "esp_log.h"
#include <cstdio>
#include <cstring>

#if __has_include("esp_jpeg_dec.h")
#include "esp_jpeg_dec.h"
#include "esp_jpeg_common.h"
#define ESP_READER_HAS_HW_JPEG 1
#else
#define ESP_READER_HAS_HW_JPEG 0
#endif

static const char *TAG = "jpeg";

namespace reader {

static esp_err_t decode_with_hw(const uint8_t *data, size_t len, JpegDecodeResult *out)
{
#if ESP_READER_HAS_HW_JPEG
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    cfg.rotate = JPEG_ROTATE_0D;

    jpeg_dec_handle_t dec = nullptr;
    jpeg_error_t jret = jpeg_dec_open(&cfg, &dec);
    if (jret != JPEG_ERR_OK) {
        return ESP_FAIL;
    }

    jpeg_dec_io_t io = {};
    io.inbuf = const_cast<uint8_t *>(data);
    io.inbuf_len = len;

    jpeg_dec_header_info_t header = {};
    jret = jpeg_dec_parse_header(dec, &io, &header);
    if (jret != JPEG_ERR_OK) {
        jpeg_dec_close(dec);
        return ESP_FAIL;
    }

    out->width = header.width;
    out->height = header.height;
    const size_t px = static_cast<size_t>(header.width) * header.height;
    const size_t bytes = px * sizeof(uint16_t);
    void *aligned = jpeg_calloc_align(bytes, 16);
    if (!aligned) {
        jpeg_dec_close(dec);
        return ESP_ERR_NO_MEM;
    }
    io.outbuf = static_cast<uint8_t *>(aligned);
    jret = jpeg_dec_process(dec, &io);
    jpeg_dec_close(dec);
    if (jret != JPEG_ERR_OK) {
        jpeg_free_align(aligned);
        return ESP_FAIL;
    }
    auto *rgb = static_cast<uint16_t *>(aligned);
    out->rgb565.assign(rgb, rgb + px);
    jpeg_free_align(aligned);
    return ESP_OK;
#else
    (void)data;
    (void)len;
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t decode_software_stub(const uint8_t *data, size_t len, JpegDecodeResult *out)
{
    // Minimal baseline JPEG marker scan for SOF0 dimensions only; full decode requires HW
    if (len < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t i = 2;
    while (i + 9 < len) {
        if (data[i] != 0xFF) {
            ++i;
            continue;
        }
        uint8_t marker = data[i + 1];
        if (marker == 0xC0 || marker == 0xC2) {
            int h = (data[i + 5] << 8) | data[i + 6];
            int w = (data[i + 7] << 8) | data[i + 8];
            out->width = w;
            out->height = h;
            out->rgb565.assign(static_cast<size_t>(w) * h, 0x7BEF);  // gray placeholder
            ESP_LOGW(TAG, "software stub placeholder %dx%d", w, h);
            return ESP_OK;
        }
        if (marker == 0xD9) {
            break;
        }
        uint16_t seg_len = (data[i + 2] << 8) | data[i + 3];
        i += 2 + seg_len;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t jpeg_decode_buffer(const uint8_t *data, size_t len, JpegDecodeResult *out)
{
    if (!data || !out || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = decode_with_hw(data, len, out);
    if (err == ESP_OK) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "HW decode failed, trying fallback");
    return decode_software_stub(data, len, out);
}

esp_err_t jpeg_decode_file(const char *path, JpegDecodeResult *out)
{
    if (!path || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "open %s failed", path);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    if (fread(buf.data(), 1, buf.size(), f) != buf.size()) {
        fclose(f);
        return ESP_FAIL;
    }
    fclose(f);

    return jpeg_decode_buffer(buf.data(), buf.size(), out);
}

}  // namespace reader
