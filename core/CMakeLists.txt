idf_component_register(
    SRCS "src/camper_core.c"
    INCLUDE_DIRS "include"
    REQUIRES config events storage gpio logger watchdog profiles zigbee ota
                 esp_system esp_timer freertos
)
