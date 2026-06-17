#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct event_bus event_bus_t;
typedef struct storage storage_t;
typedef struct profile_mgr profile_mgr_t;
typedef struct gpio_mgr gpio_mgr_t;
typedef struct logger logger_t;
typedef struct ota_mgr ota_mgr_t;

typedef struct {
    event_bus_t   *event_bus;
    storage_t     *storage;
    profile_mgr_t *profile_mgr;
    gpio_mgr_t    *gpio_mgr;
    logger_t      *logger;
    ota_mgr_t     *ota_mgr;
} zigbee_device_deps_t;

esp_err_t zigbee_device_start(const zigbee_device_deps_t *deps);
void zigbee_device_set_joined(bool joined);
bool zigbee_device_is_joined(void);
void zigbee_device_report_on_off(uint8_t endpoint, bool on);

#ifdef __cplusplus
}
#endif
