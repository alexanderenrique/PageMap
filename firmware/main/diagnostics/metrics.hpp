#pragma once

#include <cstdint>

namespace diag {

struct Metrics {
    uint64_t sd_reads = 0;
    uint64_t sd_read_us = 0;
    uint64_t decodes = 0;
    uint64_t decode_us = 0;
    uint64_t decode_fail = 0;
    uint64_t tile_read_fail = 0;
    uint64_t cache_hits = 0;
    uint64_t cache_misses = 0;
};

Metrics &metrics();
void metrics_reset();

}  // namespace diag
