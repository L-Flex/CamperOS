/**
 * @file event_bus.c
 * @brief Typed publish/subscribe event bus with async ISR-safe dispatch.
 */

#include "event_bus.h"
#include "camper_config.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

#define DISPATCH_TASK_NAME   "evt_dispatch"
#define DISPATCH_STACK_SIZE  4096
#define DISPATCH_PRIORITY    6
#define DISPATCH_POLL_MS     100

typedef struct {
    event_handler_fn handler;
    void            *ctx;
    bool             active;
} event_subscriber_t;

struct event_bus {
    event_subscriber_t subscribers[EVT_MAX][CAMPER_EVENT_MAX_SUBSCRIBERS];
    SemaphoreHandle_t  lock;
    QueueHandle_t      queue;
    TaskHandle_t       dispatch_task;
    volatile bool      running;
};

static uint32_t bus_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void bus_stamp_if_needed(event_t *evt)
{
    if (evt != NULL && evt->timestamp_ms == 0) {
        evt->timestamp_ms = bus_now_ms();
    }
}

static bool bus_type_valid(event_type_t type)
{
    return type >= 0 && type < EVT_MAX;
}

static void event_dispatch_task(void *arg)
{
    event_bus_t *bus = (event_bus_t *)arg;
    event_t evt;

    while (bus->running) {
        if (xQueueReceive(bus->queue, &evt, pdMS_TO_TICKS(DISPATCH_POLL_MS)) == pdTRUE) {
            event_bus_publish(bus, &evt);
        }
    }

    bus->dispatch_task = NULL;
    vTaskDelete(NULL);
}

event_bus_t *event_bus_create(void)
{
    event_bus_t *bus = calloc(1, sizeof(event_bus_t));
    if (bus == NULL) {
        return NULL;
    }

    bus->lock = xSemaphoreCreateMutex();
    if (bus->lock == NULL) {
        free(bus);
        return NULL;
    }

    bus->queue = xQueueCreate(CAMPER_EVENT_QUEUE_DEPTH, sizeof(event_t));
    if (bus->queue == NULL) {
        vSemaphoreDelete(bus->lock);
        free(bus);
        return NULL;
    }

    bus->running = true;

    BaseType_t created = xTaskCreate(
        event_dispatch_task,
        DISPATCH_TASK_NAME,
        DISPATCH_STACK_SIZE,
        bus,
        DISPATCH_PRIORITY,
        &bus->dispatch_task);

    if (created != pdPASS) {
        bus->running = false;
        vQueueDelete(bus->queue);
        vSemaphoreDelete(bus->lock);
        free(bus);
        return NULL;
    }

    return bus;
}

void event_bus_destroy(event_bus_t *bus)
{
    if (bus == NULL) {
        return;
    }

    bus->running = false;

    if (bus->dispatch_task != NULL) {
        for (int i = 0; i < 20 && bus->dispatch_task != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(DISPATCH_POLL_MS / 2));
        }
    }

    if (bus->queue != NULL) {
        vQueueDelete(bus->queue);
    }
    if (bus->lock != NULL) {
        vSemaphoreDelete(bus->lock);
    }
    free(bus);
}

esp_err_t event_bus_subscribe(event_bus_t *bus, event_type_t type,
                              event_handler_fn handler, void *ctx)
{
    if (bus == NULL || handler == NULL || !bus_type_valid(type)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(bus->lock, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    esp_err_t result = ESP_ERR_NO_MEM;

    for (int i = 0; i < CAMPER_EVENT_MAX_SUBSCRIBERS; i++) {
        event_subscriber_t *slot = &bus->subscribers[type][i];

        if (slot->active && slot->handler == handler) {
            slot->ctx = ctx;
            result = ESP_OK;
            break;
        }

        if (!slot->active) {
            slot->handler = handler;
            slot->ctx = ctx;
            slot->active = true;
            result = ESP_OK;
            break;
        }
    }

    xSemaphoreGive(bus->lock);
    return result;
}

esp_err_t event_bus_unsubscribe(event_bus_t *bus, event_type_t type,
                                event_handler_fn handler)
{
    if (bus == NULL || handler == NULL || !bus_type_valid(type)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(bus->lock, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    esp_err_t result = ESP_ERR_NOT_FOUND;

    for (int i = 0; i < CAMPER_EVENT_MAX_SUBSCRIBERS; i++) {
        event_subscriber_t *slot = &bus->subscribers[type][i];

        if (slot->active && slot->handler == handler) {
            slot->active = false;
            slot->handler = NULL;
            slot->ctx = NULL;
            result = ESP_OK;
            break;
        }
    }

    xSemaphoreGive(bus->lock);
    return result;
}

esp_err_t event_bus_publish(event_bus_t *bus, const event_t *evt)
{
    if (bus == NULL || evt == NULL || !bus_type_valid(evt->type)) {
        return ESP_ERR_INVALID_ARG;
    }

    event_t local_evt = *evt;
    bus_stamp_if_needed(&local_evt);

    event_subscriber_t snapshot[CAMPER_EVENT_MAX_SUBSCRIBERS];
    int count = 0;

    if (xSemaphoreTake(bus->lock, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    for (int i = 0; i < CAMPER_EVENT_MAX_SUBSCRIBERS; i++) {
        if (bus->subscribers[local_evt.type][i].active) {
            snapshot[count++] = bus->subscribers[local_evt.type][i];
        }
    }

    xSemaphoreGive(bus->lock);

    for (int i = 0; i < count; i++) {
        snapshot[i].handler(&local_evt, snapshot[i].ctx);
    }

    return ESP_OK;
}

esp_err_t event_bus_post(event_bus_t *bus, const event_t *evt)
{
    if (bus == NULL || evt == NULL || !bus_type_valid(evt->type)) {
        return ESP_ERR_INVALID_ARG;
    }

    event_t local_evt = *evt;
    bus_stamp_if_needed(&local_evt);

    if (xPortInIsrContext()) {
        BaseType_t woken = pdFALSE;
        if (xQueueSendFromISR(bus->queue, &local_evt, &woken) != pdTRUE) {
            return ESP_ERR_NO_MEM;
        }
        portYIELD_FROM_ISR(woken);
        return ESP_OK;
    }

    if (xQueueSend(bus->queue, &local_evt, 0) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
