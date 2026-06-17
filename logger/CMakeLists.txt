idf_component_register(
    SRCS "src/logger.c"
    INCLUDE_DIRS "include"
    REQUIRES config esp_common log
)
