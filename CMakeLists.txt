idf_component_register(SRCS "src/self_reflasher.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_partition esp_http_client spi_flash
                    PRIV_REQUIRES log)
