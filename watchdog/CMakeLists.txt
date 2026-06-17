idf_component_register(
    SRCS "src/watchdog.c"
    INCLUDE_DIRS "include"
    REQUIRES config esp_system
)
