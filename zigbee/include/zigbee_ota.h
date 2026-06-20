#pragma once

/**
 * @file zigbee_ota.h
 * @brief Zigbee ZCL OTA Upgrade client (writes to esp_ota dual slots).
 */

#include "esp_zigbee_core.h"
#include "esp_zigbee_cluster.h"
#include "ota_mgr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAMPER_ZB_OTA_ENDPOINT  11U

esp_err_t zigbee_ota_add_endpoint(esp_zb_ep_list_t *ep_list);
esp_err_t zigbee_ota_handle_action(esp_zb_core_action_callback_id_t callback_id, const void *message);
esp_err_t zigbee_ota_request_update(ota_mgr_t *mgr);

#ifdef __cplusplus
}
#endif
