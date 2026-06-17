#include "profile_climate.h"
static esp_err_t climate_init(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t climate_start(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t climate_stop(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t climate_on_event(profile_ctx_t *c, const event_t *e) { (void)c; (void)e; return ESP_OK; }
static esp_err_t climate_get_entities(profile_ctx_t *c, zigbee_entity_desc_t *o, size_t *n) { (void)c; (void)o; (void)n; return ESP_OK; }
static esp_err_t climate_set_config(profile_ctx_t *c, const uint8_t *b, size_t l) { (void)c; (void)b; (void)l; return ESP_OK; }
static esp_err_t climate_get_config(profile_ctx_t *c, uint8_t *b, size_t *l) { (void)c; (void)b; (void)l; return ESP_OK; }
static const profile_ops_t s_climate_ops = { .name = "climate", .id = PROFILE_ID_CLIMATE, .init = climate_init, .start = climate_start, .stop = climate_stop, .on_event = climate_on_event, .get_entities = climate_get_entities, .set_config = climate_set_config, .get_config = climate_get_config };
const profile_ops_t *profile_climate_get_ops(void) { return &s_climate_ops; }
