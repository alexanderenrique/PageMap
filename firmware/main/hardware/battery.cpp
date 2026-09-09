#include "battery.hpp"

namespace hw {

BatteryStatus battery_read()
{
    return BatteryStatus{
        .available = false,
        .voltage_v = 0.0f,
        .percent = -1,
    };
}

}  // namespace hw
