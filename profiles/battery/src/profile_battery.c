#include "profile_battery.h"
static esp_err_t battery_init(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t battery_start(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t battery_stop(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t battery_on_event(profile_ctx_t *c, const event_t *e) { (void)c; (void)e; return ESP_OK; }
static esp_err_t battery_get_entities(profile_ctx_t *c, zigbee_entity_desc_t *o, size_t *n) { (void)c; (void)o; (void)n; return ESP_OK; }
static esp_err_t battery_set_config(profile_ctx_t *c, const uint8_t *b, size_t l) { (void)c; (void)b; (void)l; return ESP_OK; }
static esp_err_t battery_get_config(profile_ctx_t *c, uint8_t *b, size_t *l) { (void)c; (void)b; (void)l; return ESP_OK; }
static const profile_ops_t s_battery_ops = { .name = "battery", .id = PROFILE_ID_BATTERY, .init = battery_init, .start = battery_start, .stop = battery_stop, .on_event = battery_on_event, .get_entities = battery_get_entities, .set_config = battery_set_config, .get_config = battery_get_config };
const profile_ops_t *profile_battery_get_ops(void) { return &s_battery_ops; }
