idf_component_register(
    SRCS "app_voice.c" "voice_audio.c" "voice_cloud.c"
    INCLUDE_DIRS "include"
    REQUIRES app_wifi
    PRIV_REQUIRES esp-mosaico-bsp esp_xiaozhi esp_audio_codec esp_event esp_http_client
                  esp_ringbuf esp_timer esp_hw_support nvs_flash mbedtls freertos log)
