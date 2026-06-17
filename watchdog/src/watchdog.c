/**
 * @file watchdog.c
 * @brief Task watchdog manager for CamperNode OS.
 */

#include "watchdog.h"
#include "esp_task_wdt.h"
#include <stdlib.h>

struct watchdog {
    bool started;
    bool task_subscribed;
};

watchdog_t *watchdog_create(void)
{
    return calloc(1, sizeof(watchdog_t));
}

void watchdog_destroy(watchdog_t *wdt)
{
    free(wdt);
}

esp_err_t watchdog_start(watchdog_t *wdt)
{
    if (wdt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (wdt->started) {
        return ESP_OK;
    }

    esp_task_wdt_config_t cfg = {
        .timeout_ms = (uint32_t)CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000U,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };

    esp_err_t err = esp_task_wdt_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    wdt->started = true;
    return ESP_OK;
}

esp_err_t watchdog_feed(watchdog_t *wdt)
{
    if (wdt == NULL || !wdt->started) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_task_wdt_reset();
}

esp_err_t watchdog_subscribe_task(watchdog_t *wdt)
{
    if (wdt == NULL || !wdt->started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (wdt->task_subscribed) {
        return ESP_OK;
    }

    esp_err_t err = esp_task_wdt_add(NULL);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        wdt->task_subscribed = true;
        return ESP_OK;
    }

    return err;
}
