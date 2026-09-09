#pragma once

#include <optional>

namespace hw {

struct BatteryStatus {
    bool available;
    float voltage_v;
    int percent;
};

BatteryStatus battery_read();

}  // namespace hw
