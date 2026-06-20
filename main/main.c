/**
 * @file main.c
 * @brief CamperNode OS entry point — dependency injection and Core bootstrap.
 */

#include "camper_config.h"
#include "camper_core.h"
#include "event_bus.h"
#include "gpio_mgr.h"
#include "logger.h"
#include "ota_mgr.h"
#include "profile_mgr.h"
#include "storage.h"
#include "watchdog.h"
#include "zigbee_mgr.h"

#include "esp_log.h"

static const char *TAG = "MAIN";

typedef struct {
    logger_t      *logger;
    watchdog_t    *watchdog;
    event_bus_t   *event_bus;
    storage_t     *storage;
    gpio_mgr_t    *gpio_mgr;
    profile_mgr_t *profile_mgr;
    zigbee_mgr_t  *zigbee_mgr;
    ota_mgr_t     *ota_mgr;
    camper_core_t *core;
} app_context_t;

static void app_teardown(app_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->core != NULL) {
        camper_core_destroy(ctx->core);
        ctx->core = NULL;
    }
    if (ctx->ota_mgr != NULL) {
        ota_mgr_destroy(ctx->ota_mgr);
        ctx->ota_mgr = NULL;
    }
    if (ctx->zigbee_mgr != NULL) {
        zigbee_mgr_destroy(ctx->zigbee_mgr);
        ctx->zigbee_mgr = NULL;
    }
    if (ctx->profile_mgr != NULL) {
        profile_mgr_destroy(ctx->profile_mgr);
        ctx->profile_mgr = NULL;
    }
    if (ctx->gpio_mgr != NULL) {
        gpio_mgr_destroy(ctx->gpio_mgr);
        ctx->gpio_mgr = NULL;
    }
    if (ctx->storage != NULL) {
        storage_destroy(ctx->storage);
        ctx->storage = NULL;
    }
    if (ctx->event_bus != NULL) {
        event_bus_destroy(ctx->event_bus);
        ctx->event_bus = NULL;
    }
    if (ctx->watchdog != NULL) {
        watchdog_destroy(ctx->watchdog);
        ctx->watchdog = NULL;
    }
    if (ctx->logger != NULL) {
        logger_destroy(ctx->logger);
        ctx->logger = NULL;
    }
}

void app_main(void)
{
    app_context_t ctx = {0};
    esp_err_t err;

    ctx.logger = logger_create();
    if (ctx.logger == NULL) {
        ESP_LOGE(TAG, "failed to create logger");
        return;
    }

    logger_info(ctx.logger, TAG, "%s v%s", CAMPER_FIRMWARE_NAME, CAMPER_FIRMWARE_VERSION);

    ctx.watchdog = watchdog_create();
    ctx.event_bus = event_bus_create();
    ctx.storage = storage_create();
    ctx.gpio_mgr = gpio_mgr_create(ctx.event_bus);

    profile_ctx_t profile_ctx = {
        .event_bus = ctx.event_bus,
        .storage = ctx.storage,
        .gpio_mgr = ctx.gpio_mgr,
        .logger = ctx.logger,
    };
    ctx.profile_mgr = profile_mgr_create(&profile_ctx);

    ctx.ota_mgr = ota_mgr_create(ctx.event_bus, ctx.storage);

    ctx.zigbee_mgr = zigbee_mgr_create(&(zigbee_mgr_deps_t){
        .event_bus = ctx.event_bus,
        .storage = ctx.storage,
        .profile_mgr = ctx.profile_mgr,
        .gpio_mgr = ctx.gpio_mgr,
        .logger = ctx.logger,
        .ota_mgr = ctx.ota_mgr,
    });

    if (ctx.watchdog == NULL || ctx.gpio_mgr == NULL || ctx.profile_mgr == NULL ||
        ctx.ota_mgr == NULL) {
        logger_error(ctx.logger, TAG, "module allocation failed");
        app_teardown(&ctx);
        return;
    }

    camper_core_deps_t deps = {
        .event_bus = ctx.event_bus,
        .storage = ctx.storage,
        .profile_mgr = ctx.profile_mgr,
        .gpio_mgr = ctx.gpio_mgr,
        .zigbee_mgr = ctx.zigbee_mgr,
        .ota_mgr = ctx.ota_mgr,
        .logger = ctx.logger,
        .watchdog = ctx.watchdog,
    };

    ctx.core = camper_core_create(&deps);
    if (ctx.core == NULL) {
        logger_error(ctx.logger, TAG, "failed to create core");
        app_teardown(&ctx);
        return;
    }

    err = camper_core_init(ctx.core);
    if (err != ESP_OK) {
        logger_error(ctx.logger, TAG, "core init failed: %s", esp_err_to_name(err));
        app_teardown(&ctx);
        return;
    }

    err = camper_core_start(ctx.core);
    if (err != ESP_OK) {
        logger_error(ctx.logger, TAG, "core start failed: %s", esp_err_to_name(err));
        app_teardown(&ctx);
        return;
    }

  /* Supervisor loop does not return; teardown is unreachable on normal operation. */
    camper_core_run(ctx.core);
}
