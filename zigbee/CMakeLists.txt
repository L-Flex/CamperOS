idf_component_register(
    SRCS "src/zigbee_mgr.c" "src/zigbee_device.c" "src/zigbee_camper_cluster.c"
    INCLUDE_DIRS "include"
    REQUIRES config events storage gpio logger ota profiles esp-zigbee-lib nvs_flash freertos
)
