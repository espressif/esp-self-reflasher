/*
 * SPDX-FileCopyrightText: 2017-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"

#include "protocol_examples_common.h"
#include "string.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "sys/param.h"
#if CONFIG_EXAMPLE_CONNECT_WIFI
#include <sys/socket.h>
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#endif

#include "spi_flash_mmap.h"
#include "esp_flash.h"

#include "self_reflasher.h"

static const char *TAG = "ota_reflash_example";
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

extern esp_flash_t *esp_flash_default_chip;

#define BUFFER_SIZE         0x400      // 1KB

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

void ota_reflash_example_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting Self Reflasher example task");

    esp_http_client_config_t http_config = {
        .url = CONFIG_EXAMPLE_BOOTLOADER_UPGRADE_URL,
        // .cert_pem = (char *)server_cert_pem_start,
        .event_handler = _http_event_handler,
        .buffer_size = BUFFER_SIZE,    // HTTP buffer size
        .buffer_size_tx = BUFFER_SIZE, // Transmission buffer size
        .keep_alive_enable = true,
    };

#ifdef CONFIG_EXAMPLE_SKIP_COMMON_NAME_CHECK
    http_config.skip_cert_common_name_check = true;
#endif

    dest_region_t bootloader_region = {
        .dest_region_address = CONFIG_EXAMPLE_BOOTLOADER_DEST_ADDRESS,
        .dest_region_size =    CONFIG_EXAMPLE_BOOTLOADER_DEST_MAX_SIZE,
    };

    esp_self_reflasher_config_t self_reflasher_config = {
        .http_config = &http_config,
        .dest_region = bootloader_region,
    };

#if CONFIG_EXAMPLE_TARGET_PARTITION
    self_reflasher_config.target_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                      ESP_PARTITION_SUBTYPE_ANY,
                                                                      CONFIG_EXAMPLE_TARGET_PARTITION_LABEL);
#endif

    esp_self_reflasher_handle_t self_reflasher_handle = NULL;

    esp_err_t err = esp_self_reflasher_init(&self_reflasher_config, &self_reflasher_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Self reflasher initialization failed: %s", esp_err_to_name(err));
    }

    err = esp_self_reflasher_download_bin(self_reflasher_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Self reflasher download failed: %s", esp_err_to_name(err));
        return;
    }

    err = esp_self_reflasher_copy_to_region(self_reflasher_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Self reflasher copying to region failed: %s", esp_err_to_name(err));
        return;
    }

    esp_http_client_config_t http_config_app = {
        .url = CONFIG_EXAMPLE_APP_UPGRADE_URL,
        // .cert_pem = (char *)server_cert_pem_start,
        .event_handler = _http_event_handler,
        .buffer_size = BUFFER_SIZE,    // HTTP buffer size
        .buffer_size_tx = BUFFER_SIZE, // Transmission buffer size
        .keep_alive_enable = true,
    };

#ifdef CONFIG_EXAMPLE_SKIP_COMMON_NAME_CHECK
    http_config_app.skip_cert_common_name_check = true;
#endif

    dest_region_t app_region = {
        .dest_region_address = CONFIG_EXAMPLE_APP_DEST_ADDRESS,
        .dest_region_size =    CONFIG_EXAMPLE_APP_DEST_MAX_SIZE,
    };

    esp_self_reflasher_config_t self_reflasher_config_app = {
        .http_config = &http_config_app,
        .dest_region = app_region,
    };

    err = esp_self_reflasher_upd_next_config(&self_reflasher_config_app, self_reflasher_handle);

    err = esp_self_reflasher_download_bin(self_reflasher_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Self reflasher download failed: %s", esp_err_to_name(err));
        return;
    }

    err = esp_self_reflasher_copy_to_region(self_reflasher_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Self reflasher copying to region failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "MCUboot+Image overwritting succeed, Rebooting...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Self Reflasher example app_main start");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

#  if CONFIG_EXAMPLE_CONNECT_WIFI
    /* Ensure to disable any WiFi power save mode, this allows best throughput
     * and hence timings for overall OTA operation.
     */
    esp_wifi_set_ps(WIFI_PS_NONE);
#  endif // CONFIG_EXAMPLE_CONNECT_WIFI

    xTaskCreate(&ota_reflash_example_task, "ota_reflash_example_task", 8192, NULL, 5, NULL);
}
