/**
 * @file zigbee_camper_cluster.c
 * @brief Custom cluster 0xFB00 — remote config, diagnostics, system commands.
 */

#include "zigbee_camper_cluster.h"
#include "zigbee_clusters.h"
#include "zigbee_gpio_io.h"
#include "gpio_io.h"
#include "camper_config.h"
#include "camper_features.h"
#include "event_types.h"
#include "gpio_mgr.h"
#include "logger.h"
#include "ota_mgr.h"
#include "zigbee_ota.h"
#include "profile_interface.h"
#include "profile_mgr.h"
#include "storage.h"

#include "board_gpio.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_zigbee_attribute.h"
#include "esp_zigbee_cluster.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "zcl/esp_zigbee_zcl_common.h"

#include <string.h>

#define TAG              "ZB_CAMPER"
#define SOURCE_ID_ZIGBEE 0x0500U

#define ZB_GPIO_BLOB_RAW_MAX   (1U + (CAMPER_BOARD_MAX_GPIO_PINS * sizeof(gpio_pin_config_t)))
#define ZB_GPIO_ATTR_BUF_SIZE  (2U + ZB_GPIO_BLOB_RAW_MAX)
#define ZB_CALIB_RAW_MAX       128U
#define ZB_CALIB_ATTR_BUF_SIZE (2U + ZB_CALIB_RAW_MAX)
#define ZB_NAME_ATTR_BUF_SIZE  (1U + CAMPER_NODE_NAME_MAX_LEN)
#define ZB_FW_ATTR_BUF_SIZE    (1U + 16U)

static zigbee_camper_deps_t s_deps;
static bool s_initialized;

static uint8_t s_attr_node_name[ZB_NAME_ATTR_BUF_SIZE];
static uint8_t s_attr_gpio[ZB_GPIO_ATTR_BUF_SIZE];
static uint8_t s_attr_calib[ZB_CALIB_ATTR_BUF_SIZE];
static uint32_t s_attr_uptime;
static int8_t s_attr_rssi;
static uint8_t s_attr_log_level;
static uint8_t s_attr_firmware[ZB_FW_ATTR_BUF_SIZE];
static uint8_t s_attr_button_gpio;
static uint8_t s_attr_output_gpio;
static uint8_t s_attr_feature_flags;
static uint8_t s_attr_temp_gpio;
static uint8_t s_attr_dht_gpio;
static uint8_t s_attr_sensor_gpio6;
static uint8_t s_attr_sensor_gpio7;

static void zb_publish(event_type_t type, int32_t value)
{
    if (s_deps.event_bus == NULL) {
        return;
    }

    event_t evt = {
        .type = type,
        .source_id = SOURCE_ID_ZIGBEE,
        .gpio_id = EVENT_GPIO_NONE,
        .data.int_val = value,
    };
    event_bus_publish(s_deps.event_bus, &evt);
}

static void zcl_char_string_set(uint8_t *buf, size_t cap, const char *str)
{
    size_t len = str != NULL ? strlen(str) : 0;
    if (len > cap - 1U) {
        len = cap - 1U;
    }
    buf[0] = (uint8_t)len;
    if (len > 0) {
        memcpy(buf + 1, str, len);
    }
}

static void zcl_long_octet_set(uint8_t *buf, size_t cap, const uint8_t *data, size_t len)
{
    if (cap < 2U || len > cap - 2U) {
        return;
    }
    buf[0] = (uint8_t)(len & 0xFFU);
    buf[1] = (uint8_t)((len >> 8) & 0xFFU);
    if (len > 0 && data != NULL) {
        memcpy(buf + 2, data, len);
    }
}

static size_t zcl_long_octet_payload_len(const uint8_t *buf, size_t cap)
{
    if (cap < 2U) {
        return 0;
    }
    return (size_t)buf[0] | ((size_t)buf[1] << 8);
}

static void load_node_name_from_storage(void)
{
    char name[CAMPER_NODE_NAME_MAX_LEN] = "campernode";
    if (s_deps.storage != NULL) {
        storage_get_string(s_deps.storage, CAMPER_NVS_NS_SYSTEM, CAMPER_KEY_NODE_NAME,
                           name, sizeof(name));
    }
    zcl_char_string_set(s_attr_node_name, sizeof(s_attr_node_name), name);
}

