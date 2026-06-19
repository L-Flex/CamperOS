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
} ds18b20_svc_deps_t;

esp_err_t ds18b20_svc_start(const ds18b20_svc_deps_t *deps, uint8_t gpio_pin,
                            uint32_t poll_interval_sec);
esp_err_t ds18b20_svc_stop(void);
esp_err_t ds18b20_svc_set_pin(uint8_t gpio_pin);

#ifdef __cplusplus
}
#endif
