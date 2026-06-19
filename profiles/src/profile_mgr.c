/**
 * @file profile_mgr.c
 * @brief Relay profile loader and event forwarding.
 */

#include "profile_mgr.h"
#include "storage.h"
#include "event_bus.h"
#include "event_types.h"
#include "profile_relay.h"
#include "camper_features.h"
#include "board_config.h"
#include "ds18b20_svc.h"

#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

#define TAG "PROFILE_MGR"

struct profile_mgr {
    profile_ctx_t        ctx;
    const profile_ops_t *active_ops;
    uint8_t              feature_flags;
    uint8_t              temp_gpio;
};

static void profile_mgr_reload_settings(profile_mgr_t *mgr)
{
    if (mgr == NULL || mgr->ctx.storage == NULL) {
        return;
    }

    storage_get_u8(mgr->ctx.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_FEATURE_FLAGS,
                   &mgr->feature_flags);
    storage_get_u8(mgr->ctx.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_TEMP_GPIO,
                   &mgr->temp_gpio);
}

static esp_err_t profile_mgr_start_temp_reader(profile_mgr_t *mgr)
{
    profile_mgr_reload_settings(mgr);

    if ((mgr->feature_flags & CAMPER_FEATURE_TEMPERATURE) == 0) {
        ESP_LOGI(TAG, "temperature off (flags=0x%02x)", mgr->feature_flags);
        ds18b20_svc_stop();
        return ESP_OK;
    }

    mgr->temp_gpio = CAMPER_BOARD_TEMP_GPIO;
    ESP_LOGI(TAG, "starting DS18B20 on GPIO %u", mgr->temp_gpio);
    profile_ctx_t *ctx = &mgr->ctx;
    return ds18b20_svc_start(&(ds18b20_svc_deps_t){
        .event_bus = ctx->event_bus,
        .logger = ctx->logger,
    }, mgr->temp_gpio, 30);
}

profile_mgr_t *profile_mgr_create(const profile_ctx_t *base_ctx)
{
    if (base_ctx == NULL) {
        return NULL;
    }

    profile_mgr_t *mgr = calloc(1, sizeof(profile_mgr_t));
    if (mgr == NULL) {
        return NULL;
    }

    mgr->ctx = *base_ctx;
    mgr->active_ops = profile_relay_get_ops();
    mgr->feature_flags = 0;
    mgr->temp_gpio = 0;
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

    mgr->active_ops = profile_relay_get_ops();

    mgr->feature_flags = 0;
    mgr->temp_gpio = 0;
    if (mgr->ctx.storage != NULL) {
        storage_set_active_profile(mgr->ctx.storage, PROFILE_ID_RELAY);
        storage_get_u8(mgr->ctx.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_FEATURE_FLAGS,
                       &mgr->feature_flags);
        storage_get_u8(mgr->ctx.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_TEMP_GPIO,
                       &mgr->temp_gpio);
    }

    if (mgr->active_ops->init != NULL) {
        return mgr->active_ops->init(&mgr->ctx);
    }
    return ESP_OK;
}

esp_err_t profile_mgr_start(profile_mgr_t *mgr)
{
    if (mgr == NULL || mgr->active_ops == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (mgr->active_ops->start != NULL) {
        esp_err_t err = mgr->active_ops->start(&mgr->ctx);
        if (err != ESP_OK) {
            return err;
        }
    }

    return profile_mgr_start_temp_reader(mgr);
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
    (void)mgr;
    return PROFILE_ID_RELAY;
}

const char *profile_mgr_get_active_name(const profile_mgr_t *mgr)
{
    if (mgr == NULL || mgr->active_ops == NULL) {
        return "relay";
    }
    return mgr->active_ops->name;
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

uint8_t profile_mgr_get_feature_flags(const profile_mgr_t *mgr)
{
    if (mgr == NULL) {
        return 0;
    }
    return mgr->feature_flags;
}

uint8_t profile_mgr_get_temp_gpio(const profile_mgr_t *mgr)
{
    if (mgr == NULL) {
        return 0;
    }
    return mgr->temp_gpio;
}

bool profile_mgr_temperature_enabled(const profile_mgr_t *mgr)
{
    if (mgr == NULL) {
        return false;
    }
    return (mgr->feature_flags & CAMPER_FEATURE_TEMPERATURE) != 0;
}

uint8_t profile_mgr_get_temp_endpoint(const profile_mgr_t *mgr)
{
    (void)mgr;
    return CAMPER_ZB_TEMP_ENDPOINT_COMBO;
}

esp_err_t profile_mgr_apply_temp_gpio(profile_mgr_t *mgr, uint8_t pin)
{
    (void)pin;
    if (mgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    mgr->temp_gpio = CAMPER_BOARD_TEMP_GPIO;
    return profile_mgr_start_temp_reader(mgr);
}

esp_err_t profile_mgr_apply_feature_flags(profile_mgr_t *mgr, uint8_t flags)
{
    if (mgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    mgr->feature_flags = flags;
    return profile_mgr_start_temp_reader(mgr);
}
