#pragma once

/**
 * @file ota_mgr.h
 * @brief OTA firmware update manager with rollback support.
 */

#include "esp_err.h"
#include "event_bus.h"
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ota_mgr ota_mgr_t;

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_APPLYING,
    OTA_STATE_DONE,
    OTA_STATE_FAILED,
} ota_state_t;

ota_mgr_t *ota_mgr_create(event_bus_t *event_bus, storage_t *storage);
void ota_mgr_destroy(ota_mgr_t *mgr);

esp_err_t ota_mgr_init(ota_mgr_t *mgr);
esp_err_t ota_mgr_start_update(ota_mgr_t *mgr, const char *url);
esp_err_t ota_mgr_request_zigbee(ota_mgr_t *mgr);
void ota_mgr_set_state(ota_mgr_t *mgr, ota_state_t state);
ota_state_t ota_mgr_get_state(const ota_mgr_t *mgr);
esp_err_t ota_mgr_mark_valid(ota_mgr_t *mgr);

#ifdef __cplusplus
}
#endif
