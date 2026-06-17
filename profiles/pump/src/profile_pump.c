/**
 * @file profile_pump.c
 * @brief Pump profile — button toggles pump output via event bus (local autonomy).
 */

#include "profile_pump.h"
#include "storage.h"
#include "event_bus.h"
#include "logger.h"

#include <string.h>

#define TAG              "PROFILE_PUMP"
#define SOURCE_ID_PUMP     0x0301U
#define PUMP_ZB_ENDPOINT   1

typedef struct {
    profile_ctx_t        ctx;
    pump_profile_config_t config;
    bool                 pump_on;
    bool                 initialized;
} pump_state_t;

static pump_state_t s_pump;

static esp_err_t pump_save_state(const profile_ctx_t *ctx)
{
    if (ctx == NULL || ctx->storage == NULL) {
        return ESP_OK;
    }
    return storage_set_u8(ctx->storage, CAMPER_NVS_NS_STATE, CAMPER_KEY_PUMP_STATE,
                          s_pump.pump_on ? 1U : 0U);
}

static esp_err_t pump_publish_output(profile_ctx_t *ctx, bool on)
{
    event_t evt = {
        .type = on ? EVT_PUMP_ON : EVT_PUMP_OFF,
        .source_id = SOURCE_ID_PUMP,
        .gpio_id = s_pump.config.pump_bind,
        .data.bool_val = on,
    };
    return event_bus_publish(ctx->event_bus, &evt);
}

static esp_err_t pump_set_state(profile_ctx_t *ctx, bool on, bool persist)
{
    if (s_pump.pump_on == on) {
        return ESP_OK;
    }

    s_pump.pump_on = on;
    if (persist) {
        pump_save_state(ctx);
    }
    return pump_publish_output(ctx, on);
}

static esp_err_t pump_on_event(profile_ctx_t *ctx, const event_t *evt)
{
    if (ctx == NULL || evt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (evt->type) {
    case EVT_BUTTON_PRESSED:
        if (evt->gpio_id != s_pump.config.button_bind) {
            return ESP_OK;
        }
        return pump_set_state(ctx, !s_pump.pump_on, true);

    case EVT_ZIGBEE_CMD:
        switch (evt->data.int_val) {
        case 1:
            return pump_set_state(ctx, true, true);
        case 0:
            return pump_set_state(ctx, false, true);
        default:
            return pump_set_state(ctx, !s_pump.pump_on, true);
        }

    default:
        break;
    }

    return ESP_OK;
}

static void pump_event_bridge(const event_t *evt, void *arg)
{
    pump_on_event((profile_ctx_t *)arg, evt);
}

static esp_err_t pump_init(profile_ctx_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_pump, 0, sizeof(s_pump));
    s_pump.ctx = *ctx;
    s_pump.config.button_bind = 0;
    s_pump.config.pump_bind = 1;

    if (ctx->storage != NULL) {
        pump_profile_config_t stored = {0};
        size_t len = sizeof(stored);
        if (storage_get_blob(ctx->storage, CAMPER_NVS_NS_SETTINGS, CAMPER_PUMP_SETTINGS_KEY,
                             &stored, &len) == ESP_OK && len == sizeof(stored)) {
            s_pump.config = stored;
        }

        uint8_t pump_state = 0;
        if (storage_get_u8(ctx->storage, CAMPER_NVS_NS_STATE, CAMPER_KEY_PUMP_STATE,
                           &pump_state) == ESP_OK) {
            s_pump.pump_on = (pump_state != 0);
        }
    }

    s_pump.initialized = true;
    logger_info(ctx->logger, TAG, "init button_bind=%u pump_bind=%u",
                s_pump.config.button_bind, s_pump.config.pump_bind);
    return ESP_OK;
}

static esp_err_t pump_start(profile_ctx_t *ctx)
{
    if (!s_pump.initialized) {
        pump_init(ctx);
    }

    event_bus_subscribe(ctx->event_bus, EVT_BUTTON_PRESSED, pump_event_bridge, ctx);
    event_bus_subscribe(ctx->event_bus, EVT_ZIGBEE_CMD, pump_event_bridge, ctx);

    pump_publish_output(ctx, s_pump.pump_on);
    logger_info(ctx->logger, TAG, "started (pump %s)", s_pump.pump_on ? "ON" : "OFF");
    return ESP_OK;
}

static esp_err_t pump_stop(profile_ctx_t *ctx)
{
    event_bus_unsubscribe(ctx->event_bus, EVT_BUTTON_PRESSED, pump_event_bridge);
    event_bus_unsubscribe(ctx->event_bus, EVT_ZIGBEE_CMD, pump_event_bridge);
    (void)ctx;
    return ESP_OK;
}

static esp_err_t pump_get_entities(profile_ctx_t *ctx, zigbee_entity_desc_t *out, size_t *count)
{
    (void)ctx;
    if (out == NULL || count == NULL || *count < 1) {
        return ESP_ERR_INVALID_ARG;
    }

    out[0].endpoint_id = PUMP_ZB_ENDPOINT;
    out[0].cluster_id = 0x0006; /* On/Off */
    out[0].device_type = 0x02; /* HA on/off output */
    out[0].name = "pump";
    *count = 1;
    return ESP_OK;
}

static esp_err_t pump_set_config(profile_ctx_t *ctx, const uint8_t *blob, size_t len)
{
    if (blob == NULL || len != sizeof(pump_profile_config_t)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(&s_pump.config, blob, sizeof(s_pump.config));
    if (ctx != NULL && ctx->storage != NULL) {
        return storage_set_blob(ctx->storage, CAMPER_NVS_NS_SETTINGS, CAMPER_PUMP_SETTINGS_KEY,
                                &s_pump.config, sizeof(s_pump.config));
    }
    return ESP_OK;
}

static esp_err_t pump_get_config(profile_ctx_t *ctx, uint8_t *blob, size_t *len)
{
    (void)ctx;
    if (blob == NULL || len == NULL || *len < sizeof(pump_profile_config_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(blob, &s_pump.config, sizeof(s_pump.config));
    *len = sizeof(pump_profile_config_t);
    return ESP_OK;
}

bool profile_pump_get_state(void)
{
    return s_pump.pump_on;
}

uint8_t profile_pump_get_endpoint(void)
{
    return PUMP_ZB_ENDPOINT;
}

static const profile_ops_t s_pump_ops = {
    .name = "pump",
    .id = PROFILE_ID_PUMP,
    .init = pump_init,
    .start = pump_start,
    .stop = pump_stop,
    .on_event = pump_on_event,
    .get_entities = pump_get_entities,
    .set_config = pump_set_config,
    .get_config = pump_get_config,
};

const profile_ops_t *profile_pump_get_ops(void)
{
    return &s_pump_ops;
}
