#include "touch_port.hpp"
#include "board_config.hpp"
#include "expander.hpp"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch";

namespace hw {

static esp_lcd_touch_handle_t s_touch = nullptr;
static lv_indev_t *s_indev = nullptr;
static esp_lcd_touch_io_gt911_config_t s_gt911_io_cfg{};

static esp_err_t drive_int_pin(int level)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = 1ULL << board::PIN_TOUCH_IRQ;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "int gpio");
    return gpio_set_level(static_cast<gpio_num_t>(board::PIN_TOUCH_IRQ), level);
}

// GT911 latches 0x5D if INT is low (0x14 if high) on the RST rising edge.
// RST is on the CH422G expander, so the stock GT911 driver skips this sequence.
static esp_err_t reset_gt911_select_addr(int int_level)
{
    ESP_RETURN_ON_ERROR(drive_int_pin(int_level), TAG, "drive int");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(expander().reset_touch(), TAG, "touch reset");
    ESP_RETURN_ON_ERROR(drive_int_pin(int_level), TAG, "hold int");
    vTaskDelay(pdMS_TO_TICKS(60));
    return ESP_OK;
}

static uint8_t probe_gt911_addr(i2c_master_bus_handle_t bus)
{
    if (i2c_master_probe(bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, 100) == ESP_OK) {
        return ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    }
    if (i2c_master_probe(bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, 100) == ESP_OK) {
        return ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
    }
    return 0;
}

// Report rotation-0 (physical 800x480) samples. LVGL's indev_pointer_proc remaps
// them with disp->rotation so widget hit-testing matches the rotated layout.
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (!s_touch) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    esp_lcd_touch_read_data(s_touch);
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t cnt = 0;
    const bool pressed = esp_lcd_touch_get_coordinates(s_touch, &x, &y, nullptr, &cnt, 1);
    if (!pressed || cnt == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    data->point.x = static_cast<int32_t>(x);
    data->point.y = static_cast<int32_t>(y);
    data->state = LV_INDEV_STATE_PRESSED;
}

static esp_err_t create_gt911(i2c_master_bus_handle_t bus, uint8_t addr, lv_display_t *display)
{
    esp_lcd_panel_io_handle_t tp_io = nullptr;
    esp_lcd_panel_io_i2c_config_t io_cfg = {};
    io_cfg.dev_addr = addr;
    io_cfg.control_phase_bytes = 1;
    io_cfg.dc_bit_offset = 0;
    io_cfg.lcd_cmd_bits = 16;
    io_cfg.flags.disable_control_phase = 1;
    io_cfg.scl_speed_hz = 100 * 1000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &tp_io), TAG, "tp io");

    s_gt911_io_cfg.dev_addr = addr;

    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max = board::LCD_H_RES;
    tp_cfg.y_max = board::LCD_V_RES;
    tp_cfg.rst_gpio_num = GPIO_NUM_NC;
    tp_cfg.int_gpio_num = static_cast<gpio_num_t>(board::PIN_TOUCH_IRQ);
    tp_cfg.levels.reset = 0;
    tp_cfg.levels.interrupt = 0;
    tp_cfg.flags.swap_xy = 0;
    tp_cfg.flags.mirror_x = 0;
    tp_cfg.flags.mirror_y = 0;
    tp_cfg.driver_data = &s_gt911_io_cfg;

    esp_err_t err = esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_touch);
    if (err != ESP_OK) {
        esp_lcd_panel_io_del(tp_io);
        return err;
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = display,
        .handle = s_touch,
    };
    s_indev = lvgl_port_add_touch(&touch_cfg);
    if (!s_indev) {
        ESP_LOGE(TAG, "lvgl_port_add_touch failed");
        return ESP_FAIL;
    }

    lv_indev_set_read_cb(s_indev, touch_read_cb);

    ESP_LOGI(TAG, "GT911 touch registered at 0x%02X indev=%p max=%ux%u",
             addr, (void *)s_indev, (unsigned)tp_cfg.x_max, (unsigned)tp_cfg.y_max);
    return ESP_OK;
}

esp_err_t touch_port_init(lv_display_t *display)
{
    i2c_master_bus_handle_t bus = expander_i2c_bus();
    if (!bus) {
        ESP_LOGE(TAG, "I2C bus not ready");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(reset_gt911_select_addr(0), TAG, "addr 0x5D reset");

    uint8_t addr = probe_gt911_addr(bus);
    if (addr == 0) {
        ESP_LOGW(TAG, "GT911 not at 0x5D; retrying with INT high for 0x14");
        ESP_RETURN_ON_ERROR(reset_gt911_select_addr(1), TAG, "addr 0x14 reset");
        addr = probe_gt911_addr(bus);
    }

    if (addr == 0) {
        ESP_LOGE(TAG, "GT911 not found at 0x5D or 0x14");
        gpio_reset_pin(static_cast<gpio_num_t>(board::PIN_TOUCH_IRQ));
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "GT911 probed at 0x%02X", addr);
    return create_gt911(bus, addr, display);
}

esp_lcd_touch_handle_t touch_handle()
{
    return s_touch;
}

lv_indev_t *touch_indev()
{
    return s_indev;
}

void touch_port_set_portrait(bool portrait)
{
    // LVGL remaps rotation-0 touch samples in indev_pointer_proc. Do not swap
    // GT911 axes here or hit-testing will be transformed twice.
    (void)portrait;
}

}  // namespace hw
