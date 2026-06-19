/**

 * @file zigbee_gpio_io.c

 * @brief Per-pin I/O map via CamperNode cluster attributes.

 */



#include "zigbee_gpio_io.h"

#include "zigbee_device.h"

#include "profile_relay.h"

#include "zigbee_clusters.h"

#include "camper_features.h"

#include "camper_config.h"

#include "board_config.h"

#include "gpio_io.h"

#include "gpio_mgr.h"

#include "storage.h"

#include "event_types.h"



#include "esp_log.h"

#include "esp_zigbee_attribute.h"

#include "esp_zigbee_core.h"

#include "zcl/esp_zigbee_zcl_common.h"



#include <stdio.h>

#include <string.h>



#define TAG "ZB_IO"

#define ZB_PIN_MAP_BUF_SIZE  CAMPER_IO_PIN_MAP_MAX_LEN



static struct {

    event_bus_t *event_bus;

    storage_t   *storage;

    gpio_mgr_t  *gpio_mgr;

    uint8_t      pin_map[ZB_PIN_MAP_BUF_SIZE];

    uint16_t     output_state;

    uint16_t     input_state;

    uint8_t      temp_gpio;

    uint8_t      output_count;

    uint8_t      input_count;

    bool         ready;

} s_io;



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



static void zcl_char_string_get(const uint8_t *buf, size_t cap, char *out, size_t out_len)

{

    if (out == NULL || out_len == 0) {

        return;

    }

    out[0] = '\0';

    if (buf == NULL || cap < 1U) {

        return;

    }

    size_t len = buf[0];

    if (len >= cap) {

        len = cap - 1U;

    }

    if (len >= out_len) {

        len = out_len - 1U;

    }

    memcpy(out, buf + 1, len);

    out[len] = '\0';

}



static void zb_io_load_temp_gpio(void)
{
    s_io.temp_gpio = CAMPER_BOARD_TEMP_GPIO;
}



static void zb_io_publish_reboot(void)

{

    if (s_io.event_bus == NULL) {

        return;

    }

    event_t evt = {

        .type = EVT_SYSTEM_REBOOT,

        .source_id = 0x0500U,

        .gpio_id = EVENT_GPIO_NONE,

        .data.int_val = REBOOT_REASON_CONFIG_CHANGE,

    };

    event_bus_publish(s_io.event_bus, &evt);

}



/** Delay reboot so ZCL Write Attributes can complete before the device resets. */

static void zb_io_delayed_reboot_cb(uint8_t param)

{

    (void)param;

    zb_io_publish_reboot();

}



static void zb_io_schedule_reboot(void)

{

    esp_zb_scheduler_alarm((esp_zb_callback_t)zb_io_delayed_reboot_cb, 0, 2000);

}



static esp_err_t zb_io_save_pin_map(void)

{

    if (s_io.storage == NULL) {

        return ESP_OK;

    }



    char map[CAMPER_IO_PIN_MAP_MAX_LEN];

    zcl_char_string_get(s_io.pin_map, sizeof(s_io.pin_map), map, sizeof(map));

    return storage_set_string(s_io.storage, CAMPER_NVS_NS_SETTINGS,

                              CAMPER_KEY_PIN_MAP, map);

}



static esp_err_t zb_io_build_and_apply_gpio(void)

{

    char map[CAMPER_IO_PIN_MAP_MAX_LEN];

    zcl_char_string_get(s_io.pin_map, sizeof(s_io.pin_map), map, sizeof(map));



    uint8_t out_pins[CAMPER_IO_MAX_OUTPUTS];

    uint8_t in_pins[CAMPER_IO_MAX_INPUTS];

    size_t out_n = 0;

    size_t in_n = 0;



    if (map[0] != '\0' &&

        gpio_io_parse_pin_map(map, s_io.temp_gpio, out_pins, &out_n, in_pins, &in_n) < 0) {

        return ESP_ERR_INVALID_ARG;

    }



    gpio_pin_config_t cfg[CAMPER_BOARD_MAX_GPIO_PINS];

    size_t count = 0;

    esp_err_t err = gpio_io_build_config_from_map(map, s_io.temp_gpio, cfg, &count,

                                                  CAMPER_BOARD_MAX_GPIO_PINS);

    if (err != ESP_OK) {

        return err;

    }



    s_io.output_count = (uint8_t)out_n;

    s_io.input_count = 1U + (uint8_t)in_n;



    if (s_io.storage != NULL) {

        err = storage_save_gpio_config(s_io.storage, cfg, count);

        if (err != ESP_OK) {

            return err;

        }

    }



    if (s_io.gpio_mgr != NULL) {

        err = gpio_mgr_apply_config(s_io.gpio_mgr, cfg, count);

        if (err != ESP_OK) {

            return err;

        }

    }



    return ESP_OK;

}



