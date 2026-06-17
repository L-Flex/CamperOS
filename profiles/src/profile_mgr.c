/**
 * @file profile_mgr.c
 * @brief Active profile loader, registry, and event forwarding.
 */

#include "profile_mgr.h"
#include "storage.h"
#include "event_bus.h"
#include "event_types.h"
#include "nvs.h"
#include "profile_relay.h"
#include "profile_pump.h"
#include "profile_light.h"
#include "profile_tank.h"
#include "profile_climate.h"
#include "profile_fan.h"
#include "profile_battery.h"
#include "profile_sensor.h"
#include "profile_custom.h"

#include <stdlib.h>
#include <string.h>

struct profile_mgr {
    profile_ctx_t        ctx;
    profile_id_t         active_id;
    const profile_ops_t *active_ops;
};

static const profile_ops_t *profile_registry[PROFILE_ID_MAX];

static void profile_registry_init(void)
{
    if (profile_registry[PROFILE_ID_RELAY] != NULL) {
        return;
    }

    profile_registry[PROFILE_ID_RELAY] = profile_relay_get_ops();
    profile_registry[PROFILE_ID_PUMP] = profile_pump_get_ops();
    profile_registry[PROFILE_ID_LIGHT] = profile_light_get_ops();
    profile_registry[PROFILE_ID_TANK] = profile_tank_get_ops();
    profile_registry[PROFILE_ID_CLIMATE] = profile_climate_get_ops();
    profile_registry[PROFILE_ID_FAN] = profile_fan_get_ops();
    profile_registry[PROFILE_ID_BATTERY] = profile_battery_get_ops();
    profile_registry[PROFILE_ID_SENSOR] = profile_sensor_get_ops();
    profile_registry[PROFILE_ID_CUSTOM] = profile_custom_get_ops();
}

static const profile_ops_t *profile_lookup(profile_id_t id)
{
    if (id >= PROFILE_ID_MAX) {
        return NULL;
    }
    return profile_registry[id];
}

profile_mgr_t *profile_mgr_create(const profile_ctx_t *base_ctx)
{
    if (base_ctx == NULL) {
        return NULL;
    }

    profile_registry_init();

    profile_mgr_t *mgr = calloc(1, sizeof(profile_mgr_t));
    if (mgr == NULL) {
        return NULL;
    }

    mgr->ctx = *base_ctx;
    mgr->active_id = PROFILE_ID_RELAY;
    mgr->active_ops = profile_lookup(PROFILE_ID_RELAY);
    return mgr;
}

void profile_mgr_destroy(profile_mgr_t *mgr)
{
    if (mgr == NULL) {
        return;
    }
    profile_mgr_stop(mgr);
    free(mgr);
}

esp_err_t profile_mgr_init(profile_mgr_t *mgr)
{
    if (mgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t profile_id = PROFILE_ID_RELAY;
    if (mgr->ctx.storage != NULL) {
        esp_err_t err = storage_get_active_profile(mgr->ctx.storage, &profile_id);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            return err;
        }
    }

    if (profile_id >= PROFILE_ID_MAX) {
        profile_id = PROFILE_ID_RELAY;
    }

    const profile_ops_t *ops = profile_lookup((profile_id_t)profile_id);
    if (ops == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    mgr->active_id = (profile_id_t)profile_id;
    mgr->active_ops = ops;

    if (ops->init != NULL) {
        return ops->init(&mgr->ctx);
    }
    return ESP_OK;
}

esp_err_t profile_mgr_start(profile_mgr_t *mgr)
{
    if (mgr == NULL || mgr->active_ops == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (mgr->active_ops->start != NULL) {
        return mgr->active_ops->start(&mgr->ctx);
    }
    return ESP_OK;
}

esp_err_t profile_mgr_stop(profile_mgr_t *mgr)
{
    if (mgr == NULL || mgr->active_ops == NULL) {
        return ESP_OK;
    }

    if (mgr->active_ops->stop != NULL) {
        return mgr->active_ops->stop(&mgr->ctx);
    }
    return ESP_OK;
}

profile_id_t profile_mgr_get_active_id(const profile_mgr_t *mgr)
{
    if (mgr == NULL) {
        return PROFILE_ID_RELAY;
    }
    return mgr->active_id;
}

const char *profile_mgr_get_active_name(const profile_mgr_t *mgr)
{
    if (mgr == NULL || mgr->active_ops == NULL) {
        return "unknown";
    }
    return mgr->active_ops->name;
}

esp_err_t profile_mgr_set_profile(profile_mgr_t *mgr, profile_id_t id)
{
    if (mgr == NULL || id >= PROFILE_ID_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    const profile_ops_t *ops = profile_lookup(id);
    if (ops == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    if (mgr->ctx.storage != NULL) {
        esp_err_t err = storage_set_active_profile(mgr->ctx.storage, (uint8_t)id);
        if (err != ESP_OK) {
            return err;
        }
    }

    mgr->active_id = id;
    mgr->active_ops = ops;

    if (mgr->ctx.event_bus != NULL) {
        event_t evt = {
            .type = EVT_PROFILE_CHANGED,
            .source_id = 0x0301,
            .gpio_id = EVENT_GPIO_NONE,
            .data.int_val = (int32_t)id,
        };
        event_bus_publish(mgr->ctx.event_bus, &evt);
    }

    return ESP_OK;
}

esp_err_t profile_mgr_on_event(profile_mgr_t *mgr, const event_t *evt)
{
    if (mgr == NULL || evt == NULL || mgr->active_ops == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mgr->active_ops->on_event != NULL) {
        return mgr->active_ops->on_event(&mgr->ctx, evt);
    }
    return ESP_OK;
}

esp_err_t profile_mgr_get_entities(profile_mgr_t *mgr, zigbee_entity_desc_t *out, size_t *count)
{
    if (mgr == NULL || mgr->active_ops == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (mgr->active_ops->get_entities != NULL) {
        return mgr->active_ops->get_entities(&mgr->ctx, out, count);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

profile_ctx_t *profile_mgr_get_ctx(profile_mgr_t *mgr)
{
    if (mgr == NULL) {
        return NULL;
    }
    return &mgr->ctx;
}
