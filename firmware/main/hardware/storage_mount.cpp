#include "storage_mount.hpp"
#include "board_config.hpp"
#include "expander.hpp"

#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "storage";

namespace hw {

static sdmmc_card_t *s_card = nullptr;
static bool s_mounted = false;

esp_err_t storage_mount_init()
{
    if (s_mounted) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(expander().init(), TAG, "expander for SD CS");

    // SD_CS active low via CH422G EXIO4
    ESP_RETURN_ON_ERROR(expander().set_sd_cs_active(true), TAG, "assert SD CS");

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = board::PIN_SD_MOSI,
        .miso_io_num = board::PIN_SD_MISO,
        .sclk_io_num = board::PIN_SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi bus init: %s", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = GPIO_NUM_NC;  // CS handled by CH422G
    slot_cfg.host_id = static_cast<spi_host_device_t>(host.slot);

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    ret = esp_vfs_fat_sdspi_mount(board::SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s (continuing without SD)", esp_err_to_name(ret));
        s_card = nullptr;
        s_mounted = false;
        return ret;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SD mounted at %s", board::SD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t storage_mount_deinit()
{
    if (!s_mounted) {
        return ESP_OK;
    }
    esp_err_t err = esp_vfs_fat_sdcard_unmount(board::SD_MOUNT_POINT, s_card);
    s_mounted = false;
    s_card = nullptr;
    expander().set_sd_cs_active(false);
    return err;
}

bool storage_is_mounted()
{
    return s_mounted;
}

sdmmc_card_t *storage_card()
{
    return s_card;
}

}  // namespace hw
