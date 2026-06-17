idf_component_register(
    SRCS "src/gpio_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES config events storage esp_driver_gpio esp_timer
)
