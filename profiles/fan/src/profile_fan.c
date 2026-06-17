#include "profile_fan.h"
static esp_err_t fan_init(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t fan_start(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t fan_stop(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t fan_on_event(profile_ctx_t *c, const event_t *e) { (void)c; (void)e; return ESP_OK; }
static esp_err_t fan_get_entities(profile_ctx_t *c, zigbee_entity_desc_t *o, size_t *n) { (void)c; (void)o; (void)n; return ESP_OK; }
static esp_err_t fan_set_config(profile_ctx_t *c, const uint8_t *b, size_t l) { (void)c; (void)b; (void)l; return ESP_OK; }
static esp_err_t fan_get_config(profile_ctx_t *c, uint8_t *b, size_t *l) { (void)c; (void)b; (void)l; return ESP_OK; }
static const profile_ops_t s_fan_ops = { .name = "fan", .id = PROFILE_ID_FAN, .init = fan_init, .start = fan_start, .stop = fan_stop, .on_event = fan_on_event, .get_entities = fan_get_entities, .set_config = fan_set_config, .get_config = fan_get_config };
const profile_ops_t *profile_fan_get_ops(void) { return &s_fan_ops; }
