/**
 * @file zigbee_device.c
 * @brief Zigbee End Device stack — HA on/off + CamperNode config cluster.
 */

#include "zigbee_device.h"
#include "zigbee_camper_cluster.h"
#include "zigbee_gpio_io.h"
#include "zigbee_clusters.h"
#include "zigbee_ota.h"
#include "zigbee_platform_cfg.h"
#include "profile_mgr.h"
#include "profile_interface.h"
#include "profile_relay.h"
#include "camper_features.h"
#include "status_led.h"
#include "storage.h"
#include "event_types.h"
#include "camper_config.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "nwk/esp_zigbee_nwk.h"
#include "zcl/esp_zigbee_zcl_common.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "zcl/esp_zigbee_zcl_core.h"
#include "zdo/esp_zigbee_zdo_command.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#ifndef CONFIG_ZB_ZED
#error "Zigbee End Device must be enabled (CONFIG_ZB_ZED=y in sdkconfig)"
#endif

#define TAG "ZB_DEV"

/** ZHA coordinator (ConBee, Sonoff, etc.) listens on endpoint 1. */
#define ZB_COORDINATOR_SHORT_ADDR  0x0000U
#define ZB_COORDINATOR_ENDPOINT    1U

static event_bus_t *s_event_bus;
static profile_mgr_t *s_profile_mgr;
static bool s_joined;
static bool s_temp_ready;
static bool s_temp_use_direct_addr;
static float s_last_temp_c;
static bool s_last_temp_valid;
static uint8_t s_pending_temp_ep;
static float s_pending_temp_c;
static uint8_t s_temp_bind_ep;

static bool s_dht_ready;
static float s_last_dht_temp_c;
static float s_last_dht_hum_pct;
static bool s_last_dht_valid;
static uint8_t s_pending_dht_ep;
static float s_pending_dht_temp_c;
static float s_pending_dht_hum_pct;
static uint8_t s_dht_bind_ep;
static uint8_t s_dht_bind_step;

static void zb_temp_report_alarm_cb(uint8_t param);
static void zb_dht_report_alarm_cb(uint8_t param);
static void zb_temp_cluster_setup_cb(uint8_t param);
static void zb_dht_cluster_setup_cb(uint8_t param);
static void zb_on_network_joined(void);
static void zb_schedule_temp_report(uint8_t endpoint, float temp_c);
static void zb_schedule_dht_report(uint8_t endpoint, float temp_c, float hum_pct);
static esp_err_t zb_enable_temp_reporting(uint8_t endpoint);
static esp_err_t zb_enable_humidity_reporting(uint8_t endpoint);
static void zb_bind_temp_cluster(uint8_t endpoint);
static void zb_bind_dht_clusters(uint8_t endpoint);
static esp_err_t zb_add_dht_endpoint(esp_zb_ep_list_t *ep_list, uint8_t endpoint);
static void zb_register_identify_handlers(profile_mgr_t *profile_mgr);
static void zb_identify_notify_cb(uint8_t identify_on);

static void zb_temp_state_handler(const event_t *evt, void *arg)
{
    (void)arg;
    if (evt == NULL || s_profile_mgr == NULL) {
        return;
    }
    if (evt->type != EVT_TEMPERATURE_UPDATE) {
        return;
    }
    if (!profile_mgr_temperature_enabled(s_profile_mgr)) {
        return;
    }

    s_last_temp_c = evt->data.float_val;
    s_last_temp_valid = true;

    if (!s_joined) {
        ESP_LOGI(TAG, "temp %.2f C cached (waiting for network)", evt->data.float_val);
        return;
    }

    zb_schedule_temp_report(profile_mgr_get_temp_endpoint(s_profile_mgr), evt->data.float_val);
}

static void zb_temp_report_alarm_cb(uint8_t param)
{
    (void)param;
    if (!s_joined || s_profile_mgr == NULL || !profile_mgr_temperature_enabled(s_profile_mgr)) {
        return;
    }
    zigbee_device_report_temperature(s_pending_temp_ep, s_pending_temp_c);
}

