#pragma once

/**
 * @file camper_features.h
 * @brief Optional board capabilities (combinable on one node).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Feature flags — stored in NVS and cluster 0xFB00 attribute 0x000A */
#define CAMPER_FEATURE_TEMPERATURE  (1U << 0)

#define CAMPER_ZB_TEMP_ENDPOINT_COMBO  2U
#define CAMPER_ZB_TEMP_ENDPOINT_SOLO   1U

#define CAMPER_KEY_FEATURE_FLAGS  "features"
#define CAMPER_KEY_TEMP_GPIO      "temp_gpio"
#define CAMPER_KEY_BUTTON_GPIO    "button_gpio"
#define CAMPER_KEY_OUTPUT_GPIO    "output_gpio"
#define CAMPER_KEY_PIN_MAP         "pin_map"
/** @deprecated — migrated to pin_map on load */
#define CAMPER_KEY_OUTPUT_PIN_LIST "output_pins"
#define CAMPER_KEY_INPUT_PIN_LIST  "input_pins"
#define CAMPER_KEY_OUTPUT_STATE    "output_state"

#ifdef __cplusplus
}
#endif
