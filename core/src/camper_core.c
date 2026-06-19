/**
 * @file camper_core.c
 * @brief CamperNode OS core — boot orchestration and system state machine.
 */

#include "camper_core.h"
#include "camper_config.h"
#include "event_types.h"
#include "storage.h"
#include "profile_mgr.h"
#include "status_led.h"
#include "zigbee_mgr.h"
#include "ota_mgr.h"

#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>

#define TAG              "CORE"
#define SOURCE_ID_CORE   0x0001U
#define SUPERVISOR_MS    1000U
#define REBOOT_DELAY_MS  250U

struct camper_core {
    camper_core_deps_t deps;
    camper_state_t     state;
    reboot_reason_t    pending_reboot;
    bool               reboot_pending;
};

static uint32_t core_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void core_log_init_result(logger_t *log, const char *module, esp_err_t err)
{
    if (log == NULL) {
        return;
    }
    if (err == ESP_OK) {
        logger_info(log, TAG, "%s ready", module);
    } else if (err == ESP_ERR_NOT_SUPPORTED) {
        logger_info(log, TAG, "%s pending (next phase)", module);
    } else {
        logger_error(log, TAG, "%s failed: %s", module, esp_err_to_name(err));
    }
}

static esp_err_t core_publish(logger_t *log, event_bus_t *bus, event_type_t type,
                              int32_t data_val)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    event_t evt = {
        .type = type,
        .timestamp_ms = core_now_ms(),
        .source_id = SOURCE_ID_CORE,
        .gpio_id = EVENT_GPIO_NONE,
        .data.int_val = data_val,
    };

    esp_err_t err = event_bus_publish(bus, &evt);
    if (err != ESP_OK) {
        logger_error(log, TAG, "event %d publish failed: %s", (int)type, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t core_init_watchdog(camper_core_t *core)
{
    logger_t *log = core->deps.logger;
    watchdog_t *wdt = core->deps.watchdog;

    if (wdt == NULL) {
        logger_error(log, TAG, "watchdog dependency missing");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = watchdog_start(wdt);
    if (err != ESP_OK) {
        return err;
    }

    err = watchdog_subscribe_task(wdt);
    if (err != ESP_OK) {
        return err;
    }

    logger_info(log, TAG, "watchdog armed (%ds)", CONFIG_ESP_TASK_WDT_TIMEOUT_S);
    return ESP_OK;
}

static esp_err_t core_init_storage(camper_core_t *core)
{
    storage_t *storage = core->deps.storage;
    if (storage == NULL) {
        return ESP_OK;
    }

    esp_err_t err = storage_init(storage);
    if (err == ESP_OK) {
        err = storage_migrate(storage);
    }
    core_log_init_result(core->deps.logger, "storage", err);
    return err;
}

static esp_err_t core_init_event_bus(camper_core_t *core)
{
    if (core->deps.event_bus == NULL) {
        return ESP_OK;
    }

    logger_info(core->deps.logger, TAG, "event bus ready");
    return ESP_OK;
}

static esp_err_t core_init_gpio(camper_core_t *core)
{
    gpio_mgr_t *gpio = core->deps.gpio_mgr;
    if (gpio == NULL) {
        return ESP_OK;
    }

    esp_err_t err = gpio_mgr_init(gpio);
    if (err != ESP_OK) {
        core_log_init_result(core->deps.logger, "gpio", err);
        return err;
    }

    err = status_led_init();
    if (err != ESP_OK) {
        logger_info(core->deps.logger, TAG, "status LED init skipped");
    }

    if (core->deps.storage != NULL) {
        err = gpio_mgr_load_from_storage(gpio, core->deps.storage);
        core_log_init_result(core->deps.logger, "gpio load", err);
        if (err != ESP_OK) {
            return err;
        }
    }

    core_log_init_result(core->deps.logger, "gpio", ESP_OK);
    return ESP_OK;
}

static esp_err_t core_init_profile(camper_core_t *core)
{
    profile_mgr_t *profile = core->deps.profile_mgr;
    if (profile == NULL) {
        return ESP_OK;
    }

    esp_err_t err = profile_mgr_init(profile);
    core_log_init_result(core->deps.logger, "profile", err);
    return err;
}

static esp_err_t core_init_zigbee(camper_core_t *core)
{
    zigbee_mgr_t *zigbee = core->deps.zigbee_mgr;
    if (zigbee == NULL) {
        return ESP_OK;
    }

    esp_err_t err = zigbee_mgr_init(zigbee);
    core_log_init_result(core->deps.logger, "zigbee", err);
    return err;
}

static esp_err_t core_init_ota(camper_core_t *core)
{
    ota_mgr_t *ota = core->deps.ota_mgr;
    if (ota == NULL) {
        return ESP_OK;
    }

    esp_err_t err = ota_mgr_init(ota);
    core_log_init_result(core->deps.logger, "ota", err);
    return err;
}

static esp_err_t core_start_modules(camper_core_t *core)
{
    logger_t *log = core->deps.logger;
    esp_err_t err;

    if (core->deps.profile_mgr != NULL) {
        err = profile_mgr_start(core->deps.profile_mgr);
        core_log_init_result(log, "profile start", err);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (core->deps.zigbee_mgr != NULL) {
        err = zigbee_mgr_start(core->deps.zigbee_mgr);
        core_log_init_result(log, "zigbee start", err);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (core->deps.ota_mgr != NULL) {
        err = ota_mgr_mark_valid(core->deps.ota_mgr);
        core_log_init_result(log, "ota mark valid", err);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

camper_core_t *camper_core_create(const camper_core_deps_t *deps)
{
    if (deps == NULL || deps->logger == NULL) {
        return NULL;
    }

    camper_core_t *core = calloc(1, sizeof(camper_core_t));
    if (core == NULL) {
        return NULL;
    }

    core->deps = *deps;
    core->state = CAMPER_STATE_BOOTING;
    core->pending_reboot = REBOOT_REASON_USER;
    core->reboot_pending = false;
    return core;
}

void camper_core_destroy(camper_core_t *core)
{
    free(core);
}

esp_err_t camper_core_init(camper_core_t *core)
{
    if (core == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    logger_t *log = core->deps.logger;
    core->state = CAMPER_STATE_BOOTING;

    logger_info(log, TAG, "%s v%s booting", CAMPER_FIRMWARE_NAME, CAMPER_FIRMWARE_VERSION);

    esp_err_t err = core_init_watchdog(core);
    if (err != ESP_OK) {
        core->state = CAMPER_STATE_SAFE_MODE;
        return err;
    }

    err = core_init_storage(core);
    if (err != ESP_OK) {
        core->state = CAMPER_STATE_SAFE_MODE;
        return err;
    }

    if (core->deps.storage != NULL) {
        uint32_t boot_count = 0;
        storage_increment_boot_count(core->deps.storage, &boot_count);
        logger_info(log, TAG, "boot count %lu", (unsigned long)boot_count);
    }

    core_init_event_bus(core);

    err = core_init_gpio(core);
    if (err != ESP_OK) {
        core->state = CAMPER_STATE_SAFE_MODE;
        return err;
    }

    err = core_init_profile(core);
    if (err != ESP_OK) {
        core->state = CAMPER_STATE_SAFE_MODE;
        return err;
    }

    err = core_init_zigbee(core);
    if (err != ESP_OK) {
        core->state = CAMPER_STATE_SAFE_MODE;
        return err;
    }

    err = core_init_ota(core);
    if (err != ESP_OK) {
        core->state = CAMPER_STATE_SAFE_MODE;
        return err;
    }

    logger_info(log, TAG, "initialization complete");
    return ESP_OK;
}

static void core_remote_system_handler(const event_t *evt, void *ctx)
{
    camper_core_t *core = (camper_core_t *)ctx;
    if (core == NULL || evt == NULL || evt->source_id == SOURCE_ID_CORE) {
        return;
    }

    if (evt->type == EVT_SYSTEM_REBOOT) {
        reboot_reason_t reason = (reboot_reason_t)evt->data.int_val;
        logger_info(core->deps.logger, TAG, "remote reboot (reason=%d)", (int)reason);
        core->pending_reboot = reason;
        core->reboot_pending = true;
        if (reason == REBOOT_REASON_PROFILE_CHANGE || reason == REBOOT_REASON_CONFIG_CHANGE) {
            core->state = CAMPER_STATE_CONFIG_PENDING;
        }
    } else if (evt->type == EVT_FACTORY_RESET) {
        (void)camper_core_request_factory_reset(core);
    }
}

esp_err_t camper_core_start(camper_core_t *core)
{
    if (core == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (core->state == CAMPER_STATE_SAFE_MODE) {
        logger_warning(core->deps.logger, TAG, "starting in safe mode");
    }

    if (core->deps.event_bus != NULL) {
        event_bus_subscribe(core->deps.event_bus, EVT_SYSTEM_REBOOT,
                            core_remote_system_handler, core);
        event_bus_subscribe(core->deps.event_bus, EVT_FACTORY_RESET,
                            core_remote_system_handler, core);
    }

    esp_err_t err = core_start_modules(core);
    if (err != ESP_OK) {
        core->state = CAMPER_STATE_SAFE_MODE;
        return err;
    }

    core_publish(core->deps.logger, core->deps.event_bus, EVT_BOOT, 0);

    if (core->state != CAMPER_STATE_SAFE_MODE) {
        core->state = CAMPER_STATE_RUNNING;
    }

    logger_info(core->deps.logger, TAG, "system running (state=%d)", (int)core->state);
    return ESP_OK;
}

camper_state_t camper_core_get_state(const camper_core_t *core)
{
    if (core == NULL) {
        return CAMPER_STATE_SAFE_MODE;
    }
    return core->state;
}

esp_err_t camper_core_request_reboot(camper_core_t *core, reboot_reason_t reason)
{
    if (core == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    logger_info(core->deps.logger, TAG, "reboot requested (reason=%d)", (int)reason);

    core->pending_reboot = reason;
    core->reboot_pending = true;

    if (reason == REBOOT_REASON_PROFILE_CHANGE || reason == REBOOT_REASON_CONFIG_CHANGE) {
        core->state = CAMPER_STATE_CONFIG_PENDING;
    }

    core_publish(core->deps.logger, core->deps.event_bus, EVT_SYSTEM_REBOOT, (int32_t)reason);
    return ESP_OK;
}

esp_err_t camper_core_request_factory_reset(camper_core_t *core)
{
    if (core == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    logger_warning(core->deps.logger, TAG, "factory reset requested");

    core->state = CAMPER_STATE_FACTORY_RESET;
    core_publish(core->deps.logger, core->deps.event_bus, EVT_FACTORY_RESET, 0);

    if (core->deps.storage != NULL) {
        esp_err_t err = storage_factory_reset(core->deps.storage);
        if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
            logger_error(core->deps.logger, TAG, "factory reset failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    return camper_core_request_reboot(core, REBOOT_REASON_FACTORY_RESET);
}

void camper_core_run(camper_core_t *core)
{
    if (core == NULL) {
        return;
    }

    logger_t *log = core->deps.logger;
    watchdog_t *wdt = core->deps.watchdog;

    logger_info(log, TAG, "supervisor loop started");

    while (true) {
        if (wdt != NULL) {
            watchdog_feed(wdt);
        }

        if (core->reboot_pending) {
            logger_info(log, TAG, "rebooting in %d ms", REBOOT_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(REBOOT_DELAY_MS));
            esp_restart();
        }

        vTaskDelay(pdMS_TO_TICKS(SUPERVISOR_MS));
    }
}