static void zb_schedule_temp_report(uint8_t endpoint, float temp_c)
{
    s_pending_temp_ep = endpoint;
    s_pending_temp_c = temp_c;
    esp_zb_scheduler_alarm((esp_zb_callback_t)zb_temp_report_alarm_cb, 0, 0);
}

static void zb_temp_report_ready(uint8_t endpoint)
{
    s_temp_ready = true;

    if (esp_zb_lock_acquire(portMAX_DELAY)) {
        zb_enable_temp_reporting(endpoint);
        esp_zb_lock_release();
    }

    if (s_last_temp_valid) {
        zb_schedule_temp_report(endpoint, s_last_temp_c);
    }
}

static void zb_identify_notify_cb(uint8_t identify_on)
{
    if (identify_on == 0U) {
        status_led_identify_stop();
        return;
    }

    ESP_LOGI(TAG, "identify from HA (%u s requested)", (unsigned)identify_on);
    status_led_identify_start(CAMPER_IDENTIFY_DURATION_SEC);
}

static esp_err_t zb_identify_effect_handler(const esp_zb_zcl_identify_effect_message_t *message)
{
    if (message == NULL) {
        return ESP_FAIL;
    }

    if (message->effect_id == ESP_ZB_ZCL_IDENTIFY_EFFECT_ID_STOP ||
        message->effect_id == ESP_ZB_ZCL_IDENTIFY_EFFECT_ID_FINISH_EFFECT) {
        status_led_identify_stop();
        return ESP_OK;
    }

    ESP_LOGI(TAG, "identify effect 0x%02x on ep %u",
             message->effect_id, message->info.dst_endpoint);
    status_led_identify_start(CAMPER_IDENTIFY_DURATION_SEC);
    return ESP_OK;
}

static void zb_register_identify_handlers(profile_mgr_t *profile_mgr)
{
    esp_zb_identify_notify_handler_register(profile_relay_get_endpoint(), zb_identify_notify_cb);

    if (profile_mgr != NULL && profile_mgr_temperature_enabled(profile_mgr)) {
        esp_zb_identify_notify_handler_register(
            profile_mgr_get_temp_endpoint(profile_mgr), zb_identify_notify_cb);
    }

    if (profile_mgr != NULL && profile_mgr_dht_enabled(profile_mgr)) {
        esp_zb_identify_notify_handler_register(
            profile_mgr_get_dht_endpoint(profile_mgr), zb_identify_notify_cb);
    }
}

static void zb_bind_temp_cluster_cb(esp_zb_zdp_status_t zdo_status, void *user_ctx)
{
    (void)user_ctx;

    if (zdo_status == ESP_ZB_ZDP_STATUS_SUCCESS) {
        s_temp_use_direct_addr = false;
        ESP_LOGI(TAG, "temperature cluster bound to coordinator");
        zb_temp_report_ready(s_temp_bind_ep);
        return;
    }

    if (zdo_status == ESP_ZB_ZDP_STATUS_NOT_SUPPORTED ||
        zdo_status == ESP_ZB_ZDP_STATUS_TIMEOUT) {
        /* ZHA: Bind oft abgelehnt (132) oder ohne Antwort (133) — direkt an Coordinator senden. */
        s_temp_use_direct_addr = true;
        ESP_LOGI(TAG, "coordinator bind skipped (%d) — using direct reports", zdo_status);
        zb_temp_report_ready(s_temp_bind_ep);
        return;
    }

    ESP_LOGW(TAG, "temperature cluster bind failed: %d, retry in 2s", zdo_status);
    esp_zb_scheduler_alarm((esp_zb_callback_t)zb_temp_cluster_setup_cb, 0, 2000);
}

