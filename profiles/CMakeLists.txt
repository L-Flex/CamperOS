idf_component_register(
    SRCS
        "src/profile_mgr.c"
        "relay/src/profile_relay.c"
    INCLUDE_DIRS
        "include"
        "relay/include"
    REQUIRES config events storage gpio logger nvs_flash sensors
)
