#pragma once

#include "camper_config.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Zigbee command values carried in EVT_ZIGBEE_CMD data.int_val */
#define ZIGBEE_CMD_OFF      0
#define ZIGBEE_CMD_ON       1
#define ZIGBEE_CMD_TOGGLE   2
#define ZIGBEE_CMD_REBOOT   10
#define ZIGBEE_CMD_FACTORY  11
#define ZIGBEE_CMD_OTA      12

typedef enum {
    ZB_ATTR_NODE_NAME        = 0x0000,
    ZB_ATTR_PROFILE_ID       = 0x0001,
    ZB_ATTR_GPIO_CONFIG      = 0x0002,
    ZB_ATTR_CALIBRATION      = 0x0003,
    ZB_ATTR_UPTIME_SEC       = 0x0004,
    ZB_ATTR_LAST_RSSI        = 0x0005,
    ZB_ATTR_LOG_LEVEL        = 0x0006,
    ZB_ATTR_FIRMWARE_VERSION = 0x0007,
    /** Simple setup: GPIO pin numbers (0 = disabled). Builds gpio_config blob. */
    ZB_ATTR_BUTTON_GPIO      = 0x0008,
    ZB_ATTR_OUTPUT_GPIO      = 0x0009,
} zigbee_camper_attr_t;

typedef enum {
    ZB_CMD_REBOOT         = 0x00,
    ZB_CMD_FACTORY_RESET  = 0x01,
    ZB_CMD_TRIGGER_OTA    = 0x02,
} zigbee_camper_cmd_t;

#define CAMPER_ZB_CONFIG_ENDPOINT  10
#define CAMPER_ZB_HA_ENDPOINT      1

#ifdef __cplusplus
}
#endif