static void zb_bind_temp_cluster(uint8_t endpoint)
{
    esp_zb_zdo_bind_req_param_t bind_req = {0};
    esp_zb_ieee_addr_t coord_ieee = {0};

    s_temp_bind_ep = endpoint;
    s_temp_ready = false;
    s_temp_use_direct_addr = false;

    if (esp_zb_ieee_address_by_short(ZB_COORDINATOR_SHORT_ADDR, coord_ieee) != ESP_OK) {
        ESP_LOGW(TAG, "coordinator ieee lookup failed, retry bind in 2s");
        esp_zb_scheduler_alarm((esp_zb_callback_t)zb_temp_cluster_setup_cb, 0, 2000);
        return;
    }

    esp_zb_get_long_address(bind_req.src_address);
    bind_req.src_endp = endpoint;
    bind_req.cluster_id = ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT;
    bind_req.dst_addr_mode = ESP_ZB_ZDO_BIND_DST_ADDR_MODE_64_BIT_EXTENDED;
    memcpy(bind_req.dst_address_u.addr_long, coord_ieee, sizeof(esp_zb_ieee_addr_t));
    bind_req.dst_endp = ZB_COORDINATOR_ENDPOINT;
    bind_req.req_dst_addr = ZB_COORDINATOR_SHORT_ADDR;

    esp_zb_zdo_device_bind_req(&bind_req, zb_bind_temp_cluster_cb, NULL);
    ESP_LOGI(TAG, "binding temperature cluster ep=%u to coordinator", endpoint);
}

static esp_zb_zcl_attr_location_info_t zb_temp_attr_loc(uint8_t endpoint)
{
    return (esp_zb_zcl_attr_location_info_t){
        .endpoint_id = endpoint,
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
        .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        .manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC,
        .attr_id = ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
    };
}

static esp_err_t zb_enable_temp_reporting(uint8_t endpoint)
{
    esp_zb_zcl_attr_location_info_t loc = zb_temp_attr_loc(endpoint);

    if (esp_zb_zcl_find_reporting_info(loc) != NULL) {
        esp_err_t err = esp_zb_zcl_start_attr_reporting(loc);
        ESP_LOGI(TAG, "temp reporting active (ZHA config)");
        return err;
    }

    esp_zb_zcl_reporting_info_t report = {0};
    report.direction = ESP_ZB_ZCL_REPORT_DIRECTION_SEND;
    report.ep = endpoint;
    report.cluster_id = ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT;
    report.cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE;
    report.attr_id = ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID;
    report.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;
    report.u.send_info.min_interval = 30;
    report.u.send_info.max_interval = 300;
    report.u.send_info.def_min_interval = 30;
    report.u.send_info.def_max_interval = 300;
    report.u.send_info.delta.s16 = 10;
    report.dst.short_addr = ZB_COORDINATOR_SHORT_ADDR;
    report.dst.endpoint = ZB_COORDINATOR_ENDPOINT;
    report.dst.profile_id = ESP_ZB_AF_HA_PROFILE_ID;

    esp_err_t err = esp_zb_zcl_update_reporting_info(&report);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "temp reporting config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_zb_zcl_start_attr_reporting(loc);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "temp reporting -> coordinator 0x%04x ep %u",
                 ZB_COORDINATOR_SHORT_ADDR, ZB_COORDINATOR_ENDPOINT);
    }
    return err;
}

static void zb_temp_cluster_setup_cb(uint8_t param)
{
    (void)param;

    if (!s_joined || s_temp_ready || s_profile_mgr == NULL ||
        !profile_mgr_temperature_enabled(s_profile_mgr)) {
        return;
    }

    zb_bind_temp_cluster(profile_mgr_get_temp_endpoint(s_profile_mgr));
}

static void zb_dht_temp_handler(const event_t *evt, void *arg)
{
    (void)arg;
    if (evt == NULL || s_profile_mgr == NULL || evt->type != EVT_DHT_TEMPERATURE_UPDATE) {
        return;
    }
    if (!profile_mgr_dht_enabled(s_profile_mgr)) {
        return;
    }

    s_last_dht_temp_c = evt->data.float_val;

    if (!s_joined) {
        ESP_LOGI(TAG, "DHT temp %.2f C cached (waiting for network)", evt->data.float_val);
    }
}

