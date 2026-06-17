/**
 * @file profile_relay.c
 * @brief Relay profile — button toggles relay via event bus (local autonomy).
 */

#include "profile_relay.h"
#include "storage.h"
#include "event_bus.h"
#include "logger.h"

#include <string.h>

#define TAG              "PROFILE_RELAY"
#define SOURCE_ID_RELAY    0x0300U
#define RELAY_ZB_ENDPOINT  1

typedef struct {
    profile_ctx_t        ctx;
    relay_profile_config_t config;
    bool                 relay_on;
    bool                 initialized;
} relay_state_t;

static relay_state_t s_relay;

static esp_err_t relay_save_state(const profile_ctx_t *ctx)
{
    if (ctx == NULL || ctx->storage == NULL) {
        return ESP_OK;
    }
    return storage_set_u8(ctx->storage, CAMPER_NVS_NS_STATE, CAMPER_KEY_RELAY_STATE,
                          s_relay.relay_on ? 1U : 0U);
}

static esp_err_t relay_publish_output(profile_ctx_t *ctx, bool on)
{
    event_t evt = {
        .type = on ? EVT_RELAY_ON : EVT_RELAY_OFF,
        .source_id = SOURCE_ID_RELAY,
        .gpio_id = s_relay.config.relay_bind,
        .data.bool_val = on,
    };
    return event_bus_publish(ctx->event_bus, &evt);
}

static esp_err_t relay_set_state(profile_ctx_t *ctx, bool on, bool persist)
{
    if (s_relay.relay_on == on) {
        return ESP_OK;
    }

    s_relay.relay_on = on;
    if (persist) {
        relay_save_state(ctx);
    }
  return relay_publish_output(ctx, on);
}

static esp_err_t relay_on_event(profile_ctx_t *ctx, const event_t *evt)
{
    if (ctx == NULL || evt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (evt->type) {
    case EVT_BUTTON_PRESSED:
        if (evt->gpio_id != s_relay.config.button_bind) {
            return ESP_OK;
        }
        return relay_set_state(ctx, !s_relay.relay_on, true);

    case EVT_ZIGBEE_CMD:
        switch (evt->data.int_val) {
        case 1:
            return relay_set_state(ctx, true, true);
        case 0:
            return relay_set_state(ctx, false, true);
        default:
            return relay_set_state(ctx, !s_relay.relay_on, true);
        }

    default:
        break;
    }

    return ESP_OK;
}

static void relay_event_bridge(const event_t *evt, void *arg)
{
    relay_on_event((profile_ctx_t *)arg, evt);
}

static esp_err_t relay_init(profile_ctx_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_relay, 0, sizeof(s_relay));
    s_relay.ctx = *ctx;
    s_relay.config.button_bind = 0;
    s_relay.config.relay_bind = 1;

    if (ctx->storage != NULL) {
        relay_profile_config_t stored = {0};
        size_t len = sizeof(stored);
        if (storage_get_blob(ctx->storage, CAMPER_NVS_NS_SETTINGS, CAMPER_RELAY_SETTINGS_KEY,
                             &stored, &len) == ESP_OK && len == sizeof(stored)) {
            s_relay.config = stored;
        }

        uint8_t relay_state = 0;
        if (storage_get_u8(ctx->storage, CAMPER_NVS_NS_STATE, CAMPER_KEY_RELAY_STATE,
                           &relay_state) == ESP_OK) {
            s_relay.relay_on = (relay_state != 0);
        }
    }

    s_relay.initialized = true;
    logger_info(ctx->logger, TAG, "init button_bind=%u relay_bind=%u",
                s_relay.config.button_bind, s_relay.config.relay_bind);
    return ESP_OK;
}

static esp_err_t relay_start(profile_ctx_t *ctx)
{
    if (!s_relay.initialized) {
        relay_init(ctx);
    }

    event_bus_subscribe(ctx->event_bus, EVT_BUTTON_PRESSED, relay_event_bridge, ctx);
    event_bus_subscribe(ctx->event_bus, EVT_ZIGBEE_CMD, relay_event_bridge, ctx);

    relay_publish_output(ctx, s_relay.relay_on);
    logger_info(ctx->logger, TAG, "started (relay %s)", s_relay.relay_on ? "ON" : "OFF");
    return ESP_OK;
}

static esp_err_t relay_stop(profile_ctx_t *ctx)
{
    event_bus_unsubscribe(ctx->event_bus, EVT_BUTTON_PRESSED, relay_event_bridge);
    event_bus_unsubscribe(ctx->event_bus, EVT_ZIGBEE_CMD, relay_event_bridge);
    (void)ctx;
    return ESP_OK;
}

static esp_err_t relay_get_entities(profile_ctx_t *ctx, zigbee_entity_desc_t *out, size_t *count)
{
    (void)ctx;
    if (out == NULL || count == NULL || *count < 1) {
        return ESP_ERR_INVALID_ARG;
    }

    out[0].endpoint_id = RELAY_ZB_ENDPOINT;
    out[0].cluster_id = 0x0006; /* On/Off */
    out[0].device_type = 0;
    out[0].name = "relay";
    *count = 1;
    return ESP_OK;
}

static esp_err_t relay_set_config(profile_ctx_t *ctx, const uint8_t *blob, size_t len)
{
    if (blob == NULL || len != sizeof(relay_profile_config_t)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(&s_relay.config, blob, sizeof(s_relay.config));
    if (ctx != NULL && ctx->storage != NULL) {
        return storage_set_blob(ctx->storage, CAMPER_NVS_NS_SETTINGS, CAMPER_RELAY_SETTINGS_KEY,
                                &s_relay.config, sizeof(s_relay.config));
    }
    return ESP_OK;
}

static esp_err_t relay_get_config(profile_ctx_t *ctx, uint8_t *blob, size_t *len)
{
    (void)ctx;
    if (blob == NULL || len == NULL || *len < sizeof(relay_profile_config_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(blob, &s_relay.config, sizeof(s_relay.config));
    *len = sizeof(s_relay.config);
    return ESP_OK;
}

bool profile_relay_get_state(void)
{
    return s_relay.relay_on;
}

uint8_t profile_relay_get_endpoint(void)
{
    return RELAY_ZB_ENDPOINT;
}

static const profile_ops_t s_relay_ops = {
    .name = "relay",
    .id = PROFILE_ID_RELAY,
    .init = relay_init,
    .start = relay_start,
    .stop = relay_stop,
    .on_event = relay_on_event,
    .get_entities = relay_get_entities,
    .set_config = relay_set_config,
    .get_config = relay_get_config,
};

const profile_ops_t *profile_relay_get_ops(void)
{
    return &s_relay_ops;
}
