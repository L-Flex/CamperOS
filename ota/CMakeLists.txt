idf_component_register(
    SRCS "src/ota_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES config events storage app_update esp_partition
)