static void zb_dht_humidity_handler(const event_t *evt, void *arg)
{
    (void)arg;
    if (evt == NULL || s_profile_mgr == NULL || evt->type != EVT_HUMIDITY_UPDATE) {
        return;
    }
    if (!profile_mgr_dht_enabled(s_profile_mgr)) {
        return;
    }

    s_last_dht_hum_pct = evt->data.float_val;
    s_last_dht_valid = true;

    if (!s_joined) {
        ESP_LOGI(TAG, "DHT humidity %.1f %% cached (waiting for network)", evt->data.float_val);
        return;
    }

    zb_schedule_dht_report(profile_mgr_get_dht_endpoint(s_profile_mgr),
                           s_last_dht_temp_c, evt->data.float_val);
}

static void zb_dht_report_alarm_cb(uint8_t param)
{
    (void)param;
    if (!s_joined || s_profile_mgr == NULL || !profile_mgr_dht_enabled(s_profile_mgr)) {
        return;
    }
    zigbee_device_report_temperature(s_pending_dht_ep, s_pending_dht_temp_c);
    zigbee_device_report_humidity(s_pending_dht_ep, s_pending_dht_hum_pct);
}

static void zb_schedule_dht_report(uint8_t endpoint, float temp_c, float hum_pct)
{
    s_pending_dht_ep = endpoint;
    s_pending_dht_temp_c = temp_c;
    s_pending_dht_hum_pct = hum_pct;
    esp_zb_scheduler_alarm((esp_zb_callback_t)zb_dht_report_alarm_cb, 0, 0);
}

static void zb_dht_report_ready(uint8_t endpoint)
{
    s_dht_ready = true;

    if (esp_zb_lock_acquire(portMAX_DELAY)) {
        zb_enable_temp_reporting(endpoint);
        zb_enable_humidity_reporting(endpoint);
        esp_zb_lock_release();
    }

    if (s_last_dht_valid) {
        zb_schedule_dht_report(endpoint, s_last_dht_temp_c, s_last_dht_hum_pct);
    }
}

static void zb_bind_dht_cluster_cb(esp_zb_zdp_status_t zdo_status, void *user_ctx)
{
    (void)user_ctx;

    if (zdo_status == ESP_ZB_ZDP_STATUS_SUCCESS ||
        zdo_status == ESP_ZB_ZDP_STATUS_NOT_SUPPORTED ||
        zdo_status == ESP_ZB_ZDP_STATUS_TIMEOUT) {
        if (s_dht_bind_step == 0) {
            s_dht_bind_step = 1;
            esp_zb_zdo_bind_req_param_t bind_req = {0};
            esp_zb_ieee_addr_t coord_ieee = {0};

            if (esp_zb_ieee_address_by_short(ZB_COORDINATOR_SHORT_ADDR, coord_ieee) != ESP_OK) {
                esp_zb_scheduler_alarm((esp_zb_callback_t)zb_dht_cluster_setup_cb, 0, 2000);
                return;
            }

            esp_zb_get_long_address(bind_req.src_address);
            bind_req.src_endp = s_dht_bind_ep;
            bind_req.cluster_id = ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT;
            bind_req.dst_addr_mode = ESP_ZB_ZDO_BIND_DST_ADDR_MODE_64_BIT_EXTENDED;
            memcpy(bind_req.dst_address_u.addr_long, coord_ieee, sizeof(esp_zb_ieee_addr_t));
            bind_req.dst_endp = ZB_COORDINATOR_ENDPOINT;
            bind_req.req_dst_addr = ZB_COORDINATOR_SHORT_ADDR;
            esp_zb_zdo_device_bind_req(&bind_req, zb_bind_dht_cluster_cb, NULL);
            ESP_LOGI(TAG, "binding humidity cluster ep=%u to coordinator", s_dht_bind_ep);
            return;
        }

        ESP_LOGI(TAG, "DHT clusters bound to coordinator");
        zb_dht_report_ready(s_dht_bind_ep);
        return;
    }

    ESP_LOGW(TAG, "DHT cluster bind failed: %d, retry in 2s", zdo_status);
    esp_zb_scheduler_alarm((esp_zb_callback_t)zb_dht_cluster_setup_cb, 0, 2000);
}

