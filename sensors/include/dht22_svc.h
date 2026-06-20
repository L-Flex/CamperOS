#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct event_bus event_bus_t;
typedef struct logger logger_t;

typedef struct {
    event_bus_t *event_bus;
    logger_t    *logger;
} dht22_svc_deps_t;

esp_err_t dht22_svc_start(const dht22_svc_deps_t *deps, uint8_t gpio_pin,
                          uint32_t poll_interval_sec);
esp_err_t dht22_svc_stop(void);

#ifdef __cplusplus
}
#endif
