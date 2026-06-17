#include "profile_custom.h"
static esp_err_t custom_init(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t custom_start(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t custom_stop(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t custom_on_event(profile_ctx_t *c, const event_t *e) { (void)c; (void)e; return ESP_OK; }
static esp_err_t custom_get_entities(profile_ctx_t *c, zigbee_entity_desc_t *o, size_t *n) { (void)c; (void)o; (void)n; return ESP_OK; }
static esp_err_t custom_set_config(profile_ctx_t *c, const uint8_t *b, size_t l) { (void)c; (void)b; (void)l; return ESP_OK; }
static esp_err_t custom_get_config(profile_ctx_t *c, uint8_t *b, size_t *l) { (void)c; (void)b; (void)l; return ESP_OK; }
static const profile_ops_t s_custom_ops = { .name = "custom", .id = PROFILE_ID_CUSTOM, .init = custom_init, .start = custom_start, .stop = custom_stop, .on_event = custom_on_event, .get_entities = custom_get_entities, .set_config = custom_set_config, .get_config = custom_get_config };
const profile_ops_t *profile_custom_get_ops(void) { return &s_custom_ops; }