static bool gpio_is_output_function(gpio_function_t fn)
{
    return fn == GPIO_FUNC_DIGITAL_OUTPUT || fn == GPIO_FUNC_RELAY ||
           fn == GPIO_FUNC_VALVE || fn == GPIO_FUNC_PUMP;
}

static esp_err_t validate_button_gpio_pin(uint8_t pin)
{
    if (pin == 0) {
        return ESP_OK;
    }
    if (pin >= CAMPER_BOARD_MAX_GPIO_PINS) {
        ESP_LOGW(TAG, "GPIO %u out of range (max %u)", pin,
                 (unsigned)(CAMPER_BOARD_MAX_GPIO_PINS - 1U));
        return ESP_ERR_INVALID_ARG;
    }
    if (!camper_board_gpio_allowed(pin, GPIO_FUNC_BUTTON)) {
        ESP_LOGW(TAG, "GPIO %u not allowed as button", pin);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t validate_output_gpio_pin(uint8_t pin)
{
    if (pin == 0) {
        return ESP_OK;
    }
    if (pin >= CAMPER_BOARD_MAX_GPIO_PINS) {
        ESP_LOGW(TAG, "GPIO %u out of range (max %u)", pin,
                 (unsigned)(CAMPER_BOARD_MAX_GPIO_PINS - 1U));
        return ESP_ERR_INVALID_ARG;
    }
    if (!camper_board_gpio_allowed(pin, GPIO_FUNC_RELAY)) {
        ESP_LOGW(TAG, "GPIO %u is a strapping pin (not allowed)", pin);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static void sync_legacy_pins_from_blob(const uint8_t *raw, size_t raw_len)
{
    s_attr_button_gpio = 0;
    s_attr_output_gpio = 0;

    if (raw == NULL || raw_len < 1U) {
        return;
    }

    uint8_t count = raw[0];
    if (count == 0 || raw_len < 1U + ((size_t)count * sizeof(gpio_pin_config_t))) {
        return;
    }

    const gpio_pin_config_t *cfg = (const gpio_pin_config_t *)(raw + 1);
    for (uint8_t i = 0; i < count; i++) {
        if (cfg[i].function == GPIO_FUNC_BUTTON &&
            cfg[i].profile_bind >= CAMPER_IO_INPUT_BIND_BASE &&
            cfg[i].pin != CAMPER_BOARD_BOOT_BUTTON_GPIO) {
            s_attr_button_gpio = cfg[i].pin;
        }
        if (cfg[i].profile_bind >= 1 && cfg[i].profile_bind <= CAMPER_IO_MAX_OUTPUTS &&
            gpio_is_output_function((gpio_function_t)cfg[i].function)) {
            s_attr_output_gpio = cfg[i].pin;
        }
    }
}

static void sync_legacy_pins_from_pin_map(void)
{
    char map[CAMPER_IO_PIN_MAP_MAX_LEN] = "";
    uint8_t *map_attr = NULL;
    zigbee_gpio_io_get_attr_ptrs(&map_attr, NULL, NULL);

    if (map_attr != NULL && map_attr[0] > 0) {
        size_t len = map_attr[0];
        if (len >= sizeof(map)) {
            len = sizeof(map) - 1U;
        }
        memcpy(map, map_attr + 1, len);
        map[len] = '\0';
    }

    uint8_t out_pins[CAMPER_IO_MAX_OUTPUTS];
    uint8_t in_pins[CAMPER_IO_MAX_INPUTS];
    size_t out_n = 0;
    size_t in_n = 0;
    uint8_t temp_gpio = s_attr_temp_gpio;

    if (map[0] != '\0') {
        gpio_io_parse_pin_map(map, temp_gpio, out_pins, &out_n, in_pins, &in_n, NULL);
    }

    s_attr_output_gpio = (out_n > 0) ? out_pins[0] : 0;
    s_attr_button_gpio = (in_n > 0) ? in_pins[0] : 0;
}

static void load_gpio_from_storage(void)
{
    uint8_t raw[ZB_GPIO_BLOB_RAW_MAX] = {0};
    size_t count = 0;

    if (s_deps.storage != NULL) {
        gpio_pin_config_t cfg[CAMPER_BOARD_MAX_GPIO_PINS];
        if (gpio_mgr_resolve_pin_config(s_deps.storage, cfg, &count) == ESP_OK && count > 0) {
            raw[0] = (uint8_t)count;
            memcpy(raw + 1, cfg, count * sizeof(gpio_pin_config_t));
            zcl_long_octet_set(s_attr_gpio, sizeof(s_attr_gpio), raw,
                               1U + (count * sizeof(gpio_pin_config_t)));
            sync_legacy_pins_from_blob(raw, 1U + (count * sizeof(gpio_pin_config_t)));
            return;
        }
    }

    zcl_long_octet_set(s_attr_gpio, sizeof(s_attr_gpio), raw, 1U);
}

static void load_calib_from_storage(void)
{
    uint8_t raw[ZB_CALIB_RAW_MAX] = {0};
    size_t len = sizeof(raw);

    if (s_deps.storage != NULL) {
        esp_err_t err = storage_get_blob(s_deps.storage, CAMPER_NVS_NS_CALIB, "default",
                                         raw, &len);
        if (err != ESP_OK) {
            len = 0;
        }
    } else {
        len = 0;
    }

    zcl_long_octet_set(s_attr_calib, sizeof(s_attr_calib), raw, len);
}

static void load_log_level_from_logger(void)
{
    if (s_deps.logger != NULL) {
        s_attr_log_level = (uint8_t)logger_get_level(s_deps.logger);
    } else {
        s_attr_log_level = LOG_LEVEL_INFO;
    }
}

static void load_firmware_version(void)
{
    zcl_char_string_set(s_attr_firmware, sizeof(s_attr_firmware), CAMPER_FIRMWARE_VERSION);
}

static esp_err_t apply_gpio_blob(const uint8_t *raw, size_t raw_len)
{
    if (raw == NULL || raw_len < 1U) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t count = raw[0];
    if (count > CAMPER_BOARD_MAX_GPIO_PINS) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t expected = 1U + ((size_t)count * sizeof(gpio_pin_config_t));
    if (raw_len < expected) {
        return ESP_ERR_INVALID_SIZE;
    }

    const gpio_pin_config_t *cfg = (const gpio_pin_config_t *)(raw + 1);
    if (s_deps.storage != NULL) {
        esp_err_t err = storage_save_gpio_config(s_deps.storage, cfg, count);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (s_deps.gpio_mgr != NULL) {
        esp_err_t err = gpio_mgr_apply_config(s_deps.gpio_mgr, cfg, count);
        if (err != ESP_OK) {
            return err;
        }
    }

    zcl_long_octet_set(s_attr_gpio, sizeof(s_attr_gpio), raw, expected);
    sync_legacy_pins_from_blob(raw, expected);
    if (s_deps.storage != NULL) {
        storage_set_u8(s_deps.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_BUTTON_GPIO,
                       s_attr_button_gpio);
        storage_set_u8(s_deps.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_OUTPUT_GPIO,
                       s_attr_output_gpio);
    }
    zb_publish(EVT_CONFIG_CHANGED, 0);
    return ESP_OK;
}

static esp_err_t validate_temp_gpio_pin(uint8_t pin)
{
    if (pin == 0) {
        return ESP_OK;
    }
    if (pin >= CAMPER_BOARD_MAX_GPIO_PINS) {
        ESP_LOGW(TAG, "GPIO %u out of range (max %u)", pin,
                 (unsigned)(CAMPER_BOARD_MAX_GPIO_PINS - 1U));
        return ESP_ERR_INVALID_ARG;
    }
    if (!camper_board_gpio_allowed(pin, GPIO_FUNC_DS18B20)) {
        ESP_LOGW(TAG, "GPIO %u not allowed for temperature sensor", pin);
        return ESP_ERR_INVALID_ARG;
    }
    if (gpio_io_is_pin_reserved(pin, 0)) {
        ESP_LOGW(TAG, "GPIO %u reserved (LED/BOOT)", pin);
        return ESP_ERR_INVALID_ARG;
    }

    char map[CAMPER_IO_PIN_MAP_MAX_LEN] = "";
    uint8_t *map_attr = NULL;
    zigbee_gpio_io_get_attr_ptrs(&map_attr, NULL, NULL);
    if (map_attr != NULL && map_attr[0] > 0) {
        size_t len = map_attr[0];
        if (len >= sizeof(map)) {
            len = sizeof(map) - 1U;
        }
        memcpy(map, map_attr + 1, len);
        map[len] = '\0';
    }
    if (map[0] != '\0') {
        uint8_t out_pins[CAMPER_IO_MAX_OUTPUTS];
        uint8_t in_pins[CAMPER_IO_MAX_INPUTS];
        size_t out_n = 0;
        size_t in_n = 0;
        if (gpio_io_parse_pin_map(map, pin, out_pins, &out_n, in_pins, &in_n, NULL) < 0) {
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t i = 0; i < out_n; i++) {
            if (out_pins[i] == pin) {
                ESP_LOGW(TAG, "temp GPIO %u already used as output", pin);
                return ESP_ERR_INVALID_ARG;
            }
        }
        for (size_t i = 0; i < in_n; i++) {
            if (in_pins[i] == pin) {
                ESP_LOGW(TAG, "temp GPIO %u already used as input", pin);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    return ESP_OK;
}

static esp_err_t apply_node_name(const uint8_t *zcl_str, size_t zcl_len)
{
    if (zcl_str == NULL || zcl_len < 1U) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t name_len = zcl_str[0];
    if (name_len == 0 || (size_t)name_len + 1U > zcl_len) {
        return ESP_ERR_INVALID_ARG;
    }
    if (name_len >= CAMPER_NODE_NAME_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    char name[CAMPER_NODE_NAME_MAX_LEN];
    memcpy(name, zcl_str + 1, name_len);
    name[name_len] = '\0';

    if (s_deps.storage != NULL) {
        esp_err_t err = storage_set_string(s_deps.storage, CAMPER_NVS_NS_SYSTEM,
                                           CAMPER_KEY_NODE_NAME, name);
        if (err != ESP_OK) {
            return err;
        }
    }

    memcpy(s_attr_node_name, zcl_str, (size_t)name_len + 1U);
    zb_publish(EVT_CONFIG_CHANGED, 0);
    return ESP_OK;
}

static esp_err_t apply_calib_blob(const uint8_t *payload, size_t payload_len)
{
    if (payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (payload_len > ZB_CALIB_RAW_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (s_deps.storage != NULL) {
        esp_err_t err = storage_set_blob(s_deps.storage, CAMPER_NVS_NS_CALIB, "default",
                                         payload, payload_len);
        if (err != ESP_OK) {
            return err;
        }
    }

    zcl_long_octet_set(s_attr_calib, sizeof(s_attr_calib), payload, payload_len);
    zb_publish(EVT_CONFIG_CHANGED, 0);
    return ESP_OK;
}

static esp_err_t validate_sensor_mode(uint8_t mode)
{
    return mode <= CAMPER_SENSOR_DHT22 ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static esp_err_t validate_sensor_pin_pair(uint8_t gpio6, uint8_t gpio7)
{
    esp_err_t err = validate_sensor_mode(gpio6);
    if (err != ESP_OK) {
        return err;
    }
    err = validate_sensor_mode(gpio7);
    if (err != ESP_OK) {
        return err;
    }
    if (gpio6 != CAMPER_SENSOR_NONE && gpio6 == gpio7) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static void sync_active_sensor_gpios(void)
{
    if (s_deps.profile_mgr != NULL) {
        s_attr_temp_gpio = profile_mgr_get_temp_gpio(s_deps.profile_mgr);
        s_attr_dht_gpio = profile_mgr_get_dht_gpio(s_deps.profile_mgr);
        return;
    }

    s_attr_temp_gpio = 0;
    s_attr_dht_gpio = 0;
    if (s_attr_sensor_gpio6 == CAMPER_SENSOR_DS18B20) {
        s_attr_temp_gpio = CAMPER_BOARD_SENSOR_GPIO_6;
    } else if (s_attr_sensor_gpio7 == CAMPER_SENSOR_DS18B20) {
        s_attr_temp_gpio = CAMPER_BOARD_SENSOR_GPIO_7;
    }
    if (s_attr_sensor_gpio6 == CAMPER_SENSOR_DHT22) {
        s_attr_dht_gpio = CAMPER_BOARD_SENSOR_GPIO_6;
    } else if (s_attr_sensor_gpio7 == CAMPER_SENSOR_DHT22) {
        s_attr_dht_gpio = CAMPER_BOARD_SENSOR_GPIO_7;
    }
}

static uint8_t flags_from_sensor_pins(uint8_t gpio6, uint8_t gpio7)
{
    uint8_t flags = 0;

    if (gpio6 == CAMPER_SENSOR_DS18B20 || gpio7 == CAMPER_SENSOR_DS18B20) {
        flags |= CAMPER_FEATURE_TEMPERATURE;
    }
    if (gpio6 == CAMPER_SENSOR_DHT22 || gpio7 == CAMPER_SENSOR_DHT22) {
        flags |= CAMPER_FEATURE_DHT22;
    }
    return flags;
}

static void legacy_flags_to_sensor_pins(uint8_t flags, uint8_t *gpio6, uint8_t *gpio7)
{
    *gpio6 = CAMPER_SENSOR_NONE;
    *gpio7 = CAMPER_SENSOR_NONE;

    if ((flags & CAMPER_FEATURE_TEMPERATURE) != 0) {
        *gpio6 = CAMPER_SENSOR_DS18B20;
    }
    if ((flags & CAMPER_FEATURE_DHT22) != 0) {
        if (*gpio6 == CAMPER_SENSOR_NONE) {
            *gpio6 = CAMPER_SENSOR_DHT22;
        } else {
            *gpio7 = CAMPER_SENSOR_DHT22;
        }
    }
}

static void load_feature_settings(void)
{
    s_attr_feature_flags = 0;
    s_attr_sensor_gpio6 = CAMPER_SENSOR_NONE;
    s_attr_sensor_gpio7 = CAMPER_SENSOR_NONE;
    s_attr_temp_gpio = 0;
    s_attr_dht_gpio = 0;

    if (s_deps.storage != NULL) {
        storage_get_u8(s_deps.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_FEATURE_FLAGS,
                       &s_attr_feature_flags);
        storage_get_u8(s_deps.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_SENSOR_GPIO6,
                       &s_attr_sensor_gpio6);
        storage_get_u8(s_deps.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_SENSOR_GPIO7,
                       &s_attr_sensor_gpio7);

        if (s_attr_sensor_gpio6 == CAMPER_SENSOR_NONE &&
            s_attr_sensor_gpio7 == CAMPER_SENSOR_NONE) {
            uint8_t legacy_type = CAMPER_SENSOR_NONE;
            storage_get_u8(s_deps.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_SENSOR_TYPE,
                           &legacy_type);
            if (legacy_type != CAMPER_SENSOR_NONE) {
                s_attr_sensor_gpio6 = legacy_type;
            } else {
                legacy_flags_to_sensor_pins(s_attr_feature_flags,
                                            &s_attr_sensor_gpio6, &s_attr_sensor_gpio7);
            }
        }
    }

    s_attr_feature_flags = flags_from_sensor_pins(s_attr_sensor_gpio6, s_attr_sensor_gpio7);
    sync_active_sensor_gpios();
}

static esp_err_t apply_sensor_pins(uint8_t gpio6, uint8_t gpio7)
{
    esp_err_t err = validate_sensor_pin_pair(gpio6, gpio7);
    if (err != ESP_OK) {
        return err;
    }

    s_attr_sensor_gpio6 = gpio6;
    s_attr_sensor_gpio7 = gpio7;
    s_attr_feature_flags = flags_from_sensor_pins(gpio6, gpio7);

    if (s_deps.storage != NULL) {
        err = storage_set_u8(s_deps.storage, CAMPER_NVS_NS_SETTINGS,
                             CAMPER_KEY_SENSOR_GPIO6, gpio6);
        if (err != ESP_OK) {
            return err;
        }
        err = storage_set_u8(s_deps.storage, CAMPER_NVS_NS_SETTINGS,
                             CAMPER_KEY_SENSOR_GPIO7, gpio7);
        if (err != ESP_OK) {
            return err;
        }
        err = storage_set_u8(s_deps.storage, CAMPER_NVS_NS_SETTINGS,
                             CAMPER_KEY_FEATURE_FLAGS, s_attr_feature_flags);
        if (err != ESP_OK) {
            return err;
        }
        err = storage_set_u8(s_deps.storage, CAMPER_NVS_NS_SETTINGS,
                             CAMPER_KEY_SENSOR_TYPE, gpio6);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (s_deps.profile_mgr != NULL) {
        err = profile_mgr_apply_sensor_pins(s_deps.profile_mgr, gpio6, gpio7);
        if (err != ESP_OK) {
            return err;
        }
    }

    sync_active_sensor_gpios();
    zb_publish(EVT_CONFIG_CHANGED, 0);
    return ESP_OK;
}

static esp_err_t apply_sensor_gpio6(uint8_t mode)
{
    return apply_sensor_pins(mode, s_attr_sensor_gpio7);
}

static esp_err_t apply_sensor_gpio7(uint8_t mode)
{
    return apply_sensor_pins(s_attr_sensor_gpio6, mode);
}

static esp_err_t apply_feature_flags(uint8_t flags)
{
    uint8_t gpio6 = CAMPER_SENSOR_NONE;
    uint8_t gpio7 = CAMPER_SENSOR_NONE;
    legacy_flags_to_sensor_pins(flags, &gpio6, &gpio7);
    return apply_sensor_pins(gpio6, gpio7);
}

static esp_err_t apply_temp_gpio(uint8_t pin)
{
    (void)pin;
    s_attr_temp_gpio = CAMPER_BOARD_SENSOR_GPIO;

    if (s_deps.profile_mgr != NULL) {
        esp_err_t err = profile_mgr_apply_temp_gpio(s_deps.profile_mgr, CAMPER_BOARD_SENSOR_GPIO);
        if (err != ESP_OK) {
            return err;
        }
    }

    zigbee_gpio_io_notify_temp_gpio(CAMPER_BOARD_SENSOR_GPIO);
    zb_publish(EVT_CONFIG_CHANGED, 0);
    return ESP_OK;
}

static esp_err_t apply_log_level(uint8_t level)
{
    if (level >= LOG_LEVEL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_deps.logger != NULL) {
        logger_set_level(s_deps.logger, (camper_log_level_t)level);
    }

    s_attr_log_level = level;
    return ESP_OK;
}

static esp_err_t handle_attr_write(uint16_t attr_id, const uint8_t *value, size_t value_len)
{
    switch (attr_id) {
    case ZB_ATTR_NODE_NAME:
        return apply_node_name(value, value_len);

    case ZB_ATTR_GPIO_CONFIG: {
        size_t raw_len = zcl_long_octet_payload_len(value, value_len);
        if (raw_len == 0 || value_len < 2U + raw_len) {
            return ESP_ERR_INVALID_ARG;
        }
        return apply_gpio_blob(value + 2, raw_len);
    }

    case ZB_ATTR_CALIBRATION: {
        size_t raw_len = zcl_long_octet_payload_len(value, value_len);
        if (value_len < 2U + raw_len) {
            return ESP_ERR_INVALID_ARG;
        }
        return apply_calib_blob(value + 2, raw_len);
    }

    case ZB_ATTR_LOG_LEVEL:
        if (value == NULL || value_len < 1U) {
            return ESP_ERR_INVALID_ARG;
        }
        return apply_log_level(value[0]);

    case ZB_ATTR_BUTTON_GPIO:
        if (value == NULL || value_len < 1U) {
            return ESP_ERR_INVALID_ARG;
        }
        s_attr_button_gpio = value[0];
        if (validate_button_gpio_pin(s_attr_button_gpio) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }
        return zigbee_gpio_io_apply_legacy_pins(s_attr_button_gpio, s_attr_output_gpio);

    case ZB_ATTR_OUTPUT_GPIO:
        if (value == NULL || value_len < 1U) {
            return ESP_ERR_INVALID_ARG;
        }
        s_attr_output_gpio = value[0];
        if (validate_output_gpio_pin(s_attr_output_gpio) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }
        return zigbee_gpio_io_apply_legacy_pins(s_attr_button_gpio, s_attr_output_gpio);

    case ZB_ATTR_PIN_MAP:
    case ZB_ATTR_OUTPUT_STATE:
        return zigbee_gpio_io_handle_attr_write(attr_id, value, value_len);

    case ZB_ATTR_INPUT_STATE:
        return ESP_ERR_NOT_SUPPORTED;

    case ZB_ATTR_FEATURE_FLAGS:
        if (value == NULL || value_len < 1U) {
            return ESP_ERR_INVALID_ARG;
        }
        return apply_feature_flags(value[0]);

    case ZB_ATTR_SENSOR_GPIO6:
        if (value == NULL || value_len < 1U) {
            return ESP_ERR_INVALID_ARG;
        }
        if (value[0] > CAMPER_SENSOR_DHT22) {
            return ESP_ERR_INVALID_ARG;
        }
        return apply_sensor_gpio6(value[0]);

    case ZB_ATTR_SENSOR_GPIO7:
        if (value == NULL || value_len < 1U) {
            return ESP_ERR_INVALID_ARG;
        }
        if (value[0] > CAMPER_SENSOR_DHT22) {
            return ESP_ERR_INVALID_ARG;
        }
        return apply_sensor_gpio7(value[0]);

    case ZB_ATTR_TEMP_GPIO:
    case ZB_ATTR_DHT_GPIO:
        return ESP_ERR_NOT_SUPPORTED;

    case ZB_ATTR_UPTIME_SEC:
    case ZB_ATTR_LAST_RSSI:
    case ZB_ATTR_FIRMWARE_VERSION:
        return ESP_ERR_NOT_SUPPORTED;

    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

static esp_zb_attribute_list_t *camper_create_custom_attr_list(void)
{
    esp_zb_attribute_list_t *attr_list = esp_zb_zcl_attr_list_create(CAMPER_ZIGBEE_CLUSTER_ID);
    if (attr_list == NULL) {
        return NULL;
    }

    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_NODE_NAME,
                                          ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, s_attr_node_name);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_GPIO_CONFIG,
                                          ESP_ZB_ZCL_ATTR_TYPE_LONG_OCTET_STRING,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, s_attr_gpio);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_CALIBRATION,
                                          ESP_ZB_ZCL_ATTR_TYPE_LONG_OCTET_STRING,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, s_attr_calib);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_UPTIME_SEC,
                                          ESP_ZB_ZCL_ATTR_TYPE_U32,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &s_attr_uptime);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_LAST_RSSI,
                                          ESP_ZB_ZCL_ATTR_TYPE_S8,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &s_attr_rssi);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_LOG_LEVEL,
                                          ESP_ZB_ZCL_ATTR_TYPE_U8,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &s_attr_log_level);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_FIRMWARE_VERSION,
                                          ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, s_attr_firmware);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_BUTTON_GPIO,
                                          ESP_ZB_ZCL_ATTR_TYPE_U8,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &s_attr_button_gpio);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_OUTPUT_GPIO,
                                          ESP_ZB_ZCL_ATTR_TYPE_U8,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &s_attr_output_gpio);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_FEATURE_FLAGS,
                                          ESP_ZB_ZCL_ATTR_TYPE_U8,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &s_attr_feature_flags);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_TEMP_GPIO,
                                          ESP_ZB_ZCL_ATTR_TYPE_U8,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &s_attr_temp_gpio);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_SENSOR_GPIO6,
                                          ESP_ZB_ZCL_ATTR_TYPE_U8,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &s_attr_sensor_gpio6);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_SENSOR_GPIO7,
                                          ESP_ZB_ZCL_ATTR_TYPE_U8,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &s_attr_sensor_gpio7);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_DHT_GPIO,
                                          ESP_ZB_ZCL_ATTR_TYPE_U8,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &s_attr_dht_gpio);

    uint8_t *pin_map = NULL;
    uint16_t *output_state = NULL;
    uint16_t *input_state = NULL;
    zigbee_gpio_io_get_attr_ptrs(&pin_map, &output_state, &input_state);

    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_PIN_MAP,
                                          ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, pin_map);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_OUTPUT_STATE,
                                          ESP_ZB_ZCL_ATTR_TYPE_U16,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, output_state);
    esp_zb_custom_cluster_add_custom_attr(attr_list, ZB_ATTR_INPUT_STATE,
                                          ESP_ZB_ZCL_ATTR_TYPE_U16,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY |
                                              ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
                                          input_state);

    return attr_list;
}