static void zb_io_refresh_input_state(void)

{

    if (s_io.gpio_mgr == NULL) {

        return;

    }



    uint16_t state = 0;

    for (uint8_t i = 0; i < s_io.input_count && i < CAMPER_IO_MAX_INPUTS; i++) {

        bool active = false;

        uint8_t bind = (uint8_t)(CAMPER_IO_INPUT_BIND_BASE + i);

        if (gpio_mgr_read(s_io.gpio_mgr, bind, &active) == ESP_OK && active) {

            state |= (uint16_t)(1U << i);

        }

    }

    s_io.input_state = state;

}



static esp_err_t zb_io_report_attr_u16(uint16_t attr_id, uint16_t value)
{
    if (!esp_zb_lock_acquire(portMAX_DELAY)) {
        return ESP_FAIL;
    }

    esp_zb_zcl_status_t st = esp_zb_zcl_set_attribute_val(
        CAMPER_ZB_CONFIG_ENDPOINT,
        CAMPER_ZIGBEE_CLUSTER_ID,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        attr_id,
        (void *)&value,
        false);

    esp_zb_lock_release();
    return (st == ESP_ZB_ZCL_STATUS_SUCCESS) ? ESP_OK : ESP_FAIL;
}



static void zb_io_input_event_handler(const event_t *evt, void *arg)

{

    (void)arg;

    if (evt == NULL || !s_io.ready) {

        return;

    }

    if (evt->type != EVT_BUTTON_PRESSED && evt->type != EVT_BUTTON_RELEASED) {

        return;

    }

    if (evt->gpio_id < CAMPER_IO_INPUT_BIND_BASE) {

        return;

    }



    uint8_t idx = (uint8_t)(evt->gpio_id - CAMPER_IO_INPUT_BIND_BASE);

    if (idx >= CAMPER_IO_MAX_INPUTS) {

        return;

    }



    zb_io_refresh_input_state();

    if (zb_io_report_attr_u16(ZB_ATTR_INPUT_STATE, s_io.input_state) == ESP_OK) {

        ESP_LOGI(TAG, "input state 0x%02x (bind %u %s)",

                 s_io.input_state, evt->gpio_id,

                 evt->type == EVT_BUTTON_PRESSED ? "active" : "released");

    }

}



static void zb_io_sync_ep1_on_off(void)

{

    bool on = (s_io.output_state & 0x01U) != 0;

    zigbee_device_report_on_off(profile_relay_get_endpoint(), on);

}



static void zb_io_load_pin_map(void)

