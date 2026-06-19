#pragma once

/**
 * @file profile_mgr.h
 * @brief Active profile loader and switcher for CamperNode OS.
 */

#include "esp_err.h"
#include "profile_interface.h"
#include "camper_features.h"
#include "event_bus.h"
#include "storage.h"
#include "gpio_mgr.h"
#include "logger.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct profile_mgr profile_mgr_t;

profile_mgr_t *profile_mgr_create(const profile_ctx_t *base_ctx);
void profile_mgr_destroy(profile_mgr_t *mgr);

esp_err_t profile_mgr_init(profile_mgr_t *mgr);
esp_err_t profile_mgr_start(profile_mgr_t *mgr);
esp_err_t profile_mgr_stop(profile_mgr_t *mgr);

profile_id_t profile_mgr_get_active_id(const profile_mgr_t *mgr);
const char *profile_mgr_get_active_name(const profile_mgr_t *mgr);

esp_err_t profile_mgr_on_event(profile_mgr_t *mgr, const event_t *evt);

esp_err_t profile_mgr_get_entities(profile_mgr_t *mgr, zigbee_entity_desc_t *out, size_t *count);

profile_ctx_t *profile_mgr_get_ctx(profile_mgr_t *mgr);

uint8_t profile_mgr_get_feature_flags(const profile_mgr_t *mgr);
uint8_t profile_mgr_get_temp_gpio(const profile_mgr_t *mgr);
bool profile_mgr_temperature_enabled(const profile_mgr_t *mgr);
uint8_t profile_mgr_get_temp_endpoint(const profile_mgr_t *mgr);

/** Apply temp GPIO at runtime (NVS should already be updated). Restarts DS18B20 reader. */
esp_err_t profile_mgr_apply_temp_gpio(profile_mgr_t *mgr, uint8_t pin);

/** Apply feature flags at runtime and restart temp reader if needed. */
esp_err_t profile_mgr_apply_feature_flags(profile_mgr_t *mgr, uint8_t flags);

#ifdef __cplusplus
}
#endif
