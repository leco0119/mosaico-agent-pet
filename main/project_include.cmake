# The application image belongs to main_app (ota_0). Vibe Mode has its own build.
# Retain the SDK checker and give its generic app check the correct subtype.
if(NOT PARTITION_TABLE_CHECK_SIZES_TOOL_PATH)
    message(FATAL_ERROR "ESP-IDF partition checker must be available before the application build hook")
endif()
set(PARTITION_TABLE_CHECK_SIZES_TOOL_PATH
    "${CMAKE_CURRENT_LIST_DIR}/../tools/check_app_partition.py"
    "${PARTITION_TABLE_CHECK_SIZES_TOOL_PATH}")
