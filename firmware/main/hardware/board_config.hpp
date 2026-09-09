#pragma once

#include <stdint.h>

namespace board {

// Display resolution
constexpr int LCD_H_RES = 800;
constexpr int LCD_V_RES = 480;
constexpr int LCD_PCLK_HZ = 16 * 1000 * 1000;

// RGB sync and control
constexpr int PIN_LCD_VSYNC = 3;
constexpr int PIN_LCD_HSYNC = 46;
constexpr int PIN_LCD_DE = 5;
constexpr int PIN_LCD_PCLK = 7;

// RGB565 data bus (B0..B4, G0..G5, R0..R4 order per Waveshare mapping)
constexpr int PIN_LCD_DATA[] = {
    14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40,
};
constexpr int LCD_DATA_WIDTH = 16;

// I2C (touch + CH422G)
constexpr int PIN_I2C_SDA = 8;
constexpr int PIN_I2C_SCL = 9;
constexpr int PIN_TOUCH_IRQ = 4;
constexpr uint8_t I2C_PORT_NUM = 0;
constexpr uint32_t I2C_FREQ_HZ = 400000;

// CH422G I2C addresses
constexpr uint8_t CH422G_ADDR_CONFIG = 0x24;
constexpr uint8_t CH422G_ADDR_DATA = 0x38;

// CH422G EXIO bit assignments (Waveshare ESP32-S3-Touch-LCD-7)
constexpr uint8_t CH422G_EXIO_TP_RST = (1 << 1);
constexpr uint8_t CH422G_EXIO_LCD_BL = (1 << 2);
constexpr uint8_t CH422G_EXIO_LCD_RST = (1 << 3);
constexpr uint8_t CH422G_EXIO_SD_CS = (1 << 4);

// Backlight on pattern: output mode + data 0x1E
constexpr uint8_t CH422G_OUTPUT_MODE = 0x01;
constexpr uint8_t CH422G_BACKLIGHT_ON = 0x1E;
constexpr uint8_t CH422G_BACKLIGHT_OFF = 0x00;

// SD card SPI (CS via CH422G EXIO4, active low)
constexpr int PIN_SD_MOSI = 11;
constexpr int PIN_SD_SCK = 12;
constexpr int PIN_SD_MISO = 13;

constexpr const char *SD_MOUNT_POINT = "/sdcard";

// Reader viewport usable area (full screen; chrome overlays)
constexpr int READER_VIEWPORT_W = LCD_H_RES;
constexpr int READER_VIEWPORT_H = LCD_V_RES;

}  // namespace board