{

    char map[CAMPER_IO_PIN_MAP_MAX_LEN] = "";



    if (s_io.storage != NULL) {

        storage_get_string(s_io.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_PIN_MAP,

                           map, sizeof(map));



        if (map[0] == '\0') {

            char out_list[CAMPER_IO_PIN_MAP_MAX_LEN] = "";

            char in_list[CAMPER_IO_PIN_MAP_MAX_LEN] = "";

            storage_get_string(s_io.storage, CAMPER_NVS_NS_SETTINGS,

                               CAMPER_KEY_OUTPUT_PIN_LIST, out_list, sizeof(out_list));

            storage_get_string(s_io.storage, CAMPER_NVS_NS_SETTINGS,

                               CAMPER_KEY_INPUT_PIN_LIST, in_list, sizeof(in_list));

            if (out_list[0] != '\0' || in_list[0] != '\0') {

                gpio_io_migrate_lists_to_map(out_list, in_list, map, sizeof(map));

            } else {

                uint8_t legacy_out = 0;

                uint8_t legacy_btn = 0;

                storage_get_u8(s_io.storage, CAMPER_NVS_NS_SETTINGS,

                               CAMPER_KEY_OUTPUT_GPIO, &legacy_out);

                storage_get_u8(s_io.storage, CAMPER_NVS_NS_SETTINGS,

                               CAMPER_KEY_BUTTON_GPIO, &legacy_btn);

                if (legacy_out > 0 || legacy_btn > 0) {

                    gpio_io_migrate_legacy_to_map(legacy_out, legacy_btn, map, sizeof(map));

                }

            }

            if (map[0] != '\0') {

                storage_set_string(s_io.storage, CAMPER_NVS_NS_SETTINGS,

                                   CAMPER_KEY_PIN_MAP, map);

            }

        }



        uint32_t stored_state = 0;

        if (storage_get_u32(s_io.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_OUTPUT_STATE,
                            &stored_state) != ESP_OK) {
            uint8_t legacy_state = 0;
            if (storage_get_u8(s_io.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_OUTPUT_STATE,
                               &legacy_state) == ESP_OK) {
                stored_state = legacy_state;
            }
        }
        s_io.output_state = (uint16_t)stored_state;

    }



    zcl_char_string_set(s_io.pin_map, sizeof(s_io.pin_map), map);

}



void zigbee_gpio_io_init(const zigbee_camper_deps_t *deps)

{

    memset(&s_io, 0, sizeof(s_io));

    if (deps != NULL) {

        s_io.event_bus = deps->event_bus;

        s_io.storage = deps->storage;

        s_io.gpio_mgr = deps->gpio_mgr;

    }



    zb_io_load_temp_gpio();

    zb_io_load_pin_map();



    if (zb_io_build_and_apply_gpio() == ESP_OK) {

        zb_io_refresh_input_state();

        for (uint8_t i = 0; i < s_io.output_count && i < CAMPER_IO_MAX_OUTPUTS; i++) {

            bool on = (s_io.output_state & (1U << i)) != 0;

            if (s_io.gpio_mgr != NULL) {

                gpio_mgr_write(s_io.gpio_mgr, (uint8_t)(i + 1U), on);

            }

        }

    }



    if (s_io.event_bus != NULL) {

        event_bus_subscribe(s_io.event_bus, EVT_BUTTON_PRESSED, zb_io_input_event_handler, NULL);

        event_bus_subscribe(s_io.event_bus, EVT_BUTTON_RELEASED, zb_io_input_event_handler, NULL);

    }



    s_io.ready = true;

    ESP_LOGI(TAG, "IO ready: %u outputs, %u inputs (GPIO9 fixed input)",

             s_io.output_count, s_io.input_count);

}



void zigbee_gpio_io_notify_temp_gpio(uint8_t pin)

{

    s_io.temp_gpio = pin;

}



esp_err_t zigbee_gpio_io_handle_attr_write(uint16_t attr_id, const uint8_t *value, size_t value_len)

{

    switch (attr_id) {

    case ZB_ATTR_PIN_MAP: {

        if (value == NULL || value_len < 1U) {

            return ESP_ERR_INVALID_ARG;

        }

        if (value[0] >= ZB_PIN_MAP_BUF_SIZE) {

            return ESP_ERR_INVALID_SIZE;

        }

        if (value_len < 1U + value[0]) {

            return ESP_ERR_INVALID_SIZE;

        }



        char map[CAMPER_IO_PIN_MAP_MAX_LEN];

        zcl_char_string_get(value, value_len, map, sizeof(map));

        if (map[0] != '\0' &&

            gpio_io_parse_pin_map(map, s_io.temp_gpio, NULL, NULL, NULL, NULL) < 0) {

            return ESP_ERR_INVALID_ARG;

        }



        memcpy(s_io.pin_map, value, 1U + value[0]);

        s_io.pin_map[value[0] + 1U] = '\0';



        esp_err_t err = zb_io_save_pin_map();

        if (err != ESP_OK) {

            return err;

        }

        err = zb_io_build_and_apply_gpio();

        if (err != ESP_OK) {

            return err;

        }

        zb_io_schedule_reboot();

        return ESP_OK;

    }



    case ZB_ATTR_INPUT_PIN_LIST:

        return ESP_ERR_NOT_SUPPORTED;



    case ZB_ATTR_OUTPUT_STATE:

        if (value == NULL || value_len < 1U) {

            return ESP_ERR_INVALID_ARG;

        }

        if (value_len >= 2U) {
            s_io.output_state = (uint16_t)(value[0] | ((uint16_t)value[1] << 8));
        } else {
            s_io.output_state = value[0];
        }

        if (s_io.storage != NULL) {

            storage_set_u32(s_io.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_OUTPUT_STATE,
                            (uint32_t)s_io.output_state);

        }

        for (uint8_t i = 0; i < s_io.output_count && i < CAMPER_IO_MAX_OUTPUTS; i++) {

            bool on = (s_io.output_state & (1U << i)) != 0;

            if (s_io.gpio_mgr != NULL) {

                gpio_mgr_write(s_io.gpio_mgr, (uint8_t)(i + 1U), on);

            }

        }

        zb_io_sync_ep1_on_off();

        ESP_LOGI(TAG, "output state 0x%04x", s_io.output_state);

        return ESP_OK;



    default:

        return ESP_ERR_NOT_SUPPORTED;

    }

}



