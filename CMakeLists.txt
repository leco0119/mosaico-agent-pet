cmake_minimum_required(VERSION 3.16)
set(MOSAICO_UTILS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-utils")
set(MOSAICO_BSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-bsp")
if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.application.defaults")
endif()
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_compiler.cmake")
list(APPEND EXTRA_COMPONENT_DIRS
    "${MOSAICO_UTILS_ROOT}/ESP-Iris/components/esp_iris"
    "${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/components/esp_mosaico_app_recovery"
    "${MOSAICO_UTILS_ROOT}/mosaico-tools/components/esp_mosaico_gsp_bundle"
    "${MOSAICO_UTILS_ROOT}/mosaico-tools/components/esp_mosaico_gsp_iris")
include("${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/cmake/mosaico_idf_project.cmake")
project(Agent_Pet VERSION 1.2.3)
# Match the activation version on both official-cloud requests, keeping the
# registry component and the other HTTP clients untouched.
idf_component_get_property(pet_xiaozhi_dir espressif__esp_xiaozhi COMPONENT_DIR)
idf_component_get_property(pet_xiaozhi_lib espressif__esp_xiaozhi COMPONENT_LIB)
set_property(SOURCE "${pet_xiaozhi_dir}/src/esp_xiaozhi_info.c"
    TARGET_DIRECTORY "${pet_xiaozhi_lib}" APPEND PROPERTY COMPILE_OPTIONS
    "-include" "${CMAKE_CURRENT_LIST_DIR}/components/app_voice/xiaozhi_activation_header.h")
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/system_update.cmake")
