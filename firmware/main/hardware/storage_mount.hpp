#pragma once

#include "esp_err.h"
#include "sdmmc_cmd.h"

namespace hw {

esp_err_t storage_mount_init();
esp_err_t storage_mount_deinit();
bool storage_is_mounted();
sdmmc_card_t *storage_card();

}  // namespace hw
