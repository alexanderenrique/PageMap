#pragma once

#include "esp_err.h"
#include <stdint.h>

namespace hw {

class Backlight {
public:
    esp_err_t init();
    esp_err_t set_power(bool on);
    bool power() const { return power_on_; }

    // 0-100 UI percent; stored in NVS; drives software dim overlay (not PWM)
    esp_err_t set_brightness_percent(int percent);
    int brightness_percent() const { return brightness_percent_; }

    // Overlay alpha 0.0 (full bright) .. ~0.90 at UI 0%
    float dim_overlay_alpha() const;

private:
    bool power_on_ = false;
    int brightness_percent_ = 80;
};

Backlight &backlight();

}  // namespace hw
