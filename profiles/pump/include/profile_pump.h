#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "profile_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAMPER_PUMP_SETTINGS_KEY  "pump_cfg"
#define CAMPER_KEY_PUMP_STATE     "pump_on"

typedef struct {
    uint8_t button_bind;
    uint8_t pump_bind;
    uint8_t reserved[6];
} pump_profile_config_t;

const profile_ops_t *profile_pump_get_ops(void);

bool profile_pump_get_state(void);
uint8_t profile_pump_get_endpoint(void);

#ifdef __cplusplus
}
#endif
