#pragma once

/**
 * @file event_bus.h
 * @brief Typed publish/subscribe event bus for inter-module communication.
 */

#include "esp_err.h"
#include "event_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct event_bus event_bus_t;

typedef void (*event_handler_fn)(const event_t *evt, void *ctx);

/**
 * @brief Create and initialize the event bus.
 */
event_bus_t *event_bus_create(void);

/**
 * @brief Destroy the event bus and release resources.
 */
void event_bus_destroy(event_bus_t *bus);

/**
 * @brief Subscribe to an event type.
 */
esp_err_t event_bus_subscribe(event_bus_t *bus, event_type_t type,
                              event_handler_fn handler, void *ctx);

/**
 * @brief Unsubscribe a handler from an event type.
 */
esp_err_t event_bus_unsubscribe(event_bus_t *bus, event_type_t type,
                                event_handler_fn handler);

/**
 * @brief Publish an event synchronously to all subscribers.
 */
esp_err_t event_bus_publish(event_bus_t *bus, const event_t *evt);

/**
 * @brief Post an event to the async dispatch queue (ISR-safe).
 *
 * The dedicated dispatch task dequeues and calls event_bus_publish().
 */
esp_err_t event_bus_post(event_bus_t *bus, const event_t *evt);

#ifdef __cplusplus
}
#endif
