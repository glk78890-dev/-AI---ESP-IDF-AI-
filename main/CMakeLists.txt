idf_component_register(SRCS "station_example_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_client json nvs_flash esp_wifi mbedtls)
