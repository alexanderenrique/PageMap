#include "display_port.hpp"
#include "board_config.hpp"
#include "expander.hpp"

#include "esp_check.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "display";

namespace hw {

static esp_lcd_panel_handle_t s_panel = nullptr;
static lv_display_t *s_lv_display = nullptr;

static esp_err_t init_rgb_panel(esp_lcd_panel_handle_t *panel_out)
{
    esp_lcd_rgb_panel_config_t panel_config = {};
    panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
    panel_config.timings.pclk_hz = board::LCD_PCLK_HZ;
    panel_config.timings.h_res = board::LCD_H_RES;
    panel_config.timings.v_res = board::LCD_V_RES;
    panel_config.timings.hsync_pulse_width = 4;
    panel_config.timings.hsync_back_porch = 8;
    panel_config.timings.hsync_front_porch = 8;
    panel_config.timings.vsync_pulse_width = 4;
    panel_config.timings.vsync_back_porch = 8;
    panel_config.timings.vsync_front_porch = 8;
    panel_config.timings.flags.pclk_active_neg = 1;
    panel_config.data_width = board::LCD_DATA_WIDTH;
    panel_config.bits_per_pixel = 16;
    panel_config.num_fbs = 2;
    // 20 lines: more DMA slack so UART/LVGL work is less likely to underrun and black the panel.
    panel_config.bounce_buffer_size_px = board::LCD_H_RES * 20;
    panel_config.psram_trans_align = 64;
    panel_config.hsync_gpio_num = board::PIN_LCD_HSYNC;
    panel_config.vsync_gpio_num = board::PIN_LCD_VSYNC;
    panel_config.de_gpio_num = board::PIN_LCD_DE;
    panel_config.pclk_gpio_num = board::PIN_LCD_PCLK;
    panel_config.disp_gpio_num = -1;
    for (int i = 0; i < board::LCD_DATA_WIDTH; ++i) {
        panel_config.data_gpio_nums[i] = board::PIN_LCD_DATA[i];
    }
    panel_config.flags.fb_in_psram = 1;
    panel_config.flags.double_fb = 1;

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_config, panel_out), TAG, "new rgb panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel_out), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel_out), TAG, "panel init");
    return ESP_OK;
}

esp_err_t display_port_init(DisplayHandles *out)
{
    ESP_RETURN_ON_ERROR(expander().reset_lcd(), TAG, "lcd reset via expander");

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 10240;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl port init");

    ESP_RETURN_ON_ERROR(init_rgb_panel(&s_panel), TAG, "rgb panel");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = nullptr,
        .panel_handle = s_panel,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(board::LCD_H_RES * board::LCD_V_RES),
        .double_buffer = true,
        .trans_size = 0,
        .hres = board::LCD_H_RES,
        .vres = board::LCD_V_RES,
        .monochrome = false,
        .rotation =
            {
                .swap_xy = false,
                .mirror_x = false,
                .mirror_y = false,
            },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags =
            {
                .buff_dma = false,
                .buff_spiram = true,
                .sw_rotate = false,
                .swap_bytes = false,
                .full_refresh = false,
                // Direct + avoid_tearing: draw into the idle RGB FB and swap on VSYNC.
                // Partial mode with avoid_tearing writes dirty strips into the scanned
                // framebuffer and shows up as blurry blobs along the top of the panel.
                .direct_mode = true,
            },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags =
            {
                .bb_mode = true,
                .avoid_tearing = true,
            },
    };

    s_lv_display = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (!s_lv_display) {
        ESP_LOGE(TAG, "lvgl_port_add_disp_rgb failed");
        return ESP_FAIL;
    }

    if (out) {
        out->panel = s_panel;
        out->lv_display = s_lv_display;
    }
    ESP_LOGI(TAG, "display initialized %dx%d (direct+vsync, bounce=%d lines)",
             board::LCD_H_RES, board::LCD_V_RES, 20);
    return ESP_OK;
}

void display_port_deinit()
{
    lvgl_port_deinit();
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = nullptr;
    }
    s_lv_display = nullptr;
}

}  // namespace hw
