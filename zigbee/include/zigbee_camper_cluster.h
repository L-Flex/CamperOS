#pragma once

/**
 * @file zigbee_camper_cluster.h
 * @brief CamperNode manufacturer cluster 0xFC00 on config endpoint 10.
 */

#include "esp_err.h"
#include "esp_zigbee_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct event_bus event_bus_t;
typedef struct storage storage_t;
typedef struct profile_mgr profile_mgr_t;
typedef struct gpio_mgr gpio_mgr_t;
typedef struct logger logger_t;
typedef struct ota_mgr ota_mgr_t;

typedef struct esp_zb_zcl_custom_cluster_command_message_s esp_zb_zcl_custom_cluster_command_message_t;
typedef struct esp_zb_zcl_set_attr_value_message_s esp_zb_zcl_set_attr_value_message_t;

typedef struct {
    event_bus_t   *event_bus;
    storage_t     *storage;
    profile_mgr_t *profile_mgr;
    gpio_mgr_t    *gpio_mgr;
    logger_t      *logger;
    ota_mgr_t     *ota_mgr;
} zigbee_camper_deps_t;

esp_err_t zigbee_camper_cluster_init(const zigbee_camper_deps_t *deps);
esp_err_t zigbee_camper_cluster_add_endpoint(esp_zb_ep_list_t *ep_list);
esp_err_t zigbee_camper_cluster_register_handlers(void);
void zigbee_camper_cluster_refresh_diagnostics(void);
void zigbee_camper_cluster_start_diagnostics_timer(void);

esp_err_t zigbee_camper_cluster_handle_custom_cmd(const esp_zb_zcl_custom_cluster_command_message_t *msg);
esp_err_t zigbee_camper_cluster_handle_set_attr(const esp_zb_zcl_set_attr_value_message_t *msg);

#ifdef __cplusplus
}
#endif
