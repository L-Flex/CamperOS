/**
 * @file zigbee_ota.c
 * @brief Zigbee OTA Upgrade client — download via ZHA/coordinator, flash inactive slot.
 */

#include "zigbee_ota.h"
#include "camper_config.h"
#include "event_types.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_zigbee_cluster.h"
#include "nwk/esp_zigbee_nwk.h"
#include "zdo/esp_zigbee_zdo_command.h"

#include <string.h>

#define TAG                    "ZB_OTA"
#define OTA_ELEMENT_HEADER_LEN 6U
#define OTA_UPGRADE_MAX_DATA   223U
#define OTA_QUERY_INTERVAL_MIN 60U

typedef enum {
    UPGRADE_IMAGE = 0x0000,
} ota_element_tag_id_t;

static ota_mgr_t *s_ota_mgr;
static const esp_partition_t *s_ota_partition;
static esp_ota_handle_t s_ota_handle;
static bool s_tagid_received;

static void zb_ota_match_desc_cb(esp_zb_zdp_status_t zdo_status, uint16_t addr, uint8_t endpoint, void *user_ctx)
{
    (void)user_ctx;

    if (zdo_status != ESP_ZB_ZDP_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "no OTA server on coordinator");
        return;
    }

    uint16_t server_addr = addr;
    uint8_t server_ep = endpoint;

    esp_zb_zcl_set_attribute_val(
        CAMPER_ZB_OTA_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_OTA_UPGRADE,
        ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE,
        ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ADDR_ID,
        &server_addr,
        false);
    esp_zb_zcl_set_attribute_val(
        CAMPER_ZB_OTA_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_OTA_UPGRADE,
        ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE,
        ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ENDPOINT_ID,
        &server_ep,
        false);

    esp_zb_ota_upgrade_client_query_interval_set(CAMPER_ZB_OTA_ENDPOINT, OTA_QUERY_INTERVAL_MIN);
    esp_err_t err = esp_zb_ota_upgrade_client_query_image_req(addr, endpoint);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "query image failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "OTA query sent to 0x%04x ep %u (mfg 0x%04x type 0x%04x cur 0x%08lx)",
             (unsigned)addr,
             (unsigned)endpoint,
             (unsigned)CAMPER_OTA_MANUFACTURER,
             (unsigned)CAMPER_OTA_IMAGE_TYPE,
             (unsigned long)CAMPER_OTA_FILE_VERSION);
}

static esp_err_t ota_element_data(uint32_t total_size, const void *payload, uint16_t payload_size,
                                  void **outbuf, uint16_t *outlen)
{
    static uint16_t tagid;

    void *data_buf = NULL;
    uint16_t data_len;

    if (!s_tagid_received) {
        uint32_t length;

        if (payload == NULL || payload_size <= OTA_ELEMENT_HEADER_LEN) {
            return ESP_ERR_INVALID_ARG;
        }

        tagid = *(const uint16_t *)payload;
        length = *(const uint32_t *)((const uint8_t *)payload + sizeof(tagid));
        if ((length + OTA_ELEMENT_HEADER_LEN) != total_size) {
            return ESP_ERR_INVALID_SIZE;
        }

        s_tagid_received = true;
        data_buf = (void *)((const uint8_t *)payload + OTA_ELEMENT_HEADER_LEN);
        data_len = (uint16_t)(payload_size - OTA_ELEMENT_HEADER_LEN);
    } else {
        data_buf = (void *)payload;
        data_len = payload_size;
    }

    if (tagid != UPGRADE_IMAGE) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    *outbuf = data_buf;
    *outlen = data_len;
    return ESP_OK;
}

