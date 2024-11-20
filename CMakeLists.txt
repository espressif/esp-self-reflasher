idf_component_register(SRCS "src/self_reflasher.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_partition
                    esp_http_client
                    spi_flash
                    PRIV_REQUIRES log
                    LDFRAGMENTS esp_self_reflasher.lf)

require_idf_targets(esp32 esp32s2 esp32s3 esp32c3 esp32c6 esp32h2)