static void zb_bind_dht_clusters(uint8_t endpoint)
{
    esp_zb_zdo_bind_req_param_t bind_req = {0};
    esp_zb_ieee_addr_t coord_ieee = {0};

    s_dht_bind_ep = endpoint;
    s_dht_ready = false;
    s_dht_bind_step = 0;

    if (esp_zb_ieee_address_by_short(ZB_COORDINATOR_SHORT_ADDR, coord_ieee) != ESP_OK) {
        ESP_LOGW(TAG, "coordinator ieee lookup failed, retry DHT bind in 2s");
        esp_zb_scheduler_alarm((esp_zb_callback_t)zb_dht_cluster_setup_cb, 0, 2000);
        return;
    }

    esp_zb_get_long_address(bind_req.src_address);
    bind_req.src_endp = endpoint;
    bind_req.cluster_id = ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT;
    bind_req.dst_addr_mode = ESP_ZB_ZDO_BIND_DST_ADDR_MODE_64_BIT_EXTENDED;
    memcpy(bind_req.dst_address_u.addr_long, coord_ieee, sizeof(esp_zb_ieee_addr_t));
    bind_req.dst_endp = ZB_COORDINATOR_ENDPOINT;
    bind_req.req_dst_addr = ZB_COORDINATOR_SHORT_ADDR;

    esp_zb_zdo_device_bind_req(&bind_req, zb_bind_dht_cluster_cb, NULL);
    ESP_LOGI(TAG, "binding DHT temperature cluster ep=%u to coordinator", endpoint);
}

static esp_err_t zb_enable_humidity_reporting(uint8_t endpoint)
{
    esp_zb_zcl_attr_location_info_t loc = {
        .endpoint_id = endpoint,
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
        .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        .manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC,
        .attr_id = ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID,
    };

    if (esp_zb_zcl_find_reporting_info(loc) != NULL) {
        return esp_zb_zcl_start_attr_reporting(loc);
    }

    esp_zb_zcl_reporting_info_t report = {0};
    report.direction = ESP_ZB_ZCL_REPORT_DIRECTION_SEND;
    report.ep = endpoint;
    report.cluster_id = ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT;
    report.cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE;
    report.attr_id = ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID;
    report.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;
    report.u.send_info.min_interval = 30;
    report.u.send_info.max_interval = 300;
    report.u.send_info.def_min_interval = 30;
    report.u.send_info.def_max_interval = 300;
    report.u.send_info.delta.u16 = 100;
    report.dst.short_addr = ZB_COORDINATOR_SHORT_ADDR;
    report.dst.endpoint = ZB_COORDINATOR_ENDPOINT;
    report.dst.profile_id = ESP_ZB_AF_HA_PROFILE_ID;

    esp_err_t err = esp_zb_zcl_update_reporting_info(&report);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "humidity reporting config failed: %s", esp_err_to_name(err));
        return err;
    }

    return esp_zb_zcl_start_attr_reporting(loc);
}

static void zb_dht_cluster_setup_cb(uint8_t param)
{
    (void)param;

    if (!s_joined || s_dht_ready || s_profile_mgr == NULL ||
        !profile_mgr_dht_enabled(s_profile_mgr)) {
        return;
    }

    zb_bind_dht_clusters(profile_mgr_get_dht_endpoint(s_profile_mgr));
}

