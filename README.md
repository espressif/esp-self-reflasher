# esp-self-reflasher

`esp-self-reflasher` enables a project to do a full system reflash. As it involves risky flash operations, such as overwriting the bootloader region, that could brick the device, this needs to be use carefully.

There are two ways of operation:

- Download reflashing image;
- Embedded reflashing image.

For a example of each usage check the [examples](/examples) directory.

## Configuration constraints

It is mandatory that the following SPI Flash configuration is enabled:

- `SPI_FLASH_DANGEROUS_WRITE_ALLOWED` - "Writing to dangerous flash regions"

Otherwise the flash writing operations may not be effective depending on the destination addresses.

## WARNING

As mentioned above, the use of the `esp-self-reflasher` may involve risky flash operations that may brick the device if not correctly configured and planned beforehand by the user.

No NVS data, configuration, or other existing data in the destination flash region are transported neither kept. It is also on the user responsibility to handle it if needed.

## Download reflashing image

This mode downloads the **reflashing image** binary, thus expects a network connection and the HTTP endpoint to it.

`esp-self-reflasher` fetches from the system a valid OTA partition that does not conflict with the final destination neither the current execution partition, so the **reflashing image** can be staged into it.
After the download the reflash image can be copied to the final destination address.

Overall, the process can be described as following:

1. Firstly, the required configuration needs to be filled:
    - Set a HTTP configuration (`esp_http_client_config_t`) with the target URL;
    ```c
        esp_http_client_config_t http_config = {
            .url = CONFIG_EXAMPLE_UPGRADE_URL,
            .event_handler = _http_event_handler,
            .buffer_size = BUFFER_SIZE,    // HTTP buffer size
            .buffer_size_tx = BUFFER_SIZE, // Transmission buffer size
            .keep_alive_enable = true,
        };
    ```
    - Set the destination address and size (`addr_region_t`);
    ```c
        addr_region_t example_region = {
            .region_address = CONFIG_EXAMPLE_DEST_ADDRESS,
            .region_size =    CONFIG_EXAMPLE_DEST_MAX_SIZE,
        };
    ```
    - Set a `esp-self-reflasher` configuration (`esp_self_reflasher_config_t`) with the previous mentioned information;
    ```c
        esp_self_reflasher_config_t self_reflasher_config = {
            .http_config = &http_config,
            .dest_region = example_region,
        };
    ```
    - Optionally, the target partition where the download will be placed can also be set into the `esp-self-reflasher` configuration;
2. `esp_self_reflasher_init` fetches a valid OTA partition if its not previously set, erases the partition found and sets the component handle with the configuration information;
```c
    esp_self_reflasher_handle_t self_reflasher_handle = NULL;
    esp_err_t err = esp_self_reflasher_init(&self_reflasher_config, &self_reflasher_handle);
```
3. `esp_self_reflasher_download_bin` initializes the HTTP client and starts the **reflashing image** download from the endpoint;
```c
    err = esp_self_reflasher_download_bin(self_reflasher_handle);
```
4. `esp_self_reflasher_copy_to_region` erases the final destination flash region and copy the downloaded **reflashing image** to it;
```c
    err = esp_self_reflasher_copy_to_region(self_reflasher_handle);
```

It is also possible to set and repeat the process for downloading other `reflashing images` to another destination:

5. Set a new HTTP configuration, a new destination address and a new `esp-self-reflasher` configuration (like step 1);
6. `esp_self_reflasher_upd_next_config` updates the configuration and the component handle;
```c
    err = esp_self_reflasher_upd_next_config(&self_reflasher_new_config, self_reflasher_handle);
```
7. Repeat steps 3 and 4;

### Constraints

The reflash image size should fit into an existing partition.
The final destination address and size must not conflict with where the current code is executed from.

## Embedded reflashing image

In this way of operation, the target **reflashing image** is embedded to the application for direct copy to the destination (like a single shot execution), so it can pushed over any firmware update method, however as the binary load will increase the application size itself, the partition size constraints must be considered.

The example `boot_swap_embedded_example` can be checked for how to embedded the **reflashing image** binary data into the project.

The process can be described as following:

1. Required configuration needs to be filled:
    - Set the source address from the embedded binary and size (`addr_region_t`), ensure that the address is an address from flash;
    - Set the destination address and size (`addr_region_t`);
    - Set a `esp-self-reflasher` configuration (`esp_self_reflasher_config_t`) with the previous mentioned information;
2. `esp_self_reflasher_directly_copy_to_region` erases the destination as it copies the **reflashing image** to it.
```c
    err = esp_self_reflasher_directly_copy_to_region(&self_reflasher_config);
```

### Constraints

As mentioned, the binary load will increase the application size itself, so the target device's partition size must be observed.
