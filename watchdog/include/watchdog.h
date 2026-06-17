#pragma once

/**
 * @file watchdog.h
 * @brief Task watchdog manager for CamperNode OS.
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct watchdog watchdog_t;

watchdog_t *watchdog_create(void);
void watchdog_destroy(watchdog_t *wdt);

esp_err_t watchdog_start(watchdog_t *wdt);
esp_err_t watchdog_feed(watchdog_t *wdt);
esp_err_t watchdog_subscribe_task(watchdog_t *wdt);

#ifdef __cplusplus
}
#endif
