#include "profile_pump.h"

static esp_err_t pump_init(profile_ctx_t *ctx) { (void)ctx; return ESP_OK; }
static esp_err_t pump_start(profile_ctx_t *ctx) { (void)ctx; return ESP_OK; }
static esp_err_t pump_stop(profile_ctx_t *ctx) { (void)ctx; return ESP_OK; }
static esp_err_t pump_on_event(profile_ctx_t *ctx, const event_t *evt) { (void)ctx; (void)evt; return ESP_OK; }
static esp_err_t pump_get_entities(profile_ctx_t *ctx, zigbee_entity_desc_t *out, size_t *count) { (void)ctx; (void)out; (void)count; return ESP_OK; }
static esp_err_t pump_set_config(profile_ctx_t *ctx, const uint8_t *blob, size_t len) { (void)ctx; (void)blob; (void)len; return ESP_OK; }
static esp_err_t pump_get_config(profile_ctx_t *ctx, uint8_t *blob, size_t *len) { (void)ctx; (void)blob; (void)len; return ESP_OK; }

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
