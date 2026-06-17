idf_component_register(
    SRCS "src/storage.c"
    INCLUDE_DIRS "include"
    REQUIRES config nvs_flash log
)
