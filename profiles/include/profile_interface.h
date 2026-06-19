#pragma once

/**
 * @file profile_interface.h
 * @brief Profile plugin interface for CamperNode OS.
 */

#include "esp_err.h"
#include "event_types.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct event_bus event_bus_t;
typedef struct storage storage_t;
typedef struct gpio_mgr gpio_mgr_t;
typedef struct logger logger_t;

typedef enum {
    PROFILE_ID_RELAY = 0,
    PROFILE_ID_MAX
} profile_id_t;

typedef struct {
    event_bus_t *event_bus;
    storage_t   *storage;
    gpio_mgr_t  *gpio_mgr;
    logger_t    *logger;
} profile_ctx_t;

typedef struct {
    uint8_t     endpoint_id;
    uint16_t    cluster_id;
    uint8_t     device_type;
    const char *name;
} zigbee_entity_desc_t;

typedef struct {
    const char *name;
    profile_id_t id;
    esp_err_t (*init)(profile_ctx_t *ctx);
    esp_err_t (*start)(profile_ctx_t *ctx);
    esp_err_t (*stop)(profile_ctx_t *ctx);
    esp_err_t (*on_event)(profile_ctx_t *ctx, const event_t *evt);
    esp_err_t (*get_entities)(profile_ctx_t *ctx, zigbee_entity_desc_t *out, size_t *count);
    esp_err_t (*set_config)(profile_ctx_t *ctx, const uint8_t *blob, size_t len);
    esp_err_t (*get_config)(profile_ctx_t *ctx, uint8_t *blob, size_t *len);
} profile_ops_t;

#ifdef __cplusplus
}
#endif
