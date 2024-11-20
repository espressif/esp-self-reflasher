/*
 * SPDX-FileCopyrightText: 2017-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"

#include "string.h"

#include "sys/param.h"

#include "spi_flash_mmap.h"
#include "esp_flash.h"

#include "self_reflasher.h"

static const char *TAG = "boot_swap_embedded_example";
extern const uint8_t bin_start[] asm(EXAMPLE_TARGET_BIN_START);
extern const uint8_t bin_end[] asm(EXAMPLE_TARGET_BIN_END);

void app_main(void)
{
    ESP_LOGI(TAG, "Boot swap embedded bin example start");

    esp_err_t err;

    addr_region_t destination_region = {
        .region_address = CONFIG_EXAMPLE_BIN_DEST_ADDRESS,
        .region_size =    CONFIG_EXAMPLE_BIN_DEST_MAX_SIZE,
    };

    // spi_flash_cache2phys converts the virtual address to the real physical address
    // where the build system places the embedded binary
    addr_region_t embedded_bin_region = {
        .region_address = (uint32_t)spi_flash_cache2phys((void *)bin_start),
        .region_size =    bin_end - bin_start,
    };

    esp_self_reflasher_config_t self_reflasher_config = {
        .dest_region = destination_region,
        .src_region = embedded_bin_region,
        .src_bin_size = embedded_bin_region.region_size
    };

    err = esp_self_reflasher_directly_copy_to_region(&self_reflasher_config);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Copying to region failed: %s", esp_err_to_name(err));
        abort();
    }

    ESP_LOGI(TAG, "Overwritting succeed, Rebooting...");
    esp_restart();
    while (1) {
    }
}