static esp_zb_cluster_list_t *camper_create_config_cluster_list(void)
{
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    if (cluster_list == NULL) {
        return NULL;
    }

    static uint8_t manuf_name[] = {8, 'C', 'a', 'm', 'p', 'e', 'r', 'O', 'S'};
    static uint8_t model_id[] = {13, 'C', 'a', 'm', 'p', 'e', 'r', 'N', 'o', 'd', 'e', ' ', 'O', 'S'};

    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);
    if (basic_cluster != NULL) {
        esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                      manuf_name);
        esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                      model_id);
        esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster,
                                              ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    }

    esp_zb_attribute_list_t *custom_attrs = camper_create_custom_attr_list();
    if (custom_attrs != NULL) {
        esp_zb_cluster_list_add_custom_cluster(cluster_list, custom_attrs,
                                               ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    }

    return cluster_list;
}

static void diagnostics_timer_cb(uint8_t param)
{
    (void)param;
    zigbee_camper_cluster_refresh_diagnostics();
    esp_zb_scheduler_alarm((esp_zb_callback_t)diagnostics_timer_cb, 0, 30000);
}

esp_err_t zigbee_camper_cluster_init(const zigbee_camper_deps_t *deps)
{
    if (deps == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_deps = *deps;
    s_initialized = true;

    load_node_name_from_storage();
    load_gpio_from_storage();
    load_calib_from_storage();
    load_log_level_from_logger();
    load_firmware_version();
    load_feature_settings();
    zigbee_gpio_io_init(&s_deps);
    sync_legacy_pins_from_pin_map();
    zigbee_camper_cluster_refresh_diagnostics();

    ESP_LOGI(TAG, "camper cluster initialized (endpoint %u)", CAMPER_ZB_CONFIG_ENDPOINT);
    return ESP_OK;
}

esp_err_t zigbee_camper_cluster_add_endpoint(esp_zb_ep_list_t *ep_list)
{
    if (ep_list == NULL || !s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_zb_cluster_list_t *cluster_list = camper_create_config_cluster_list();
    if (cluster_list == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint = CAMPER_ZB_CONFIG_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_CONFIGURATION_TOOL_DEVICE_ID,
        .app_device_version = 0,
    };

    esp_err_t err = esp_zb_ep_list_add_ep(ep_list, cluster_list, ep_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "registered config endpoint %u (cluster 0x%04x)",
                 CAMPER_ZB_CONFIG_ENDPOINT, CAMPER_ZIGBEE_CLUSTER_ID);
    }
    return err;
}

void zigbee_camper_cluster_refresh_diagnostics(void)
{
    s_attr_uptime = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    s_attr_rssi = 0;
    load_firmware_version();
}

void zigbee_camper_cluster_start_diagnostics_timer(void)
{
    esp_zb_scheduler_alarm((esp_zb_callback_t)diagnostics_timer_cb, 0, 30000);
}

esp_err_t zigbee_camper_cluster_handle_custom_cmd(const esp_zb_zcl_custom_cluster_command_message_t *msg)
{
    if (msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (msg->info.cluster != CAMPER_ZIGBEE_CLUSTER_ID ||
        msg->info.dst_endpoint != CAMPER_ZB_CONFIG_ENDPOINT) {
        return ESP_OK;
    }

    uint8_t cmd = msg->info.command.id;
    ESP_LOGI(TAG, "custom cmd 0x%02x on endpoint %u", cmd, msg->info.dst_endpoint);

    switch (cmd) {
    case ZB_CMD_REBOOT:
        zb_publish(EVT_SYSTEM_REBOOT, REBOOT_REASON_USER);
        break;

    case ZB_CMD_FACTORY_RESET:
        zb_publish(EVT_FACTORY_RESET, 0);
        break;

    case ZB_CMD_TRIGGER_OTA:
        if (s_deps.ota_mgr != NULL) {
            ota_mgr_request_zigbee(s_deps.ota_mgr);
        }
        zigbee_ota_request_update(s_deps.ota_mgr);
        break;

    default:
        ESP_LOGW(TAG, "unknown custom cmd 0x%02x", cmd);
        break;
    }

    return ESP_OK;
}

esp_err_t zigbee_camper_cluster_handle_set_attr(const esp_zb_zcl_set_attr_value_message_t *msg)
{
    if (msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (msg->info.cluster != CAMPER_ZIGBEE_CLUSTER_ID ||
        msg->info.dst_endpoint != CAMPER_ZB_CONFIG_ENDPOINT) {
        return ESP_OK;
    }

    const uint8_t *value = (const uint8_t *)msg->attribute.data.value;
    size_t value_len = 0;

    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (msg->attribute.data.type) {
    case ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING:
        value_len = (size_t)value[0] + 1U;
        break;
    case ESP_ZB_ZCL_ATTR_TYPE_U8:
    case ESP_ZB_ZCL_ATTR_TYPE_S8:
        value_len = 1U;
        break;
    case ESP_ZB_ZCL_ATTR_TYPE_U32:
        value_len = 4U;
        break;
    case ESP_ZB_ZCL_ATTR_TYPE_U16:
        value_len = 2U;
        break;
    case ESP_ZB_ZCL_ATTR_TYPE_LONG_OCTET_STRING:
        value_len = 2U + zcl_long_octet_payload_len(value, ZB_GPIO_ATTR_BUF_SIZE);
        break;
    default:
        value_len = 1U;
        break;
    }

    esp_err_t err = handle_attr_write(msg->attribute.id, value, value_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set attr 0x%04x failed: %s (type %d len %u val %u)",
                 msg->attribute.id, esp_err_to_name(err),
                 (int)msg->attribute.data.type, (unsigned)value_len,
                 value_len > 0 ? (unsigned)value[0] : 0U);
        return err;
    }

    return ESP_OK;
}
