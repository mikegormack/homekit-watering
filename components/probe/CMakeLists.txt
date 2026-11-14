idf_component_register(
    SRCS ModbusMaster.cpp ProbeManager.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    REQUIRES esp_driver_uart nvs_flash app_update esp_partition esp_timer
)
