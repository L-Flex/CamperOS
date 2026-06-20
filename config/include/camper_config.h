#pragma once

/**
 * @file camper_config.h
 * @brief Global compile-time configuration for CamperNode OS.
 */

#include "board_config.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Firmware name */
#define CAMPER_FIRMWARE_NAME        "CamperNode OS"

/** Firmware version (semver) */
#if CONFIG_CAMPER_OTA_BASE_FIRMWARE
#define CAMPER_FIRMWARE_VERSION     "0.1.1"
#define CAMPER_OTA_FILE_VERSION     0x00000101U
#define CAMPER_FEATURE_MOSFET_OD    0
#else
#define CAMPER_FIRMWARE_VERSION     "0.2.0"
#define CAMPER_OTA_FILE_VERSION     0x00000200U
#define CAMPER_FEATURE_MOSFET_OD    1
#endif

/** Zigbee OTA image metadata — must match the OTA file served by ZHA. */
#define CAMPER_OTA_MANUFACTURER     0x147BU
#define CAMPER_OTA_IMAGE_TYPE       0xC602U
#define CAMPER_OTA_HW_VERSION       0x0001U

/** NVS schema version — increment on breaking storage changes */
#define CAMPER_NVS_SCHEMA_VERSION   1

/** Maximum event subscribers per event type */
#define CAMPER_EVENT_MAX_SUBSCRIBERS  16

/** Async event queue depth */
#define CAMPER_EVENT_QUEUE_DEPTH      32

/** Maximum node name length (stored in NVS) */
#define CAMPER_NODE_NAME_MAX_LEN      32

/** Maximum Zigbee endpoints per node */
#define CAMPER_ZIGBEE_MAX_ENDPOINTS   8

/**
 * CamperNode custom ZCL cluster ID (endpoint 10).
 * Must be in 0x8000..0xFBFF (ZBOSS custom, non-manufacturer-specific).
 * Do not use 0xFC00 (Tunnel) or >= 0xFC00 (manufacturer-specific range).
 */
#define CAMPER_ZIGBEE_CLUSTER_ID      0xFB00

#ifdef __cplusplus
}
#endif
