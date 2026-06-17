#pragma once

/**
 * @file zigbee_platform_cfg.h
 * @brief Helpers for ESP-Zigbee-Lib v1.6+ (macros removed from SDK headers).
 */

#include "esp_zigbee_core.h"
#include "platform/esp_zigbee_platform.h"
#include "nwk/esp_zigbee_nwk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAMPER_ZB_CHANNEL_MASK  ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK

static inline esp_zb_cfg_t camper_zb_zed_config(void)
{
    esp_zb_cfg_t cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
        .install_code_policy = false,
    };
    cfg.nwk_cfg.zed_cfg.ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN;
    cfg.nwk_cfg.zed_cfg.keep_alive = 3000;
    return cfg;
}

static inline esp_zb_platform_config_t camper_zb_platform_config(void)
{
    esp_zb_platform_config_t config = {0};
    config.radio_config.radio_mode = ZB_RADIO_MODE_NATIVE;
    config.host_config.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE;
    return config;
}

#ifdef __cplusplus
}
#endif
