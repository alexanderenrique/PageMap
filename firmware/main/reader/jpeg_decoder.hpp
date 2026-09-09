#pragma once

#include "esp_err.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace reader {

struct JpegDecodeResult {
    int width = 0;
    int height = 0;
    std::vector<uint16_t> rgb565;
};

esp_err_t jpeg_decode_file(const char *path, JpegDecodeResult *out);
esp_err_t jpeg_decode_buffer(const uint8_t *data, size_t len, JpegDecodeResult *out);

}  // namespace reader
