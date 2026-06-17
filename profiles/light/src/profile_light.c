#include "profile_light.h"

static esp_err_t light_init(profile_ctx_t *ctx) { (void)ctx; return ESP_OK; }
static esp_err_t light_start(profile_ctx_t *ctx) { (void)ctx; return ESP_OK; }
static esp_err_t light_stop(profile_ctx_t *ctx) { (void)ctx; return ESP_OK; }
static esp_err_t light_on_event(profile_ctx_t *ctx, const event_t *evt) { (void)ctx; (void)evt; return ESP_OK; }
static esp_err_t light_get_entities(profile_ctx_t *ctx, zigbee_entity_desc_t *out, size_t *count) { (void)ctx; (void)out; (void)count; return ESP_OK; }
static esp_err_t light_set_config(profile_ctx_t *ctx, const uint8_t *blob, size_t len) { (void)ctx; (void)blob; (void)len; return ESP_OK; }
static esp_err_t light_get_config(profile_ctx_t *ctx, uint8_t *blob, size_t *len) { (void)ctx; (void)blob; (void)len; return ESP_OK; }

static const profile_ops_t s_light_ops = {
    .name = "light",
    .id = PROFILE_ID_LIGHT,
    .init = light_init,
    .start = light_start,
    .stop = light_stop,
    .on_event = light_on_event,
    .get_entities = light_get_entities,
    .set_config = light_set_config,
    .get_config = light_get_config,
};

const profile_ops_t *profile_light_get_ops(void)
{
    return &s_light_ops;
}