static void zb_on_network_joined(void)
{
    uint16_t short_addr = esp_zb_get_short_address();
    if (short_addr == 0xFFFFU || short_addr == 0xFFFEU) {
        return;
    }

    if (!s_joined) {
        ESP_LOGI(TAG, "joined network (short addr 0x%04x)", short_addr);
    }
    s_joined = true;

    zigbee_gpio_io_on_network_joined();

    if (s_profile_mgr != NULL && profile_mgr_temperature_enabled(s_profile_mgr)) {
        esp_zb_scheduler_alarm((esp_zb_callback_t)zb_temp_cluster_setup_cb, 0, 1000);
    }

    if (s_profile_mgr != NULL && profile_mgr_dht_enabled(s_profile_mgr)) {
        esp_zb_scheduler_alarm((esp_zb_callback_t)zb_dht_cluster_setup_cb, 0, 1500);
    }
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
                zb_on_network_joined();
            }
        } else {
            ESP_LOGW(TAG, "stack init failed: %s", esp_err_to_name(err_status));
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            zb_on_network_joined();
        } else {
            ESP_LOGW(TAG, "steering failed, retrying");
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_commissioning_cb,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        s_joined = false;
        s_temp_ready = false;
        s_temp_use_direct_addr = false;
        ESP_LOGW(TAG, "left network");
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
        if (message->info.dst_endpoint == profile_relay_get_endpoint()) {
            zigbee_gpio_io_set_output_bit(0, on);
        }
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
    case ESP_ZB_CORE_IDENTIFY_EFFECT_CB_ID:
        return zb_identify_effect_handler(
            (const esp_zb_zcl_identify_effect_message_t *)message);
    case ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID:
    case ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID:
        return zigbee_ota_handle_action(callback_id, message);
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

void zigbee_device_report_temperature(uint8_t endpoint, float temp_c)
{
    int16_t zb_temp = (int16_t)(temp_c * 100.0f);

    if (zb_temp < (int16_t)(-5500)) {
        zb_temp = (int16_t)(-5500);
    } else if (zb_temp > (int16_t)(12500)) {
        zb_temp = (int16_t)(12500);
    }

    esp_zb_zcl_status_t status = ESP_ZB_ZCL_STATUS_FAIL;

    if (esp_zb_lock_acquire(portMAX_DELAY)) {
        if (esp_zb_zcl_get_attribute(endpoint, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
                                     ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                     ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID) == NULL) {
            esp_zb_lock_release();
            ESP_LOGW(TAG, "temperature ep %u not registered", endpoint);
            return;
        }

        status = esp_zb_zcl_set_attribute_val(
            endpoint,
            ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
            (void *)&zb_temp,
            false);
        esp_zb_lock_release();
    }

    if (status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "report temperature failed: %d", status);
    } else if (!s_temp_ready) {
        ESP_LOGI(TAG, "temperature endpoint=%u -> %.2f C (local, awaiting network setup)", endpoint, temp_c);
    } else {
        ESP_LOGI(TAG, "temperature endpoint=%u -> %.2f C (attribute updated)", endpoint, temp_c);
    }
}

void zigbee_device_report_humidity(uint8_t endpoint, float hum_pct)
{
    uint16_t zb_hum = 0;

    if (hum_pct < 0.0f) {
        zb_hum = 0;
    } else if (hum_pct > 100.0f) {
        zb_hum = 10000U;
    } else {
        zb_hum = (uint16_t)(hum_pct * 100.0f);
    }

    esp_zb_zcl_status_t status = ESP_ZB_ZCL_STATUS_FAIL;

    if (esp_zb_lock_acquire(portMAX_DELAY)) {
        if (esp_zb_zcl_get_attribute(endpoint, ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
                                     ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                     ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID) == NULL) {
            esp_zb_lock_release();
            ESP_LOGW(TAG, "humidity ep %u not registered", endpoint);
            return;
        }

        status = esp_zb_zcl_set_attribute_val(
            endpoint,
            ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID,
            (void *)&zb_hum,
            false);
        esp_zb_lock_release();
    }

    if (status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "report humidity failed: %d", status);
    } else if (!s_dht_ready) {
        ESP_LOGI(TAG, "humidity endpoint=%u -> %.1f %% (local, awaiting network setup)", endpoint, hum_pct);
    } else {
        ESP_LOGI(TAG, "humidity endpoint=%u -> %.1f %% (attribute updated)", endpoint, hum_pct);
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

static esp_err_t zb_add_temp_endpoint(esp_zb_ep_list_t *ep_list, uint8_t endpoint)
{
    esp_zb_temperature_sensor_cfg_t cfg = ESP_ZB_DEFAULT_TEMPERATURE_SENSOR_CONFIG();
    /* Stack asserts in report_attr if min/max stay at UNKNOWN — DS18B20 range. */
    cfg.temp_meas_cfg.min_value = (int16_t)(-5500);
    cfg.temp_meas_cfg.max_value = (int16_t)(12500);
    cfg.temp_meas_cfg.measured_value = ESP_ZB_ZCL_TEMP_MEASUREMENT_MEASURED_VALUE_UNKNOWN;

    esp_zb_cluster_list_t *clusters = esp_zb_temperature_sensor_clusters_create(&cfg);
    if (clusters == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint = endpoint,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_TEMPERATURE_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };

    esp_err_t err = esp_zb_ep_list_add_ep(ep_list, clusters, ep_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "registered temperature HA endpoint %u", endpoint);
    }
    return err;
}

