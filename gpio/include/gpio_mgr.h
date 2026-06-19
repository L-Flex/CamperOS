#pragma once

/**
 * @file gpio_mgr.h
 * @brief Dynamic GPIO manager for CamperNode OS.
 */

#include "esp_err.h"
#include "gpio_types.h"
#include "event_bus.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gpio_mgr gpio_mgr_t;
typedef struct storage storage_t;

gpio_mgr_t *gpio_mgr_create(event_bus_t *event_bus);
void gpio_mgr_destroy(gpio_mgr_t *mgr);

esp_err_t gpio_mgr_init(gpio_mgr_t *mgr);
esp_err_t gpio_mgr_load_from_storage(gpio_mgr_t *mgr, storage_t *storage);
esp_err_t gpio_mgr_resolve_pin_config(storage_t *storage, gpio_pin_config_t *cfg, size_t *count);
esp_err_t gpio_mgr_apply_config(gpio_mgr_t *mgr, const gpio_pin_config_t *cfg, size_t count);

esp_err_t gpio_mgr_write(gpio_mgr_t *mgr, uint8_t logical_id, bool value);
esp_err_t gpio_mgr_read(gpio_mgr_t *mgr, uint8_t logical_id, bool *value);
esp_err_t gpio_mgr_set_pwm(gpio_mgr_t *mgr, uint8_t logical_id, uint8_t percent);

#ifdef __cplusplus
}
#endif
