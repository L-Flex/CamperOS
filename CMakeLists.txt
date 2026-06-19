# CamperNode OS - Root CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "config"
    "core"
    "events"
    "storage"
    "gpio"
    "zigbee"
    "ota"
    "logger"
    "watchdog"
    "profiles"
    "sensors"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(campernode_os)
