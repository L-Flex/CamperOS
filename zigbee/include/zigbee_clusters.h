#pragma once

#include "camper_features.h"
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
    /** Bitmask: legacy; derived from sensor_gpio6/7. */
    ZB_ATTR_FEATURE_FLAGS    = 0x000A,
    /** Active DS18B20 pin (read-only: 0, 6, or 7). */
    ZB_ATTR_TEMP_GPIO        = 0x000B,
    /** Per-pin I/O map: "3:O,10:I". Set once; reboot after change. GPIO9 always input. */
    ZB_ATTR_PIN_MAP           = 0x000C,
    /** @deprecated — use ZB_ATTR_PIN_MAP */
    ZB_ATTR_OUTPUT_PIN_LIST   = 0x000C,
    ZB_ATTR_INPUT_PIN_LIST    = 0x000D,
    /** Output bitmap — bit0 = first output pin, HA writable. */
    ZB_ATTR_OUTPUT_STATE     = 0x000E,
    /** Input bitmap — bit0 = BOOT/GPIO9, read-only, reported on change. */
    ZB_ATTR_INPUT_STATE      = 0x000F,
    /** GPIO 6: 0=off, 1=DS18B20, 2=DHT22. */
    ZB_ATTR_SENSOR_GPIO6     = 0x0010,
    /** GPIO 7: 0=off, 1=DS18B20, 2=DHT22. */
    ZB_ATTR_SENSOR_GPIO7     = 0x0011,
    /** Active DHT22 pin (read-only: 0, 6, or 7). */
    ZB_ATTR_DHT_GPIO         = 0x0012,
} zigbee_camper_attr_t;

/** @deprecated — same attribute id as ZB_ATTR_SENSOR_GPIO6 (0x0010). */
#define ZB_ATTR_SENSOR_TYPE   ZB_ATTR_SENSOR_GPIO6
/** @deprecated — same attribute id as ZB_ATTR_SENSOR_GPIO6 (0x0010). */
#define ZB_ATTR_DHT_ACTIVE    ZB_ATTR_SENSOR_GPIO6

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