static esp_err_t zb_add_dht_endpoint(esp_zb_ep_list_t *ep_list, uint8_t endpoint)
{
    esp_zb_temperature_sensor_cfg_t cfg = ESP_ZB_DEFAULT_TEMPERATURE_SENSOR_CONFIG();
    cfg.temp_meas_cfg.min_value = (int16_t)(-4000);
    cfg.temp_meas_cfg.max_value = (int16_t)(8000);
    cfg.temp_meas_cfg.measured_value = ESP_ZB_ZCL_TEMP_MEASUREMENT_MEASURED_VALUE_UNKNOWN;

    esp_zb_cluster_list_t *clusters = esp_zb_temperature_sensor_clusters_create(&cfg);
    if (clusters == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uint16_t hum_default = ESP_ZB_ZCL_REL_HUMIDITY_MEASUREMENT_MEASURED_VALUE_UNKNOWN;
    uint16_t hum_min = 0;
    uint16_t hum_max = 10000U;

    esp_zb_attribute_list_t *humidity_cluster =
        esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT);
    if (humidity_cluster == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_zb_humidity_meas_cluster_add_attr(
        humidity_cluster, ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID, &hum_default);
    esp_zb_humidity_meas_cluster_add_attr(
        humidity_cluster, ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_MIN_VALUE_ID, &hum_min);
    esp_zb_humidity_meas_cluster_add_attr(
        humidity_cluster, ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_MAX_VALUE_ID, &hum_max);
    esp_zb_cluster_list_add_humidity_meas_cluster(clusters, humidity_cluster,
                                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint = endpoint,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_TEMPERATURE_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };

    esp_err_t err = esp_zb_ep_list_add_ep(ep_list, clusters, ep_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "registered DHT22 HA endpoint %u (temp + humidity)", endpoint);
    }
    return err;
}

static void zb_register_endpoints(profile_mgr_t *profile_mgr)
{
    esp_zb_on_off_light_cfg_t light_cfg = ESP_ZB_DEFAULT_ON_OFF_LIGHT_CONFIG();
    esp_zb_ep_list_t *ep_list = esp_zb_on_off_light_ep_create(
        profile_relay_get_endpoint(), &light_cfg);

    if (ep_list == NULL) {
        ep_list = esp_zb_ep_list_create();
    } else {
        ESP_LOGI(TAG, "registered relay HA endpoint %u", profile_relay_get_endpoint());
    }

    esp_err_t err = zb_add_temp_endpoint(ep_list, profile_mgr_get_temp_endpoint(profile_mgr));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "temperature endpoint failed: %s", esp_err_to_name(err));
    }

    err = zb_add_dht_endpoint(ep_list, profile_mgr_get_dht_endpoint(profile_mgr));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DHT endpoint failed: %s", esp_err_to_name(err));
    }

    err = zigbee_camper_cluster_add_endpoint(ep_list);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "config endpoint registration failed: %s", esp_err_to_name(err));
    }

    err = zigbee_ota_add_endpoint(ep_list);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA endpoint registration failed: %s", esp_err_to_name(err));
    }

    esp_zb_device_register(ep_list);
    (void)profile_mgr;
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
    zb_register_identify_handlers(deps->profile_mgr);

    esp_zb_core_action_handler_register(zb_action_handler);
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
    s_last_temp_valid = false;

    if (deps->event_bus != NULL) {
        event_bus_subscribe(deps->event_bus, EVT_TEMPERATURE_UPDATE, zb_temp_state_handler, NULL);
        event_bus_subscribe(deps->event_bus, EVT_DHT_TEMPERATURE_UPDATE, zb_dht_temp_handler, NULL);
        event_bus_subscribe(deps->event_bus, EVT_HUMIDITY_UPDATE, zb_dht_humidity_handler, NULL);
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
