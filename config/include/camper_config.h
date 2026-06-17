#pragma once

/**
 * @file camper_config.h
 * @brief Global compile-time configuration for CamperNode OS.
 */

#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Firmware name */
#define CAMPER_FIRMWARE_NAME        "CamperNode OS"

/** Firmware version (semver) */
#define CAMPER_FIRMWARE_VERSION     "0.1.0"

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

/** CamperNode manufacturer-specific Zigbee cluster ID */
#define CAMPER_ZIGBEE_CLUSTER_ID      0xFC00

#ifdef __cplusplus
}
#endif
