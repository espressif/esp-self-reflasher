/*
 * SPDX-FileCopyrightText: 2017-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <esp_http_client.h>
#include <esp_partition.h>
#include <sdkconfig.h>
#include <esp_attr.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t  region_address;
    size_t    region_size;
} addr_region_t;

typedef struct {
    const esp_http_client_config_t *http_config;   /*!< ESP HTTP client configuration */
    const esp_partition_t          *target_partition;
    addr_region_t                  src_region;
    addr_region_t                  dest_region;
    size_t                         src_bin_size;
} esp_self_reflasher_config_t;

typedef void *esp_self_reflasher_handle_t;

esp_err_t esp_self_reflasher_init(const esp_self_reflasher_config_t *self_reflasher_config, esp_self_reflasher_handle_t *handle);

esp_err_t esp_self_reflasher_download_bin(esp_self_reflasher_handle_t handle);

esp_err_t esp_self_reflasher_copy_to_region(esp_self_reflasher_handle_t handle);

esp_err_t esp_self_reflasher_upd_next_config(const esp_self_reflasher_config_t *self_reflasher_config, esp_self_reflasher_handle_t handle);

esp_err_t esp_self_reflasher_directly_copy_to_region(const esp_self_reflasher_config_t *self_reflasher_config);

#ifdef __cplusplus
}
#endif
