/**
 * @file ota_mgr.c
 * @brief OTA firmware update manager with rollback support.
 */

#include "ota_mgr.h"
#include "storage.h"
#include "event_types.h"

#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>

#define TAG            "OTA"
#define SOURCE_ID_OTA  0x0400U

struct ota_mgr {
    event_bus_t *event_bus;
    storage_t   *storage;
    ota_state_t  state;
};

static void ota_publish(ota_mgr_t *mgr, event_type_t type, int32_t value)
{
    if (mgr == NULL || mgr->event_bus == NULL) {
        return;
    }

    event_t evt = {
        .type = type,
        .source_id = SOURCE_ID_OTA,
        .gpio_id = EVENT_GPIO_NONE,
        .data.int_val = value,
    };
    event_bus_publish(mgr->event_bus, &evt);
}

ota_mgr_t *ota_mgr_create(event_bus_t *event_bus, storage_t *storage)
{
    ota_mgr_t *mgr = calloc(1, sizeof(ota_mgr_t));
    if (mgr != NULL) {
        mgr->event_bus = event_bus;
        mgr->storage = storage;
        mgr->state = OTA_STATE_IDLE;
    }
    return mgr;
}

void ota_mgr_destroy(ota_mgr_t *mgr)
{
    free(mgr);
}

esp_err_t ota_mgr_init(ota_mgr_t *mgr)
{
    if (mgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t app_desc;
    esp_err_t err = esp_ota_get_partition_description(running, &app_desc);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "running firmware %s", app_desc.version);
        if (mgr->storage != NULL) {
            storage_set_string(mgr->storage, CAMPER_NVS_NS_SYSTEM, CAMPER_KEY_OTA_VERSION,
                               app_desc.version);
        }
    }

    esp_ota_img_states_t ota_state;
    err = esp_ota_get_state_partition(running, &ota_state);
    if (err == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "image pending verification");
        mgr->state = OTA_STATE_VERIFYING;
    }

    ESP_LOGI(TAG, "initialized");
    return ESP_OK;
}

esp_err_t ota_mgr_start_update(ota_mgr_t *mgr, const char *url)
{
    if (mgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (url == NULL || url[0] == '\0') {
        ota_publish(mgr, EVT_OTA_START, 0);
        return ESP_ERR_NOT_SUPPORTED;
    }

    (void)url;
    mgr->state = OTA_STATE_FAILED;
    ota_publish(mgr, EVT_OTA_START, 0);
    ota_publish(mgr, EVT_OTA_FINISHED, -1);
    ESP_LOGW(TAG, "HTTP OTA not enabled — use Zigbee OTA");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ota_mgr_request_zigbee(ota_mgr_t *mgr)
{
    if (mgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ota_publish(mgr, EVT_OTA_START, 0);
    mgr->state = OTA_STATE_IDLE;
    return ESP_OK;
}

void ota_mgr_set_state(ota_mgr_t *mgr, ota_state_t state)
{
    if (mgr != NULL) {
        mgr->state = state;
    }
}

ota_state_t ota_mgr_get_state(const ota_mgr_t *mgr)
{
    if (mgr == NULL) {
        return OTA_STATE_IDLE;
    }
    return mgr->state;
}

esp_err_t ota_mgr_mark_valid(ota_mgr_t *mgr)
{
    if (mgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);
    if (err != ESP_OK) {
        return err;
    }

    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            mgr->state = OTA_STATE_DONE;
            ota_publish(mgr, EVT_OTA_FINISHED, 1);
            ESP_LOGI(TAG, "firmware marked valid");
        }
        return err;
    }

    mgr->state = OTA_STATE_IDLE;
    return ESP_OK;
}
