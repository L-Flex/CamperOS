#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include "event_bus.h"
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct profile_mgr profile_mgr_t;
typedef struct gpio_mgr gpio_mgr_t;
typedef struct logger logger_t;
typedef struct ota_mgr ota_mgr_t;
typedef struct zigbee_mgr zigbee_mgr_t;

typedef struct {
    event_bus_t   *event_bus;
    storage_t     *storage;
    profile_mgr_t *profile_mgr;
    gpio_mgr_t    *gpio_mgr;
    logger_t      *logger;
    ota_mgr_t     *ota_mgr;
} zigbee_mgr_deps_t;

zigbee_mgr_t *zigbee_mgr_create(const zigbee_mgr_deps_t *deps);
void zigbee_mgr_destroy(zigbee_mgr_t *mgr);

esp_err_t zigbee_mgr_init(zigbee_mgr_t *mgr);
esp_err_t zigbee_mgr_start(zigbee_mgr_t *mgr);
esp_err_t zigbee_mgr_stop(zigbee_mgr_t *mgr);

bool zigbee_mgr_is_joined(const zigbee_mgr_t *mgr);
int8_t zigbee_mgr_get_rssi(const zigbee_mgr_t *mgr);

void zigbee_mgr_report_relay_state(bool on);

#ifdef __cplusplus
}
#endif
