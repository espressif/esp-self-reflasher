/*
 * SPDX-FileCopyrightText: 2017-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#include <sys/param.h>
#include <string.h>
#include <errno.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "spi_flash_mmap.h"
#include "self_reflasher.h"

static const char *TAG = "self_reflasher";

struct esp_self_reflasher_handle {
    const esp_http_client_config_t *http_config;   /*!< ESP HTTP client configuration */
    esp_http_client_handle_t       http_client;
    const esp_partition_t          *target_partition;
    dest_region_t                  dest_region;
    bool                           running_from_ram;
    size_t                         total_bin_data_size;
    uint32_t                       partition_curr_download_addr;
    uint32_t                       partition_curr_copy_offset;
};

typedef struct esp_self_reflasher_handle esp_self_reflasher_t;

#define BUFFER_SIZE                               0x400      // 1KB

extern esp_flash_t *esp_flash_default_chip;

IRAM_ATTR void http_cleanup(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

static const esp_partition_t* esp_self_reflasher_get_running_partition(void)
{
    static const esp_partition_t *curr_partition = NULL;

    /*
     * Currently running partition is unlikely to change across reset cycle,
     * so it can be cached here, and avoid lookup on every flash write operation.
     */
    if (curr_partition != NULL) {
        return curr_partition;
    }

    /* Find the flash address of this exact function. By definition that is part
       of the currently running firmware. Then find the enclosing partition. */
    size_t phys_offs = spi_flash_cache2phys(esp_self_reflasher_get_running_partition);

    assert (phys_offs != SPI_FLASH_CACHE2PHYS_FAIL); /* indicates cache2phys lookup is buggy */

    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP,
                                                     ESP_PARTITION_SUBTYPE_ANY,
                                                     NULL);
    assert(it != NULL); /* has to be at least one app partition */

    while (it != NULL) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p->address <= phys_offs && p->address + p->size > phys_offs) {
            esp_partition_iterator_release(it);
            curr_partition = p;
            return p;
        }
        it = esp_partition_next(it);
    }

    abort(); /* Partition table is invalid or corrupt */
}

IRAM_ATTR uint8_t esp_self_reflasher_get_ota_partition_count()
{
    uint8_t count = 0;
    for (esp_partition_subtype_t t = ESP_PARTITION_SUBTYPE_APP_OTA_0;
         t != ESP_PARTITION_SUBTYPE_APP_OTA_MAX;
         t++) {
        const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, t, NULL);
        if (p == NULL) {
            continue;
        }
        count++;
    }
    return count;
}

IRAM_ATTR const esp_partition_t* esp_self_reflasher_get_next_partition(const esp_partition_t *start_from)
{
    const esp_partition_t *default_ota = NULL;
    bool next_is_result = false;
    if (start_from == NULL) {
        start_from = esp_self_reflasher_get_running_partition();
    }

    assert (start_from != NULL);
    /* 'start_from' points to actual partition table data in flash */

    /* Two possibilities: either we want the OTA partition immediately after the current running OTA partition, or we
       want the first OTA partition in the table (for the case when the last OTA partition is the running partition, or
       if the current running partition is not OTA.)

       This loop iterates subtypes instead of using esp_partition_find, so we
       get all OTA partitions in a known order (low slot to high slot).
    */

    for (esp_partition_subtype_t t = ESP_PARTITION_SUBTYPE_APP_OTA_0;
         t != ESP_PARTITION_SUBTYPE_APP_OTA_MAX;
         t++) {
        const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, t, NULL);
        if (p == NULL) {
            continue;
        }

        if (default_ota == NULL) {
            /* Default to first OTA partition we find,
               will be used if nothing else matches */
            default_ota = p;
        }

        if (p == start_from) {
            /* Next OTA partition is the one to use */
            next_is_result = true;
        } else if (next_is_result) {
            return p;
        }
    }

    return default_ota;
}

