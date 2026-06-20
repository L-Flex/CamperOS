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
#include "dht22_svc.h"

#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

#define TAG "PROFILE_MGR"

struct profile_mgr {
    profile_ctx_t        ctx;
    const profile_ops_t *active_ops;
    uint8_t              feature_flags;
    uint8_t              sensor_gpio6;
    uint8_t              sensor_gpio7;
    uint8_t              temp_gpio;
    uint8_t              dht_gpio;
};

static bool profile_mgr_sensor_mode_valid(uint8_t mode)
{
    return mode <= CAMPER_SENSOR_DHT22;
}

static esp_err_t profile_mgr_validate_sensor_pins(uint8_t gpio6, uint8_t gpio7)
{
    if (!profile_mgr_sensor_mode_valid(gpio6) || !profile_mgr_sensor_mode_valid(gpio7)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (gpio6 != CAMPER_SENSOR_NONE && gpio6 == gpio7) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static uint8_t profile_mgr_flags_from_pins(uint8_t gpio6, uint8_t gpio7)
{
    uint8_t flags = 0;

    if (gpio6 == CAMPER_SENSOR_DS18B20 || gpio7 == CAMPER_SENSOR_DS18B20) {
        flags |= CAMPER_FEATURE_TEMPERATURE;
    }
    if (gpio6 == CAMPER_SENSOR_DHT22 || gpio7 == CAMPER_SENSOR_DHT22) {
        flags |= CAMPER_FEATURE_DHT22;
    }
    return flags;
}

static void profile_mgr_resolve_active_pins(uint8_t gpio6, uint8_t gpio7,
                                            uint8_t *ds18_pin, uint8_t *dht_pin)
{
    *ds18_pin = 0;
    *dht_pin = 0;

    if (gpio6 == CAMPER_SENSOR_DS18B20) {
        *ds18_pin = CAMPER_BOARD_SENSOR_GPIO_6;
    } else if (gpio6 == CAMPER_SENSOR_DHT22) {
        *dht_pin = CAMPER_BOARD_SENSOR_GPIO_6;
    }

    if (gpio7 == CAMPER_SENSOR_DS18B20) {
        *ds18_pin = CAMPER_BOARD_SENSOR_GPIO_7;
    } else if (gpio7 == CAMPER_SENSOR_DHT22) {
        *dht_pin = CAMPER_BOARD_SENSOR_GPIO_7;
    }
}

static void profile_mgr_flags_to_pins(uint8_t flags, uint8_t *gpio6, uint8_t *gpio7)
{
    *gpio6 = CAMPER_SENSOR_NONE;
    *gpio7 = CAMPER_SENSOR_NONE;

    if ((flags & CAMPER_FEATURE_TEMPERATURE) != 0) {
        *gpio6 = CAMPER_SENSOR_DS18B20;
    }
    if ((flags & CAMPER_FEATURE_DHT22) != 0) {
        if (*gpio6 == CAMPER_SENSOR_NONE) {
            *gpio6 = CAMPER_SENSOR_DHT22;
        } else {
            *gpio7 = CAMPER_SENSOR_DHT22;
        }
    }
}

static void profile_mgr_migrate_legacy(profile_mgr_t *mgr)
{
    uint8_t legacy_type = CAMPER_SENSOR_NONE;

    if (mgr->ctx.storage != NULL) {
        storage_get_u8(mgr->ctx.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_SENSOR_TYPE,
                       &legacy_type);
    }

    if (mgr->sensor_gpio6 == CAMPER_SENSOR_NONE && mgr->sensor_gpio7 == CAMPER_SENSOR_NONE) {
        if (legacy_type != CAMPER_SENSOR_NONE) {
            mgr->sensor_gpio6 = legacy_type;
        } else {
            profile_mgr_flags_to_pins(mgr->feature_flags, &mgr->sensor_gpio6, &mgr->sensor_gpio7);
        }
    }
}

static void profile_mgr_reload_settings(profile_mgr_t *mgr)
{
    if (mgr == NULL || mgr->ctx.storage == NULL) {
        return;
    }

    storage_get_u8(mgr->ctx.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_FEATURE_FLAGS,
                   &mgr->feature_flags);
    storage_get_u8(mgr->ctx.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_SENSOR_GPIO6,
                   &mgr->sensor_gpio6);
    storage_get_u8(mgr->ctx.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_SENSOR_GPIO7,
                   &mgr->sensor_gpio7);
    storage_get_u8(mgr->ctx.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_TEMP_GPIO,
                   &mgr->temp_gpio);

    profile_mgr_migrate_legacy(mgr);
}

static esp_err_t profile_mgr_start_sensors(profile_mgr_t *mgr)
{
    profile_mgr_reload_settings(mgr);

    ds18b20_svc_stop();
    dht22_svc_stop();

    esp_err_t err = profile_mgr_validate_sensor_pins(mgr->sensor_gpio6, mgr->sensor_gpio7);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "invalid sensor config gpio6=%u gpio7=%u",
                 mgr->sensor_gpio6, mgr->sensor_gpio7);
        mgr->sensor_gpio6 = CAMPER_SENSOR_NONE;
        mgr->sensor_gpio7 = CAMPER_SENSOR_NONE;
    }

    uint8_t ds18_pin = 0;
    uint8_t dht_pin = 0;
    profile_mgr_resolve_active_pins(mgr->sensor_gpio6, mgr->sensor_gpio7, &ds18_pin, &dht_pin);

    mgr->temp_gpio = ds18_pin;
    mgr->dht_gpio = dht_pin;
    mgr->feature_flags = profile_mgr_flags_from_pins(mgr->sensor_gpio6, mgr->sensor_gpio7);

    profile_ctx_t *ctx = &mgr->ctx;

    if (ds18_pin != 0) {
        ESP_LOGI(TAG, "starting DS18B20 on GPIO %u", (unsigned)ds18_pin);
        err = ds18b20_svc_start(&(ds18b20_svc_deps_t){
            .event_bus = ctx->event_bus,
            .logger = ctx->logger,
        }, ds18_pin, 30);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (dht_pin != 0) {
        ESP_LOGI(TAG, "starting DHT22 on GPIO %u", (unsigned)dht_pin);
        err = dht22_svc_start(&(dht22_svc_deps_t){
            .event_bus = ctx->event_bus,
            .logger = ctx->logger,
        }, dht_pin, 30);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (ds18_pin == 0 && dht_pin == 0) {
        ESP_LOGI(TAG, "sensor pins 6/7 off");
    }

    return ESP_OK;
}

static esp_err_t profile_mgr_store_sensor_pins(profile_mgr_t *mgr)
{
    if (mgr == NULL || mgr->ctx.storage == NULL) {
        return ESP_OK;
    }

    esp_err_t err = storage_set_u8(mgr->ctx.storage, CAMPER_NVS_NS_SETTINGS,
                                   CAMPER_KEY_SENSOR_GPIO6, mgr->sensor_gpio6);
    if (err != ESP_OK) {
        return err;
    }
    err = storage_set_u8(mgr->ctx.storage, CAMPER_NVS_NS_SETTINGS,
                         CAMPER_KEY_SENSOR_GPIO7, mgr->sensor_gpio7);
    if (err != ESP_OK) {
        return err;
    }
    err = storage_set_u8(mgr->ctx.storage, CAMPER_NVS_NS_SETTINGS,
                         CAMPER_KEY_FEATURE_FLAGS, mgr->feature_flags);
    if (err != ESP_OK) {
        return err;
    }
    return storage_set_u8(mgr->ctx.storage, CAMPER_NVS_NS_SETTINGS,
                          CAMPER_KEY_SENSOR_TYPE, mgr->sensor_gpio6);
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

    if (mgr->ctx.storage != NULL) {
        storage_set_active_profile(mgr->ctx.storage, PROFILE_ID_RELAY);
        profile_mgr_reload_settings(mgr);
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

    return profile_mgr_start_sensors(mgr);
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

uint8_t profile_mgr_get_sensor_gpio6(const profile_mgr_t *mgr)
{
    if (mgr == NULL) {
        return CAMPER_SENSOR_NONE;
    }
    return mgr->sensor_gpio6;
}

uint8_t profile_mgr_get_sensor_gpio7(const profile_mgr_t *mgr)
{
    if (mgr == NULL) {
        return CAMPER_SENSOR_NONE;
    }
    return mgr->sensor_gpio7;
}

uint8_t profile_mgr_get_temp_gpio(const profile_mgr_t *mgr)
{
    if (mgr == NULL) {
        return 0;
    }
    return mgr->temp_gpio;
}

uint8_t profile_mgr_get_dht_gpio(const profile_mgr_t *mgr)
{
    if (mgr == NULL) {
        return 0;
    }
    return mgr->dht_gpio;
}

bool profile_mgr_temperature_enabled(const profile_mgr_t *mgr)
{
    if (mgr == NULL) {
        return false;
    }
    return mgr->temp_gpio != 0;
}

bool profile_mgr_dht_enabled(const profile_mgr_t *mgr)
{
    if (mgr == NULL) {
        return false;
    }
    return mgr->dht_gpio != 0;
}

uint8_t profile_mgr_get_dht_endpoint(const profile_mgr_t *mgr)
{
    (void)mgr;
    return CAMPER_ZB_DHT_ENDPOINT;
}

uint8_t profile_mgr_get_temp_endpoint(const profile_mgr_t *mgr)
{
    (void)mgr;
    return CAMPER_ZB_TEMP_ENDPOINT_COMBO;
}

esp_err_t profile_mgr_apply_sensor_pins(profile_mgr_t *mgr, uint8_t gpio6, uint8_t gpio7)
{
    if (mgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = profile_mgr_validate_sensor_pins(gpio6, gpio7);
    if (err != ESP_OK) {
        return err;
    }

    mgr->sensor_gpio6 = gpio6;
    mgr->sensor_gpio7 = gpio7;
    err = profile_mgr_store_sensor_pins(mgr);
    if (err != ESP_OK) {
        return err;
    }

    return profile_mgr_start_sensors(mgr);
}

esp_err_t profile_mgr_apply_sensor_gpio6(profile_mgr_t *mgr, uint8_t mode)
{
    if (mgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return profile_mgr_apply_sensor_pins(mgr, mode, mgr->sensor_gpio7);
}

esp_err_t profile_mgr_apply_sensor_gpio7(profile_mgr_t *mgr, uint8_t mode)
{
    if (mgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return profile_mgr_apply_sensor_pins(mgr, mgr->sensor_gpio6, mode);
}

esp_err_t profile_mgr_apply_temp_gpio(profile_mgr_t *mgr, uint8_t pin)
{
    (void)pin;
    if (mgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return profile_mgr_start_sensors(mgr);
}

esp_err_t profile_mgr_apply_sensor_type(profile_mgr_t *mgr, uint8_t sensor_type)
{
    if (mgr == NULL || sensor_type > CAMPER_SENSOR_DHT22) {
        return ESP_ERR_INVALID_ARG;
    }

    return profile_mgr_apply_sensor_gpio6(mgr, sensor_type);
}

esp_err_t profile_mgr_apply_feature_flags(profile_mgr_t *mgr, uint8_t flags)
{
    if (mgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t gpio6 = CAMPER_SENSOR_NONE;
    uint8_t gpio7 = CAMPER_SENSOR_NONE;
    profile_mgr_flags_to_pins(flags, &gpio6, &gpio7);
    return profile_mgr_apply_sensor_pins(mgr, gpio6, gpio7);
}
