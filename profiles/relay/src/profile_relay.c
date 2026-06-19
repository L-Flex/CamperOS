/**
 * @file profile_relay.c
 * @brief Relay profile — HA controls outputs; no local input→relay coupling.
 */

#include "profile_relay.h"
#include "logger.h"

#include <string.h>

#define TAG              "PROFILE_RELAY"
#define RELAY_ZB_ENDPOINT  1

typedef struct {
    profile_ctx_t ctx;
    bool          initialized;
} relay_state_t;

static relay_state_t s_relay;

static esp_err_t relay_init(profile_ctx_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_relay, 0, sizeof(s_relay));
    s_relay.ctx = *ctx;
    s_relay.initialized = true;
    logger_info(ctx->logger, TAG, "init (outputs via HA / output_state)");
    return ESP_OK;
}

static esp_err_t relay_start(profile_ctx_t *ctx)
{
    if (!s_relay.initialized) {
        relay_init(ctx);
    }
    logger_info(ctx->logger, TAG, "started");
    return ESP_OK;
}

static esp_err_t relay_stop(profile_ctx_t *ctx)
{
    (void)ctx;
    return ESP_OK;
}

static esp_err_t relay_on_event(profile_ctx_t *ctx, const event_t *evt)
{
    (void)ctx;
    (void)evt;
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
    (void)ctx;
    (void)blob;
    (void)len;
    return ESP_OK;
}

static esp_err_t relay_get_config(profile_ctx_t *ctx, uint8_t *blob, size_t *len)
{
    (void)ctx;
    if (blob == NULL || len == NULL || *len < 1) {
        return ESP_ERR_INVALID_ARG;
    }
    blob[0] = 0;
    *len = 1;
    return ESP_OK;
}

bool profile_relay_get_state(void)
{
    return false;
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