static esp_err_t ota_upgrade_value_handler(esp_zb_zcl_ota_upgrade_value_message_t message)
{
    static uint32_t total_size;
    static uint32_t offset;
    static int64_t start_time;
    esp_err_t ret = ESP_OK;

    if (message.info.status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        return ESP_FAIL;
    }

    switch (message.upgrade_status) {
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START:
        ESP_LOGI(TAG, "OTA download start");
        start_time = esp_timer_get_time();
        offset = 0;
        total_size = 0;
        s_tagid_received = false;
        s_ota_partition = esp_ota_get_next_update_partition(NULL);
        if (s_ota_partition == NULL) {
            return ESP_ERR_NOT_FOUND;
        }
        if (s_ota_mgr != NULL) {
            ota_mgr_set_state(s_ota_mgr, OTA_STATE_DOWNLOADING);
        }
        ret = esp_ota_begin(s_ota_partition, 0, &s_ota_handle);
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE:
        total_size = message.ota_header.image_size;
        offset += message.payload_size;
        ESP_LOGI(TAG, "OTA progress %lu/%lu", (unsigned long)offset, (unsigned long)total_size);
        if (message.payload_size > 0 && message.payload != NULL) {
            void *payload = NULL;
            uint16_t payload_size = 0;

            ret = ota_element_data(total_size, message.payload, message.payload_size, &payload, &payload_size);
            if (ret == ESP_OK && payload_size > 0) {
                ret = esp_ota_write(s_ota_handle, payload, payload_size);
            }
        }
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_APPLY:
        ESP_LOGI(TAG, "OTA apply");
        if (s_ota_mgr != NULL) {
            ota_mgr_set_state(s_ota_mgr, OTA_STATE_APPLYING);
        }
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK:
        ret = (offset == total_size) ? ESP_OK : ESP_FAIL;
        offset = 0;
        total_size = 0;
        s_tagid_received = false;
        ESP_LOGI(TAG, "OTA check: %s", esp_err_to_name(ret));
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH:
        ESP_LOGI(TAG, "OTA finish (ver 0x%08lx, %lu B, %lld ms)",
                 (unsigned long)message.ota_header.file_version,
                 (unsigned long)message.ota_header.image_size,
                 (long long)((esp_timer_get_time() - start_time) / 1000));
        ret = esp_ota_end(s_ota_handle);
        if (ret == ESP_OK) {
            ret = esp_ota_set_boot_partition(s_ota_partition);
        }
        if (ret == ESP_OK) {
            ESP_LOGW(TAG, "rebooting into new firmware");
            esp_restart();
        }
        break;

    default:
        ESP_LOGD(TAG, "OTA status %d", message.upgrade_status);
        break;
    }

    return ret;
}

static esp_err_t ota_query_image_resp_handler(esp_zb_zcl_ota_upgrade_query_image_resp_message_t message)
{
    if (message.info.status == ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGI(TAG, "OTA image from 0x%04x ep %u ver 0x%08lx size %ld",
                 message.server_addr.u.short_addr,
                 (unsigned)message.server_endpoint,
                 (unsigned long)message.file_version,
                 (long)message.image_size);
    } else {
        ESP_LOGW(TAG, "OTA query rejected by coordinator (status 0x%02x) — "
                 "check HA zha_ota folder and advanced OTA provider config",
                 (unsigned)message.info.status);
    }
    return ESP_OK;
}

