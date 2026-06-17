/**
 * @file zigbee_device.c
 * @brief Zigbee End Device stack — HA on/off + CamperNode config cluster.
 */

#include "zigbee_device.h"
#include "zigbee_camper_cluster.h"
#include "zigbee_clusters.h"
#include "zigbee_platform_cfg.h"
#include "profile_mgr.h"
#include "profile_interface.h"
#include "profile_relay.h"
#include "storage.h"
#include "event_types.h"
#include "camper_config.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "zcl/esp_zigbee_zcl_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef CONFIG_ZB_ZED
#error "Zigbee End Device must be enabled (CONFIG_ZB_ZED=y in sdkconfig)"
#endif

#define TAG "ZB_DEV"

static event_bus_t *s_event_bus;
static profile_mgr_t *s_profile_mgr;
static bool s_joined;

static void zb_publish_cmd(int32_t cmd)
{
    if (s_event_bus == NULL) {
        return;
    }

    event_t evt = {
        .type = EVT_ZIGBEE_CMD,
        .source_id = 0x0500,
        .gpio_id = EVENT_GPIO_NONE,
        .data.int_val = cmd,
    };
    event_bus_publish(s_event_bus, &evt);
}

static void zb_relay_state_handler(const event_t *evt, void *arg)
{
    (void)arg;
    if (evt == NULL || !s_joined) {
        return;
    }

    if (profile_mgr_get_active_id(s_profile_mgr) != PROFILE_ID_RELAY) {
        return;
    }

    bool on = (evt->type == EVT_RELAY_ON);
    zigbee_device_report_on_off(profile_relay_get_endpoint(), on);
}

static void bdb_commissioning_cb(uint8_t mode_mask)
{
    esp_zb_bdb_start_top_level_commissioning(mode_mask);
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "rejoining saved network");
            }
        } else {
            ESP_LOGW(TAG, "stack init failed: %s", esp_err_to_name(err_status));
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            s_joined = true;
            ESP_LOGI(TAG, "joined network (short addr 0x%04x)", esp_zb_get_short_address());
        } else {
            ESP_LOGW(TAG, "steering failed, retrying");
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_commissioning_cb,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;

    default:
        ESP_LOGD(TAG, "signal %s status %s",
                 esp_zb_zdo_signal_to_string(sig_type), esp_err_to_name(err_status));
        break;
    }
}

static esp_err_t zb_on_off_attr_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS,
                        ESP_ERR_INVALID_ARG, TAG, "zcl error %d", message->info.status);

    if (message->info.cluster != ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
        return ESP_OK;
    }

    if (message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID &&
        message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
        bool on = message->attribute.data.value ?
                  *(bool *)message->attribute.data.value : false;
        ESP_LOGI(TAG, "on/off command endpoint=%d -> %s",
                 message->info.dst_endpoint, on ? "ON" : "OFF");
        zb_publish_cmd(on ? ZIGBEE_CMD_ON : ZIGBEE_CMD_OFF);
    }

    return ESP_OK;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID: {
        const esp_zb_zcl_set_attr_value_message_t *set_msg =
            (const esp_zb_zcl_set_attr_value_message_t *)message;
        esp_err_t err = zigbee_camper_cluster_handle_set_attr(set_msg);
        if (err != ESP_OK) {
            return err;
        }
        return zb_on_off_attr_handler(set_msg);
    }
    case ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID:
        return zigbee_camper_cluster_handle_custom_cmd(
            (const esp_zb_zcl_custom_cluster_command_message_t *)message);
    default:
        return ESP_OK;
    }
}

void zigbee_device_report_on_off(uint8_t endpoint, bool on)
{
    esp_zb_zcl_status_t status = esp_zb_zcl_set_attribute_val(
        endpoint,
        ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
        (void *)&on,
        false);

    if (status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "report on/off failed: %d", status);
    }
}

void zigbee_device_set_joined(bool joined)
{
    s_joined = joined;
}

bool zigbee_device_is_joined(void)
{
    return s_joined;
}

static void zb_register_endpoints(profile_mgr_t *profile_mgr)
{
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();

    if (profile_mgr != NULL && profile_mgr_get_active_id(profile_mgr) == PROFILE_ID_RELAY) {
        esp_zb_on_off_light_cfg_t light_cfg = ESP_ZB_DEFAULT_ON_OFF_LIGHT_CONFIG();
        esp_zb_ep_list_t *relay_ep = esp_zb_on_off_light_ep_create(
            profile_relay_get_endpoint(), &light_cfg);

        if (relay_ep != NULL) {
            ep_list = relay_ep;
            ESP_LOGI(TAG, "registered relay HA endpoint %u", profile_relay_get_endpoint());
        }
    }

    esp_err_t err = zigbee_camper_cluster_add_endpoint(ep_list);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "config endpoint registration failed: %s", esp_err_to_name(err));
    }

    esp_zb_device_register(ep_list);
}

static void esp_zb_task(void *arg)
{
    zigbee_device_deps_t *deps = (zigbee_device_deps_t *)arg;

    esp_zb_cfg_t zb_nwk_cfg = camper_zb_zed_config();
    esp_zb_init(&zb_nwk_cfg);

    zigbee_camper_deps_t cluster_deps = {
        .event_bus = deps->event_bus,
        .storage = deps->storage,
        .profile_mgr = deps->profile_mgr,
        .gpio_mgr = deps->gpio_mgr,
        .logger = deps->logger,
        .ota_mgr = deps->ota_mgr,
    };
    ESP_ERROR_CHECK(zigbee_camper_cluster_init(&cluster_deps));

    zb_register_endpoints(deps->profile_mgr);

    esp_zb_core_action_handler_register(zb_action_handler);
    ESP_ERROR_CHECK(zigbee_camper_cluster_register_handlers());
    esp_zb_set_primary_network_channel_set(CAMPER_ZB_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    zigbee_camper_cluster_start_diagnostics_timer();

    while (true) {
        esp_zb_stack_main_loop_iteration();
    }
}

esp_err_t zigbee_device_start(const zigbee_device_deps_t *deps)
{
    if (deps == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_event_bus = deps->event_bus;
    s_profile_mgr = deps->profile_mgr;
    s_joined = false;

    if (deps->event_bus != NULL) {
        event_bus_subscribe(deps->event_bus, EVT_RELAY_ON, zb_relay_state_handler, NULL);
        event_bus_subscribe(deps->event_bus, EVT_RELAY_OFF, zb_relay_state_handler, NULL);
    }

    esp_zb_platform_config_t config = camper_zb_platform_config();
    esp_err_t err = esp_zb_platform_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    static zigbee_device_deps_t s_task_deps;
    s_task_deps = *deps;

    BaseType_t created = xTaskCreate(esp_zb_task, "zb_main", 8192, &s_task_deps, 5, NULL);
    return (created == pdPASS) ? ESP_OK : ESP_FAIL;
}
