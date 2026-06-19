/**
 * @file profile_sensor.c
 * @brief Temperature-only profile — DS18B20 on endpoint 1.
 */

#include "profile_sensor.h"
#include "camper_features.h"
#include "ds18b20_svc.h"
#include "storage.h"
#include "logger.h"

#include <string.h>

#define TAG                 "PROFILE_SENSOR"
#define SENSOR_ZB_ENDPOINT  CAMPER_ZB_TEMP_ENDPOINT_SOLO

typedef struct {
    profile_ctx_t          ctx;
    sensor_profile_config_t config;
    bool                   initialized;
} sensor_state_t;

static sensor_state_t s_sensor;

static esp_err_t sensor_init(profile_ctx_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_sensor, 0, sizeof(s_sensor));
    s_sensor.ctx = *ctx;
    s_sensor.config.sensor_bind = 2;

    if (ctx->storage != NULL) {
        sensor_profile_config_t stored = {0};
        size_t len = sizeof(stored);
        if (storage_get_blob(ctx->storage, CAMPER_NVS_NS_SETTINGS, CAMPER_SENSOR_SETTINGS_KEY,
                             &stored, &len) == ESP_OK && len == sizeof(stored)) {
            s_sensor.config = stored;
        }
    }

    s_sensor.initialized = true;
    logger_info(ctx->logger, TAG, "init sensor_bind=%u", s_sensor.config.sensor_bind);
    return ESP_OK;
}

static esp_err_t sensor_start(profile_ctx_t *ctx)
{
    if (!s_sensor.initialized) {
        sensor_init(ctx);
    }

    uint8_t temp_gpio = 0;
    if (ctx->storage != NULL) {
        storage_get_u8(ctx->storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_TEMP_GPIO, &temp_gpio);
    }

    if (temp_gpio > 0) {
        ds18b20_svc_start(&(ds18b20_svc_deps_t){
            .event_bus = ctx->event_bus,
            .logger = ctx->logger,
        }, temp_gpio, 30);
    }

    logger_info(ctx->logger, TAG, "started (temp GPIO %u)", temp_gpio);
    return ESP_OK;
}

static esp_err_t sensor_stop(profile_ctx_t *ctx)
{
    (void)ctx;
    ds18b20_svc_stop();
    return ESP_OK;
}

static esp_err_t sensor_get_entities(profile_ctx_t *ctx, zigbee_entity_desc_t *out, size_t *count)
{
    (void)ctx;
    if (out == NULL || count == NULL || *count < 1) {
        return ESP_ERR_INVALID_ARG;
    }

    out[0].endpoint_id = SENSOR_ZB_ENDPOINT;
    out[0].cluster_id = 0x0402;
    out[0].device_type = 0;
    out[0].name = "temperature";
    *count = 1;
    return ESP_OK;
}

static esp_err_t sensor_set_config(profile_ctx_t *ctx, const uint8_t *blob, size_t len)
{
    if (blob == NULL || len != sizeof(sensor_profile_config_t)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(&s_sensor.config, blob, sizeof(s_sensor.config));
    if (ctx != NULL && ctx->storage != NULL) {
        return storage_set_blob(ctx->storage, CAMPER_NVS_NS_SETTINGS, CAMPER_SENSOR_SETTINGS_KEY,
                                &s_sensor.config, sizeof(s_sensor.config));
    }
    return ESP_OK;
}

static esp_err_t sensor_get_config(profile_ctx_t *ctx, uint8_t *blob, size_t *len)
{
    (void)ctx;
    if (blob == NULL || len == NULL || *len < sizeof(sensor_profile_config_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(blob, &s_sensor.config, sizeof(sensor_profile_config_t));
    *len = sizeof(sensor_profile_config_t);
    return ESP_OK;
}

uint8_t profile_sensor_get_endpoint(void)
{
    return SENSOR_ZB_ENDPOINT;
}

static const profile_ops_t s_sensor_ops = {
    .name = "sensor",
    .id = PROFILE_ID_SENSOR,
    .init = sensor_init,
    .start = sensor_start,
    .stop = sensor_stop,
    .on_event = NULL,
    .get_entities = sensor_get_entities,
    .set_config = sensor_set_config,
    .get_config = sensor_get_config,
};

const profile_ops_t *profile_sensor_get_ops(void)
{
    return &s_sensor_ops;
}
