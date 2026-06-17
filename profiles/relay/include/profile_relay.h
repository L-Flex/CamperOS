#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "profile_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAMPER_RELAY_SETTINGS_KEY  "relay_cfg"

typedef struct {
    uint8_t button_bind;
    uint8_t relay_bind;
    uint8_t reserved[6];
} relay_profile_config_t;

const profile_ops_t *profile_relay_get_ops(void);

/** Relay state accessors for Zigbee reporting */
bool profile_relay_get_state(void);
uint8_t profile_relay_get_endpoint(void);

#ifdef __cplusplus
}
#endif
