/**
 * @file zigbee_mgr.c
 * @brief Zigbee End Device manager facade.
 */

#include "zigbee_mgr.h"
#include "zigbee_device.h"
#include "profile_relay.h"
#include "profile_pump.h"

#include <stdlib.h>

struct zigbee_mgr {
    zigbee_mgr_deps_t deps;
    bool              started;
};

zigbee_mgr_t *zigbee_mgr_create(const zigbee_mgr_deps_t *deps)
{
    if (deps == NULL) {
        return NULL;
    }

    zigbee_mgr_t *mgr = calloc(1, sizeof(zigbee_mgr_t));
    if (mgr != NULL) {
        mgr->deps = *deps;
    }
    return mgr;
}

void zigbee_mgr_destroy(zigbee_mgr_t *mgr)
{
    free(mgr);
}

esp_err_t zigbee_mgr_init(zigbee_mgr_t *mgr)
{
    (void)mgr;
    return ESP_OK;
}

esp_err_t zigbee_mgr_start(zigbee_mgr_t *mgr)
{
    if (mgr == NULL || mgr->started) {
        return ESP_ERR_INVALID_STATE;
    }

    zigbee_device_deps_t deps = {
        .event_bus = mgr->deps.event_bus,
        .storage = mgr->deps.storage,
        .profile_mgr = mgr->deps.profile_mgr,
        .gpio_mgr = mgr->deps.gpio_mgr,
        .logger = mgr->deps.logger,
        .ota_mgr = mgr->deps.ota_mgr,
    };

    esp_err_t err = zigbee_device_start(&deps);
    if (err == ESP_OK) {
        mgr->started = true;
    }
    return err;
}

esp_err_t zigbee_mgr_stop(zigbee_mgr_t *mgr)
{
    if (mgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    mgr->started = false;
    return ESP_OK;
}

bool zigbee_mgr_is_joined(const zigbee_mgr_t *mgr)
{
    (void)mgr;
    return zigbee_device_is_joined();
}

int8_t zigbee_mgr_get_rssi(const zigbee_mgr_t *mgr)
{
    (void)mgr;
    return 0;
}

void zigbee_mgr_report_relay_state(bool on)
{
    zigbee_device_report_on_off(profile_relay_get_endpoint(), on);
}

void zigbee_mgr_report_pump_state(bool on)
{
    zigbee_device_report_on_off(profile_pump_get_endpoint(), on);
}