esp_err_t zigbee_gpio_io_apply_legacy_pins(uint8_t button_pin, uint8_t output_pin)

{

    char map[CAMPER_IO_PIN_MAP_MAX_LEN];

    zcl_char_string_get(s_io.pin_map, sizeof(s_io.pin_map), map, sizeof(map));



    if (output_pin > 0) {

        esp_err_t err = gpio_io_pin_map_upsert(map, sizeof(map), output_pin, true, s_io.temp_gpio);

        if (err != ESP_OK) {

            return err;

        }

    }

    if (button_pin > 0 && button_pin != CAMPER_BOARD_BOOT_BUTTON_GPIO) {

        esp_err_t err = gpio_io_pin_map_upsert(map, sizeof(map), button_pin, false, s_io.temp_gpio);

        if (err != ESP_OK) {

            return err;

        }

    }



    zcl_char_string_set(s_io.pin_map, sizeof(s_io.pin_map), map);



    if (s_io.storage != NULL) {

        storage_set_u8(s_io.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_BUTTON_GPIO, button_pin);

        storage_set_u8(s_io.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_OUTPUT_GPIO, output_pin);

    }



    esp_err_t err = zb_io_save_pin_map();

    if (err != ESP_OK) {

        return err;

    }

    err = zb_io_build_and_apply_gpio();

    if (err != ESP_OK) {

        return err;

    }

    zb_io_schedule_reboot();

    return ESP_OK;

}



esp_err_t zigbee_gpio_io_set_output_bit(uint8_t index, bool on)

{

    if (index >= CAMPER_IO_MAX_OUTPUTS) {

        return ESP_ERR_INVALID_ARG;

    }

    if (on) {

        s_io.output_state |= (uint16_t)(1U << index);

    } else {

        s_io.output_state &= (uint16_t)~(1U << index);

    }

    if (s_io.gpio_mgr != NULL && index < s_io.output_count) {

        gpio_mgr_write(s_io.gpio_mgr, (uint8_t)(index + 1U), on);

    }

    if (s_io.storage != NULL) {

        storage_set_u32(s_io.storage, CAMPER_NVS_NS_SETTINGS, CAMPER_KEY_OUTPUT_STATE,
                        (uint32_t)s_io.output_state);

    }

    zb_io_report_attr_u16(ZB_ATTR_OUTPUT_STATE, s_io.output_state);

    if (index == 0) {

        zb_io_sync_ep1_on_off();

    }

    return ESP_OK;

}



uint16_t zigbee_gpio_io_get_output_state(void)

{

    return s_io.output_state;

}



void zigbee_gpio_io_get_attr_ptrs(uint8_t **pin_map, uint16_t **output_state, uint16_t **input_state)

{

    if (pin_map != NULL) {

        *pin_map = s_io.pin_map;

    }

    if (output_state != NULL) {

        *output_state = &s_io.output_state;

    }

    if (input_state != NULL) {

        *input_state = &s_io.input_state;

    }

}

