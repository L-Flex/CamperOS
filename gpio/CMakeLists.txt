idf_component_register(
    SRCS "src/gpio_mgr.c" "src/gpio_io.c" "src/status_led.c"
    INCLUDE_DIRS "include"
    REQUIRES config events storage esp_driver_gpio esp_timer led_strip
)
