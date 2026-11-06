idf_component_register(
    SRCS "app_wifi.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_event esp_netif esp_wifi esp_timer esp_mosaico_app_recovery
                  nvs_flash mbedtls freertos log)
