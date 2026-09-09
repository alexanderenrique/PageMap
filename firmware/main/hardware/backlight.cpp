#include "backlight.hpp"
#include "expander.hpp"

#include "esp_log.h"
#include <algorithm>
#include <cmath>

static const char *TAG = "backlight";

namespace hw {

esp_err_t Backlight::init()
{
    power_on_ = false;
    return set_power(true);
}

esp_err_t Backlight::set_power(bool on)
{
    power_on_ = on;
    esp_err_t err = expander().set_backlight(on);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "backlight %s failed: %s", on ? "on" : "off", esp_err_to_name(err));
    }
    return err;
}

esp_err_t Backlight::set_brightness_percent(int percent)
{
    brightness_percent_ = std::clamp(percent, 0, 100);
    return ESP_OK;
}

float Backlight::dim_overlay_alpha() const
{
    if (!power_on_) {
        return 1.0f;
    }
    // Perceptual curve: UI 0% -> ~90% overlay (visible minimum), 100% -> 0 overlay
    const float t = brightness_percent_ / 100.0f;
    const float curved = std::pow(t, 1.8f);
    return (1.0f - curved) * 0.90f;
}

Backlight &backlight()
{
    static Backlight instance;
    return instance;
}

}  // namespace hw
