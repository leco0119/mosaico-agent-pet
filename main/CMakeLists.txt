idf_component_register(SRCS "main.c"
    PRIV_REQUIRES app_display app_pet app_voice app_wifi esp_iris esp_mosaico_app_recovery
        esp_mosaico_gsp_bundle esp_mosaico_gsp_iris nvs_flash log partition_table)
