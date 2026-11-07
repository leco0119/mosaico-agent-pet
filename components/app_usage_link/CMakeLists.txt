idf_component_register(SRCS "app_usage_link.c" "usage_store.c"
    INCLUDE_DIRS "include"
    REQUIRES app_pet esp-gsp
    PRIV_REQUIRES esp_iris log nvs_flash freertos)