esp_err_t zigbee_ota_handle_action(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    if (message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (callback_id) {
    case ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID:
        return ota_upgrade_value_handler(*(const esp_zb_zcl_ota_upgrade_value_message_t *)message);
    case ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID:
        return ota_query_image_resp_handler(*(const esp_zb_zcl_ota_upgrade_query_image_resp_message_t *)message);
    default:
        return ESP_OK;
    }
}

esp_err_t zigbee_ota_add_endpoint(esp_zb_ep_list_t *ep_list)
{
    if (ep_list == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_zb_ota_cluster_cfg_t ota_cfg = {
        .ota_upgrade_manufacturer = CAMPER_OTA_MANUFACTURER,
        .ota_upgrade_image_type = CAMPER_OTA_IMAGE_TYPE,
        .ota_upgrade_file_version = CAMPER_OTA_FILE_VERSION,
        .ota_upgrade_downloaded_file_ver = 0xFFFFFFFFU,
    };

    esp_zb_zcl_ota_upgrade_client_variable_t client_var = {
        .timer_query = ESP_ZB_ZCL_OTA_UPGRADE_QUERY_TIMER_COUNT_DEF,
        .hw_version = CAMPER_OTA_HW_VERSION,
        .max_data_size = OTA_UPGRADE_MAX_DATA,
    };

    uint16_t server_addr = 0xFFFFU;
    uint8_t server_ep = 0xFFU;

    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(NULL);
    esp_zb_attribute_list_t *ota_cluster = esp_zb_ota_cluster_create(&ota_cfg);
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    if (basic == NULL || ota_cluster == NULL || cluster_list == NULL) {
        return ESP_ERR_NO_MEM;
    }

    static uint8_t manuf_name[] = {8, 'C', 'a', 'm', 'p', 'e', 'r', 'O', 'S'};
    static uint8_t model_id[] = {13, 'C', 'a', 'm', 'p', 'e', 'r', 'N', 'o', 'd', 'e', ' ', 'O', 'S'};

    ESP_RETURN_ON_ERROR(
        esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manuf_name),
        TAG, "basic manuf");
    ESP_RETURN_ON_ERROR(
        esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model_id),
        TAG, "basic model");
    ESP_RETURN_ON_ERROR(
        esp_zb_ota_cluster_add_attr(ota_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID, &client_var),
        TAG, "ota client data");
    ESP_RETURN_ON_ERROR(
        esp_zb_ota_cluster_add_attr(ota_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ADDR_ID, &server_addr),
        TAG, "ota server addr");
    ESP_RETURN_ON_ERROR(
        esp_zb_ota_cluster_add_attr(ota_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ENDPOINT_ID, &server_ep),
        TAG, "ota server ep");

    ESP_RETURN_ON_ERROR(
        esp_zb_cluster_list_add_basic_cluster(cluster_list, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE),
        TAG, "add basic");
    ESP_RETURN_ON_ERROR(
        esp_zb_cluster_list_add_ota_cluster(cluster_list, ota_cluster, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE),
        TAG, "add ota");

    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint = CAMPER_ZB_OTA_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_TEST_DEVICE_ID,
        .app_device_version = 0,
    };

    ESP_RETURN_ON_ERROR(esp_zb_ep_list_add_ep(ep_list, cluster_list, ep_cfg), TAG, "add ep");

    ESP_LOGI(TAG, "OTA client endpoint %u (mfg 0x%04x type 0x%04x ver 0x%08lx)",
             (unsigned)CAMPER_ZB_OTA_ENDPOINT,
             (unsigned)CAMPER_OTA_MANUFACTURER,
             (unsigned)CAMPER_OTA_IMAGE_TYPE,
             (unsigned long)CAMPER_OTA_FILE_VERSION);
    return ESP_OK;
}

esp_err_t zigbee_ota_request_update(ota_mgr_t *mgr)
{
    s_ota_mgr = mgr;

    if (!esp_zb_bdb_dev_joined()) {
        ESP_LOGW(TAG, "OTA query skipped — not joined");
        return ESP_ERR_INVALID_STATE;
    }

    esp_zb_zdo_match_desc_req_param_t req = {
        .addr_of_interest = 0x0000U,
        .dst_nwk_addr = 0x0000U,
        .profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .num_in_clusters = 1,
        .num_out_clusters = 0,
    };
    uint16_t cluster_list[] = {ESP_ZB_ZCL_CLUSTER_ID_OTA_UPGRADE};
    req.cluster_list = cluster_list;

    esp_zb_zdo_match_cluster(&req, zb_ota_match_desc_cb, NULL);
    ESP_LOGI(TAG, "searching OTA server on coordinator");
    return ESP_OK;
}
