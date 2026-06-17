idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES core config events storage gpio logger watchdog profiles zigbee ota
)
