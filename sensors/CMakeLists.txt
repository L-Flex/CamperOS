idf_component_register(
    SRCS "src/ds18b20_svc.c" "src/dht22_svc.c"
    INCLUDE_DIRS "include"
    REQUIRES config events logger esp_rom
    PRIV_REQUIRES esp_driver_gpio
)
