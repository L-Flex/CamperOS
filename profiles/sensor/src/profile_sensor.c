#include "profile_sensor.h"
static esp_err_t sensor_init(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t sensor_start(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t sensor_stop(profile_ctx_t *c) { (void)c; return ESP_OK; }
static esp_err_t sensor_on_event(profile_ctx_t *c, const event_t *e) { (void)c; (void)e; return ESP_OK; }
static esp_err_t sensor_get_entities(profile_ctx_t *c, zigbee_entity_desc_t *o, size_t *n) { (void)c; (void)o; (void)n; return ESP_OK; }
static esp_err_t sensor_set_config(profile_ctx_t *c, const uint8_t *b, size_t l) { (void)c; (void)b; (void)l; return ESP_OK; }
static esp_err_t sensor_get_config(profile_ctx_t *c, uint8_t *b, size_t *l) { (void)c; (void)b; (void)l; return ESP_OK; }
static const profile_ops_t s_sensor_ops = { .name = "sensor", .id = PROFILE_ID_SENSOR, .init = sensor_init, .start = sensor_start, .stop = sensor_stop, .on_event = sensor_on_event, .get_entities = sensor_get_entities, .set_config = sensor_set_config, .get_config = sensor_get_config };
const profile_ops_t *profile_sensor_get_ops(void) { return &s_sensor_ops; }
