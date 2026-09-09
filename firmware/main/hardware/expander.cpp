#include "expander.hpp"
#include "board_config.hpp"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "expander";

namespace hw {

static i2c_master_bus_handle_t s_i2c_bus = nullptr;
static i2c_master_dev_handle_t s_dev_config = nullptr;
static i2c_master_dev_handle_t s_dev_data = nullptr;

static esp_err_t i2c_write_dev(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len)
{
    return i2c_master_transmit(dev, data, len, 100);
}

esp_err_t Expander::write_config(uint8_t value)
{
    if (!s_dev_config) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_write_dev(s_dev_config, &value, 1);
}

esp_err_t Expander::write_data(uint8_t value)
{
    if (!s_dev_data) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_write_dev(s_dev_data, &value, 1);
}

esp_err_t Expander::init()
{
    if (initialized_) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = static_cast<i2c_port_num_t>(board::I2C_PORT_NUM);
    bus_cfg.sda_io_num = static_cast<gpio_num_t>(board::PIN_I2C_SDA);
    bus_cfg.scl_io_num = static_cast<gpio_num_t>(board::PIN_I2C_SCL);
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.intr_priority = 0;
    bus_cfg.trans_queue_depth = 0;
    bus_cfg.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "i2c bus");

    i2c_device_config_t cfg_dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = board::CH422G_ADDR_CONFIG,
        .scl_speed_hz = board::I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &cfg_dev, &s_dev_config), TAG, "cfg dev");

    i2c_device_config_t data_dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = board::CH422G_ADDR_DATA,
        .scl_speed_hz = board::I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &data_dev, &s_dev_data), TAG, "data dev");

    ESP_RETURN_ON_ERROR(write_config(board::CH422G_OUTPUT_MODE), TAG, "output mode");

    // Hold TP_RST and LCD_RST high, backlight off, SD_CS inactive (high)
    output_mask_ = board::CH422G_EXIO_TP_RST | board::CH422G_EXIO_LCD_RST;
    ESP_RETURN_ON_ERROR(write_data(output_mask_), TAG, "initial outputs");

    vTaskDelay(pdMS_TO_TICKS(10));
    initialized_ = true;
    ESP_LOGI(TAG, "CH422G initialized");
    return ESP_OK;
}

esp_err_t Expander::set_output(uint8_t mask)
{
    output_mask_ = mask;
    return write_data(output_mask_);
}

esp_err_t Expander::set_backlight(bool on)
{
    // Preserve SD_CS and reset lines; only toggle backlight bit when possible.
    // Waveshare bring-up uses 0x1E for "backlight on" which asserts TP_RST, BL, LCD_RST,
    // and leaves SD_CS high (inactive). We then clear SD_CS separately for card access.
    if (on) {
        output_mask_ |= static_cast<uint8_t>(board::CH422G_EXIO_TP_RST | board::CH422G_EXIO_LCD_BL |
                                             board::CH422G_EXIO_LCD_RST);
    } else {
        output_mask_ &= ~static_cast<uint8_t>(board::CH422G_EXIO_LCD_BL);
    }
    return write_data(output_mask_);
}

esp_err_t Expander::set_sd_cs_active(bool active_low)
{
    if (active_low) {
        output_mask_ &= ~static_cast<uint8_t>(board::CH422G_EXIO_SD_CS);
    } else {
        output_mask_ |= board::CH422G_EXIO_SD_CS;
    }
    return write_data(output_mask_);
}

esp_err_t Expander::reset_touch()
{
    output_mask_ &= ~static_cast<uint8_t>(board::CH422G_EXIO_TP_RST);
    ESP_RETURN_ON_ERROR(write_data(output_mask_), TAG, "tp rst low");
    vTaskDelay(pdMS_TO_TICKS(10));
    output_mask_ |= board::CH422G_EXIO_TP_RST;
    ESP_RETURN_ON_ERROR(write_data(output_mask_), TAG, "tp rst high");
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

esp_err_t Expander::reset_lcd()
{
    output_mask_ &= ~static_cast<uint8_t>(board::CH422G_EXIO_LCD_RST);
    ESP_RETURN_ON_ERROR(write_data(output_mask_), TAG, "lcd rst low");
    vTaskDelay(pdMS_TO_TICKS(10));
    output_mask_ |= board::CH422G_EXIO_LCD_RST;
    ESP_RETURN_ON_ERROR(write_data(output_mask_), TAG, "lcd rst high");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

Expander &expander()
{
    static Expander instance;
    return instance;
}

i2c_master_bus_handle_t expander_i2c_bus()
{
    return s_i2c_bus;
}

}  // namespace hw
