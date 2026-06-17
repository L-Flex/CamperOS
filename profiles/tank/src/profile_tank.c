#include "profile_tank.h"
static esp_err_t tank_init(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t tank_start(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t tank_stop(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t tank_on_event(profile_ctx_t *c, const event_t *e) { (void)c; (void)e; return ESP_OK; }
static esp_err_t tank_get_entities(profile_ctx_t *c, zigbee_entity_desc_t *o, size_t *n) { (void)c; (void)o; (void)n; return ESP_OK; }
static esp_err_t tank_set_config(profile_ctx_t *c, const uint8_t *b, size_t l) { (void)c; (void)b; (void)l; return ESP_OK; }
static esp_err_t tank_get_config(profile_ctx_t *c, uint8_t *b, size_t *l) { (void)c; (void)b; (void)l; return ESP_OK; }
static const profile_ops_t s_tank_ops = { .name = "tank", .id = PROFILE_ID_TANK, .init = tank_init, .start = tank_start, .stop = tank_stop, .on_event = tank_on_event, .get_entities = tank_get_entities, .set_config = tank_set_config, .get_config = tank_get_config };
const profile_ops_t *profile_tank_get_ops(void) { return &s_tank_ops; }
