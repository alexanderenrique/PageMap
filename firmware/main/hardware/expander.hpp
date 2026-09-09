#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

namespace hw {

class Expander {
public:
    esp_err_t init();
    esp_err_t set_output(uint8_t mask);
    esp_err_t set_backlight(bool on);
    esp_err_t set_sd_cs_active(bool active_low);
    esp_err_t reset_touch();
    esp_err_t reset_lcd();
    uint8_t output_mask() const { return output_mask_; }

private:
    esp_err_t write_config(uint8_t value);
    esp_err_t write_data(uint8_t value);
    bool initialized_ = false;
    uint8_t output_mask_ = 0;
};

Expander &expander();
i2c_master_bus_handle_t expander_i2c_bus();

}  // namespace hw