#define IS_REGION_OVERLAPPING(src_start, src_end, dest_start, dest_end)    ((src_start >= dest_start && src_start <= dest_end) || \
                                                                           (src_end >= dest_start && src_end <= dest_end))

IRAM_ATTR esp_err_t esp_self_reflasher_init(const esp_self_reflasher_config_t *self_reflasher_config, esp_self_reflasher_handle_t *handle)
{
    esp_err_t err = ESP_OK;

    if (handle == NULL || self_reflasher_config == NULL || self_reflasher_config->http_config == NULL) {
        ESP_LOGE(TAG, "%s: Invalid argument", __func__);
        if (handle) {
            *handle = NULL;
        }
        return ESP_ERR_INVALID_ARG;
    }

    esp_self_reflasher_t *self_reflasher_handle = calloc(1, sizeof(esp_self_reflasher_t));
    if (!self_reflasher_handle) {
        ESP_LOGE(TAG, "%s: Couldn't allocate memory to upgrade data buffer", __func__);
        *handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    self_reflasher_handle->dest_region.dest_region_address = self_reflasher_config->dest_region.dest_region_address;
    self_reflasher_handle->dest_region.dest_region_size = self_reflasher_config->dest_region.dest_region_size;
    self_reflasher_handle->http_config = self_reflasher_config->http_config;
    self_reflasher_handle->http_client = NULL;

    if (self_reflasher_config->target_partition != NULL) {
        if (!IS_REGION_OVERLAPPING(self_reflasher_config->target_partition->address,
                                   self_reflasher_config->target_partition->address + self_reflasher_config->target_partition->size,
                                   self_reflasher_handle->dest_region.dest_region_address,
                                   self_reflasher_handle->dest_region.dest_region_address + self_reflasher_handle->dest_region.dest_region_size)) {
            self_reflasher_handle->target_partition = self_reflasher_config->target_partition;
        }
    } else {
        const esp_partition_t *part = NULL;
        uint8_t part_count = esp_self_reflasher_get_ota_partition_count();

        for (uint8_t i = 0; i < part_count; i++) {
            part = esp_self_reflasher_get_next_partition(part);
            // Check if the partition overlaps destination region
            if (!IS_REGION_OVERLAPPING(part->address,
                                       part->address + part->size,
                                       self_reflasher_handle->dest_region.dest_region_address,
                                       self_reflasher_handle->dest_region.dest_region_address + self_reflasher_handle->dest_region.dest_region_size)) {
                self_reflasher_handle->target_partition = part;
                break;
            }
        }
    }
    if (self_reflasher_handle->target_partition == NULL) {
        err = ESP_ERR_NOT_FOUND;
        ESP_LOGE(TAG, "%s: Partition overlaps destination", __func__);
        free(self_reflasher_handle);
        *handle = NULL;
        return err;
    }
    ESP_LOGI(TAG, "src_start 0x%08lx src_end 0x%08lx dest_start 0x%08lx dest_end 0x%08lx",
             self_reflasher_handle->target_partition->address,
             self_reflasher_handle->target_partition->address + self_reflasher_handle->target_partition->size,
             self_reflasher_handle->dest_region.dest_region_address,
             self_reflasher_handle->dest_region.dest_region_address + self_reflasher_handle->dest_region.dest_region_size);

    // Erase the partition before writing the first time
    err = esp_partition_erase_range(self_reflasher_handle->target_partition, 0, self_reflasher_handle->target_partition->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: Failed to erase partition: %s", __func__, esp_err_to_name(err));
        free(self_reflasher_handle);
        *handle = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Partition erased successfully");

    *handle = (esp_self_reflasher_handle_t)self_reflasher_handle;

    return err;
}

IRAM_ATTR esp_err_t esp_self_reflasher_download_bin(esp_self_reflasher_handle_t handle)
{
    esp_err_t err = ESP_OK;
    int status_code;
    char data[BUFFER_SIZE] = {0};
    uint32_t curr_offset = 0;

    esp_self_reflasher_t *self_reflasher_handle = (esp_self_reflasher_t *)handle;

    if (self_reflasher_handle == NULL ||
        self_reflasher_handle->target_partition == NULL ||
        self_reflasher_handle->http_config == NULL) {
        ESP_LOGE(TAG, "%s: Invalid argument", __func__);
        return ESP_ERR_INVALID_ARG;
    }

    self_reflasher_handle->http_client = esp_http_client_init(self_reflasher_handle->http_config);
    if (self_reflasher_handle->http_client == NULL) {
        ESP_LOGE(TAG, "%s: Failed to initialise HTTP connection", __func__);
        err = ESP_FAIL;
        return err;
    }

    err = esp_http_client_open(self_reflasher_handle->http_client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: Failed to open HTTP connection: %s", __func__, esp_err_to_name(err));
        esp_http_client_cleanup(self_reflasher_handle->http_client);
        return err;
    }

    self_reflasher_handle->partition_curr_copy_offset = self_reflasher_handle->partition_curr_download_addr;
    self_reflasher_handle->total_bin_data_size = 0;
    esp_http_client_fetch_headers(self_reflasher_handle->http_client);
    status_code = esp_http_client_get_status_code(self_reflasher_handle->http_client);
    if (status_code != HttpStatus_Ok) {
        ESP_LOGE(TAG, "%s: HTTP request error", __func__);
        return ESP_ERR_HTTP_CONNECT;
    }

    while (1) {
        int data_read = esp_http_client_read(self_reflasher_handle->http_client, data, BUFFER_SIZE);
        if (data_read < 0) {
            ESP_LOGE(TAG, "%s: Error: SSL data read error", __func__);
            http_cleanup(self_reflasher_handle->http_client);
            err = ESP_ERR_HTTP_EAGAIN;
            return err;
        } else if (data_read > 0) {
            // Ensure that we don't exceed the partition size
            if (self_reflasher_handle->total_bin_data_size + data_read > self_reflasher_handle->target_partition->size) {
                ESP_LOGE(TAG, "%s: Blob size exceeds partition size", __func__);
                return ESP_FAIL;
            }

            // Write the received data to the flash partition
            esp_err_t err = esp_partition_write(self_reflasher_handle->target_partition,
                                                self_reflasher_handle->partition_curr_download_addr + curr_offset,
                                                data, data_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s: Failed to write data to partition: %s", __func__, esp_err_to_name(err));
                return ESP_FAIL;
            }

            if (err != ESP_OK) {
                http_cleanup(self_reflasher_handle->http_client);
                ESP_LOGE(TAG, "%s: Error while writing to partition", __func__);
                return err;
            }
            curr_offset += data_read;
            self_reflasher_handle->total_bin_data_size += data_read;

            ESP_LOGD(TAG, "Chunk length written: %d partial downloaded length %d", data_read, self_reflasher_handle->total_bin_data_size);
        } else if (data_read == 0) {
           /*
            * As esp_http_client_read never returns negative error code, we rely on
            * `errno` to check for underlying transport connectivity closure if any
            */
            if (errno == ECONNRESET || errno == ENOTCONN) {
                ESP_LOGE(TAG, "%s: Connection closed, errno = %d", __func__, errno);
                break;
            }
            if (esp_http_client_is_complete_data_received(self_reflasher_handle->http_client) == true) {
                ESP_LOGI(TAG, "Connection closed");
                break;
            }
        }
    }
    self_reflasher_handle->partition_curr_download_addr += self_reflasher_handle->total_bin_data_size;

    ESP_LOGI(TAG, "Total downloaded binary length: %d (0x%x)", self_reflasher_handle->total_bin_data_size, self_reflasher_handle->total_bin_data_size);
    if (esp_http_client_is_complete_data_received(self_reflasher_handle->http_client) != true) {
        ESP_LOGE(TAG, "%s: Error in receiving complete data", __func__);
        http_cleanup(self_reflasher_handle->http_client);
        err = ESP_ERR_HTTP_WRITE_DATA;
        return err;
    }
    ESP_LOGI(TAG, "File downloaded successfully");
    http_cleanup(self_reflasher_handle->http_client);

    return err;
}

IRAM_ATTR esp_err_t esp_self_reflasher_copy_to_region(esp_self_reflasher_handle_t handle)
{
    esp_err_t err;
    char data[BUFFER_SIZE] = {0};
    size_t data_len = BUFFER_SIZE;

    esp_self_reflasher_t *self_reflasher_handle = (esp_self_reflasher_t *)handle;

    if (self_reflasher_handle == NULL || self_reflasher_handle->target_partition == NULL) {
        ESP_LOGE(TAG, "%s: Invalid argument", __func__);
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t part_curr_offset = self_reflasher_handle->partition_curr_copy_offset;
    uint32_t address_write = self_reflasher_handle->dest_region.dest_region_address;

    ESP_LOGI(TAG, "Starting copy 0x%08x bytes from address 0x%08lx to address 0x%08lx",
             self_reflasher_handle->total_bin_data_size, self_reflasher_handle->target_partition->address + part_curr_offset, address_write);

    // Erase the flash region before writing
    err = esp_flash_erase_region(esp_flash_default_chip,
                                 self_reflasher_handle->dest_region.dest_region_address,
                                 self_reflasher_handle->dest_region.dest_region_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: Failed to erase flash destination region, error: %s", __func__, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Flash destination region erased successfully");

    while (part_curr_offset < self_reflasher_handle->partition_curr_copy_offset + self_reflasher_handle->total_bin_data_size) {
        data_len = MIN((self_reflasher_handle->partition_curr_copy_offset + self_reflasher_handle->total_bin_data_size - part_curr_offset),
                        BUFFER_SIZE);
        ESP_LOGD(TAG, "Reading 0x%08x bytes (data_len) from address 0x%08lx",
                 data_len, self_reflasher_handle->target_partition->address + part_curr_offset);

        err = esp_partition_read(self_reflasher_handle->target_partition, part_curr_offset, data, data_len);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: Failed to read from flash, address: 0x%08lx, error: %s", __func__, self_reflasher_handle->target_partition->address + part_curr_offset, esp_err_to_name(err));
            return err;
        }

        err = esp_flash_write(esp_flash_default_chip, data, address_write, data_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: Failed to write to flash, address: 0x%08lx, error: %s", __func__, address_write, esp_err_to_name(err));
            return err;
        }
        ESP_LOGD(TAG, "Data written to address 0x%08lx successfully", address_write);

        part_curr_offset += data_len;
        address_write += data_len;
    }

#if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG)
    // Read first bytes to verify
    uint32_t read_data = 0;
    err = esp_flash_read(esp_flash_default_chip, &read_data, self_reflasher_handle->dest_region.dest_region_address, 4);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: Failed to read from flash, error: %s", __func__, esp_err_to_name(err));
        return err;
    }
    ESP_LOGD(TAG, "Data read from flash: 0x%08lx", read_data);
#endif

    ESP_LOGI(TAG, "Data copied from partition address 0x%08lx offset 0x%08lx to region: 0x%08lx",
             self_reflasher_handle->target_partition->address, self_reflasher_handle->partition_curr_copy_offset,
             self_reflasher_handle->dest_region.dest_region_address);

    return err;
}

IRAM_ATTR esp_err_t esp_self_reflasher_upd_next_config(const esp_self_reflasher_config_t *self_reflasher_config, esp_self_reflasher_handle_t handle)
{
    esp_err_t err;
    esp_self_reflasher_t *self_reflasher_handle = (esp_self_reflasher_t *)handle;

    ESP_LOGI(TAG, "Updating configuration for next download");
    if (self_reflasher_handle == NULL ||
        self_reflasher_config == NULL ||
        self_reflasher_handle->http_config == NULL) {
        ESP_LOGE(TAG, "%s: Invalid argument", __func__);
        return ESP_ERR_INVALID_ARG;
    }

    self_reflasher_handle->dest_region.dest_region_address = self_reflasher_config->dest_region.dest_region_address;
    self_reflasher_handle->dest_region.dest_region_size = self_reflasher_config->dest_region.dest_region_size;
    self_reflasher_handle->http_config = self_reflasher_config->http_config;
    self_reflasher_handle->http_client = NULL;

    const esp_partition_t *handle_partition = self_reflasher_handle->target_partition;
    if (self_reflasher_config->target_partition == NULL) {
        ESP_LOGD(TAG, "src_start 0x%08lx src_end 0x%08lx dest_start 0x%08lx dest_end 0x%08lx",
                 self_reflasher_handle->target_partition->address,
                 self_reflasher_handle->target_partition->address + self_reflasher_handle->target_partition->size,
                 self_reflasher_handle->dest_region.dest_region_address,
                 self_reflasher_handle->dest_region.dest_region_address + self_reflasher_handle->dest_region.dest_region_size);

        // Check if current target partition overlaps with the new destination region
        if (IS_REGION_OVERLAPPING(self_reflasher_handle->target_partition->address,
                                  self_reflasher_handle->target_partition->address + self_reflasher_handle->target_partition->size,
                                  self_reflasher_handle->dest_region.dest_region_address,
                                  self_reflasher_handle->dest_region.dest_region_address + self_reflasher_handle->dest_region.dest_region_size)) {
            ESP_LOGI(TAG, "Current used partition overlaps with destination. Trying to fetch next partition...");

            // Search for a new target partition
            self_reflasher_handle->target_partition = NULL;
            const esp_partition_t *part = NULL;
            uint8_t part_count = esp_self_reflasher_get_ota_partition_count();

            for (uint8_t i = 0; i < part_count; i++) {
                part = esp_self_reflasher_get_next_partition(part);
                // Check if the partition overlaps destination region
                if (!IS_REGION_OVERLAPPING(part->address,
                                           part->address + part->size,
                                           self_reflasher_handle->dest_region.dest_region_address,
                                           self_reflasher_handle->dest_region.dest_region_address + self_reflasher_handle->dest_region.dest_region_size)) {
                    self_reflasher_handle->target_partition = part;
                    break;
                }
            }
        }
    } else {
        if (!IS_REGION_OVERLAPPING(self_reflasher_config->target_partition->address,
                                   self_reflasher_config->target_partition->address + self_reflasher_config->target_partition->size,
                                   self_reflasher_handle->dest_region.dest_region_address,
                                   self_reflasher_handle->dest_region.dest_region_address + self_reflasher_handle->dest_region.dest_region_size)) {
            self_reflasher_handle->target_partition = self_reflasher_config->target_partition;
        } else {
            // New target partition overlaps the destination region
            self_reflasher_handle->target_partition = NULL;
        }
    }

    if (self_reflasher_handle->target_partition == NULL) {
        ESP_LOGE(TAG, "%s: Partition overlaps destination", __func__);
        free(self_reflasher_handle);
        return ESP_ERR_NOT_FOUND;
    }

    if (handle_partition != self_reflasher_handle->target_partition) {
        ESP_LOGI(TAG, "New partition target set");

        self_reflasher_handle->partition_curr_copy_offset = 0;
        self_reflasher_handle->partition_curr_download_addr = 0;
        // Erase the new set partition before writing the first time
        err = esp_partition_erase_range(self_reflasher_handle->target_partition, 0, self_reflasher_handle->target_partition->size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: Failed to erase partition: %s", __func__, esp_err_to_name(err));
            free(self_reflasher_handle);
            return err;
        }

        ESP_LOGI(TAG, "Partition erased successfully");
    }

    return ESP_OK;
}
