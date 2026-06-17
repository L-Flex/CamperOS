#pragma once

/**
 * @file camper_core.h
 * @brief CamperNode OS core — boot orchestration and system state machine.
 */

#include "esp_err.h"
#include "event_bus.h"
#include "event_types.h"
#include "storage.h"
#include "gpio_mgr.h"
#include "logger.h"
#include "watchdog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations — full headers wired in later phases */
typedef struct profile_mgr profile_mgr_t;
typedef struct zigbee_mgr zigbee_mgr_t;
typedef struct ota_mgr ota_mgr_t;

typedef enum {
    CAMPER_STATE_BOOTING = 0,
    CAMPER_STATE_RUNNING,
    CAMPER_STATE_CONFIG_PENDING,
    CAMPER_STATE_OTA_IN_PROGRESS,
    CAMPER_STATE_SAFE_MODE,
    CAMPER_STATE_FACTORY_RESET,
} camper_state_t;

typedef struct {
    event_bus_t   *event_bus;
    storage_t     *storage;
    profile_mgr_t *profile_mgr;
    gpio_mgr_t    *gpio_mgr;
    zigbee_mgr_t  *zigbee_mgr;
    ota_mgr_t     *ota_mgr;
    logger_t      *logger;
    watchdog_t    *watchdog;
} camper_core_deps_t;

typedef struct camper_core camper_core_t;

camper_core_t *camper_core_create(const camper_core_deps_t *deps);
void camper_core_destroy(camper_core_t *core);

esp_err_t camper_core_init(camper_core_t *core);
esp_err_t camper_core_start(camper_core_t *core);

camper_state_t camper_core_get_state(const camper_core_t *core);
esp_err_t camper_core_request_reboot(camper_core_t *core, reboot_reason_t reason);
esp_err_t camper_core_request_factory_reset(camper_core_t *core);

/**
 * @brief Supervisor loop — feeds watchdog until reboot. Does not return.
 */
void camper_core_run(camper_core_t *core);

#ifdef __cplusplus
}
#endif
