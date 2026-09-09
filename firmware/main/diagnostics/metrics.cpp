#include "metrics.hpp"

namespace diag {

static Metrics s_metrics;

Metrics &metrics()
{
    return s_metrics;
}

void metrics_reset()
{
    s_metrics = Metrics{};
}

}  // namespace diag
