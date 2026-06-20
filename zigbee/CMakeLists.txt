idf_component_register(
    SRCS "src/zigbee_mgr.c" "src/zigbee_device.c" "src/zigbee_camper_cluster.c"
         "src/zigbee_gpio_io.c" "src/zigbee_ota.c"
    INCLUDE_DIRS "include"
    REQUIRES config events storage gpio logger ota profiles esp-zigbee-lib nvs_flash freertos app_update
)
