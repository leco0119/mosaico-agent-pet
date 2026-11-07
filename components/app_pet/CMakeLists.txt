idf_component_register(SRCS "app_pet.c" "pet_settings.c" "pet_pages.c" "status_link.c"
        "usage/app_usage.c" "usage/usage_payload.c"
    INCLUDE_DIRS "include"
    REQUIRES esp-gsp
    PRIV_REQUIRES app_voice esp_iris freertos log)
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_bundle.cmake")
mosaico_gsp_add_ui_bundle(${COMPONENT_LIB} "ui/main.json;ui/usage.json")
