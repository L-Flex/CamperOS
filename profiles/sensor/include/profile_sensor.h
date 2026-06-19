#pragma once

#include "profile_interface.h"
#include "camper_features.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t sensor_bind;
    uint8_t reserved[7];
} sensor_profile_config_t;

#define CAMPER_SENSOR_SETTINGS_KEY  "sensor_cfg"

const profile_ops_t *profile_sensor_get_ops(void);
uint8_t profile_sensor_get_endpoint(void);

#ifdef __cplusplus
}
#endif
