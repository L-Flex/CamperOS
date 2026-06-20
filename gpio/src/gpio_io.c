/**

 * @file gpio_io.c

 * @brief Per-pin I/O map parsing and GPIO blob builder.

 */



#include "gpio_io.h"

#include "board_config.h"

#include "board_gpio.h"

#include "camper_config.h"

#include "sdkconfig.h"



#include <ctype.h>

#include <stdio.h>

#include <stdlib.h>

#include <string.h>



static bool gpio_io_contains_pin(const uint8_t *pins, size_t count, uint8_t pin)

{

    for (size_t i = 0; i < count; i++) {

        if (pins[i] == pin) {

            return true;

        }

    }

    return false;

}



static bool gpio_io_is_console_uart_pin(uint8_t pin)
{
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    (void)pin;
    return false;
#elif defined(CONFIG_ESP_CONSOLE_NONE) && CONFIG_ESP_CONSOLE_NONE
    (void)pin;
    return false;
#else
    return pin == CAMPER_BOARD_CONSOLE_TX_GPIO || pin == CAMPER_BOARD_CONSOLE_RX_GPIO;
#endif
}

bool gpio_io_is_pin_reserved(uint8_t pin, uint8_t temp_gpio)

{

    if (pin == 0 || pin >= CAMPER_BOARD_MAX_GPIO_PINS) {

        return true;

    }

    if (pin == CAMPER_BOARD_BOOT_BUTTON_GPIO) {

        return true;

    }

#if CAMPER_BOARD_STATUS_LED_GPIO < CAMPER_BOARD_MAX_GPIO_PINS

    if (pin == CAMPER_BOARD_STATUS_LED_GPIO) {

        return true;

    }

#endif

#if CAMPER_BOARD_SENSOR_GPIO_6 < CAMPER_BOARD_MAX_GPIO_PINS

    if (pin == CAMPER_BOARD_SENSOR_GPIO_6) {

        return true;

    }

#endif

#if CAMPER_BOARD_SENSOR_GPIO_7 < CAMPER_BOARD_MAX_GPIO_PINS

    if (pin == CAMPER_BOARD_SENSOR_GPIO_7) {

        return true;

    }

#endif

    if (gpio_io_is_console_uart_pin(pin)) {

        return true;

    }

    (void)temp_gpio;

    return false;

}



static bool gpio_io_role_allowed(uint8_t pin, bool is_output)

{

    return is_output ?

           camper_board_gpio_allowed(pin, GPIO_FUNC_RELAY) :

           camper_board_gpio_allowed(pin, GPIO_FUNC_BUTTON);

}



int gpio_io_parse_pin_map(const char *map, uint8_t temp_gpio,

                          uint8_t *out_pins, size_t *out_count,

                          uint8_t *in_pins, size_t *in_count,

                          uint8_t *out_flags)

{

    uint8_t out_buf[CAMPER_IO_MAX_OUTPUTS];

    uint8_t in_buf[CAMPER_IO_MAX_INPUTS];

    uint8_t flag_buf[CAMPER_IO_MAX_OUTPUTS];

    uint8_t *outs = (out_pins != NULL) ? out_pins : out_buf;

    uint8_t *ins = (in_pins != NULL) ? in_pins : in_buf;

    uint8_t *flags = (out_flags != NULL) ? out_flags : flag_buf;

    size_t out_local = 0;

    size_t in_local = 0;

    size_t *out_n = (out_count != NULL) ? out_count : &out_local;

    size_t *in_n = (in_count != NULL) ? in_count : &in_local;



    *out_n = 0;

    *in_n = 0;



    if (map == NULL || map[0] == '\0') {

        return 0;

    }



    const char *p = map;

    while (*p != '\0') {

        while (*p == ' ' || *p == ',') {

            p++;

        }

        if (*p == '\0') {

            break;

        }



        char *end = NULL;

        long val = strtol(p, &end, 10);

        if (end == p || val < 0 || val >= CAMPER_BOARD_MAX_GPIO_PINS) {

            return -1;

        }

        if (*end != ':') {

            return -1;

        }



        const char *role = end + 1;

        bool is_output = false;

        uint8_t pin_flags = GPIO_FLAG_NONE;



        if (role[0] == 'O' || role[0] == 'o') {

            is_output = true;

#if CAMPER_FEATURE_MOSFET_OD
            if ((role[1] == 'd' || role[1] == 'D' || role[1] == 'm' || role[1] == 'M') &&

                (role[2] == '\0' || role[2] == ',' || role[2] == ' ')) {

                pin_flags = GPIO_FLAG_OPEN_DRAIN;

                p = end + 3;

            } else
#endif
            if (role[1] == '\0' || role[1] == ',' || role[1] == ' ') {

                p = end + 2;

            } else {

                return -1;

            }

        } else if (role[0] == 'I' || role[0] == 'i') {

            if (role[1] != '\0' && role[1] != ',' && role[1] != ' ') {

                return -1;

            }

            p = end + 2;

        } else {

            return -1;

        }



        uint8_t pin = (uint8_t)val;

        if (gpio_io_is_pin_reserved(pin, temp_gpio)) {

            return -1;

        }



        if (!gpio_io_role_allowed(pin, is_output)) {

            return -1;

        }



        uint8_t *dest = is_output ? outs : ins;

        size_t *n = is_output ? out_n : in_n;

        size_t max = is_output ? CAMPER_IO_MAX_OUTPUTS : CAMPER_IO_MAX_INPUTS;



        if (*n >= max) {

            return -1;

        }

        if (gpio_io_contains_pin(outs, *out_n, pin) ||

            gpio_io_contains_pin(ins, *in_n, pin)) {

            return -1;

        }



        dest[(*n)++] = pin;

        if (is_output) {

            flags[*out_n - 1U] = pin_flags;

        }

    }



    return (int)(*out_n + *in_n);

}



static esp_err_t gpio_io_build_config(const uint8_t *output_pins, size_t output_count,

                                      const uint8_t *input_pins, size_t input_count,

                                      const uint8_t *output_flags,

                                      gpio_pin_config_t *cfg, size_t *count, size_t cfg_max)

{

    if (cfg == NULL || count == NULL) {

        return ESP_ERR_INVALID_ARG;

    }



    if (output_count > CAMPER_IO_MAX_OUTPUTS || input_count > CAMPER_IO_MAX_INPUTS) {

        return ESP_ERR_INVALID_SIZE;

    }



    size_t n = 0;



    for (size_t i = 0; i < output_count; i++) {

        uint8_t pin = output_pins[i];

        if (pin == 0 || !gpio_io_role_allowed(pin, true)) {

            return ESP_ERR_INVALID_ARG;

        }

        if (n >= cfg_max) {

            return ESP_ERR_NO_MEM;

        }

        cfg[n++] = (gpio_pin_config_t){

            .pin = pin,

            .function = GPIO_FUNC_RELAY,

            .flags = (output_flags != NULL) ? output_flags[i] : GPIO_FLAG_NONE,

            .profile_bind = (uint8_t)(i + 1U),

        };

    }



    uint8_t inputs[CAMPER_IO_MAX_INPUTS];

    size_t in_count = 0;



    inputs[in_count++] = CAMPER_BOARD_BOOT_BUTTON_GPIO;

    for (size_t i = 0; i < input_count && in_count < CAMPER_IO_MAX_INPUTS; i++) {

        uint8_t pin = input_pins[i];

        if (pin == CAMPER_BOARD_BOOT_BUTTON_GPIO) {

            continue;

        }

        if (pin == 0 || !gpio_io_role_allowed(pin, false)) {

            return ESP_ERR_INVALID_ARG;

        }

        if (gpio_io_contains_pin(inputs, in_count, pin)) {

            continue;

        }

        inputs[in_count++] = pin;

    }



    for (size_t i = 0; i < in_count; i++) {

        if (n >= cfg_max) {

            return ESP_ERR_NO_MEM;

        }

        cfg[n++] = (gpio_pin_config_t){

            .pin = inputs[i],

            .function = GPIO_FUNC_BUTTON,

            .flags = GPIO_FLAG_PULLUP,

            .profile_bind = (uint8_t)(CAMPER_IO_INPUT_BIND_BASE + i),

            .debounce_ms = 50,

        };

    }



    *count = n;

    return ESP_OK;

}



esp_err_t gpio_io_build_config_from_map(const char *map, uint8_t temp_gpio,

                                        gpio_pin_config_t *cfg, size_t *count,

                                        size_t cfg_max)

{

    uint8_t out_pins[CAMPER_IO_MAX_OUTPUTS];

    uint8_t out_flags[CAMPER_IO_MAX_OUTPUTS];

    uint8_t in_pins[CAMPER_IO_MAX_INPUTS];

    size_t out_n = 0;

    size_t in_n = 0;



    if (gpio_io_parse_pin_map(map, temp_gpio, out_pins, &out_n, in_pins, &in_n, out_flags) < 0) {

        return ESP_ERR_INVALID_ARG;

    }



    return gpio_io_build_config(out_pins, out_n, in_pins, in_n, out_flags, cfg, count, cfg_max);

}



void gpio_io_format_pin_map(const uint8_t *out_pins, size_t out_n,

                            const uint8_t *in_pins, size_t in_n,

                            char *map, size_t map_len)

{

    if (map == NULL || map_len == 0) {

        return;

    }



    map[0] = '\0';

    size_t pos = 0;



    for (size_t i = 0; i < out_n; i++) {

        int written = snprintf(map + pos, map_len - pos, "%s%u:O", (pos > 0) ? "," : "", out_pins[i]);

        if (written < 0 || (size_t)written >= map_len - pos) {

            return;

        }

        pos += (size_t)written;

    }



    for (size_t i = 0; i < in_n; i++) {

        if (in_pins[i] == CAMPER_BOARD_BOOT_BUTTON_GPIO) {

            continue;

        }

        int written = snprintf(map + pos, map_len - pos, "%s%u:I", (pos > 0) ? "," : "", in_pins[i]);

        if (written < 0 || (size_t)written >= map_len - pos) {

            return;

        }

        pos += (size_t)written;

    }

}



static void gpio_io_parse_comma_list(const char *list, uint8_t *pins, size_t *count, size_t max)

{

    *count = 0;

    if (list == NULL || list[0] == '\0') {

        return;

    }



    const char *p = list;

    while (*p != '\0' && *count < max) {

        while (*p == ' ' || *p == ',') {

            p++;

        }

        if (*p == '\0') {

            break;

        }

        char *end = NULL;

        long val = strtol(p, &end, 10);

        if (end != p && val >= 0 && val < CAMPER_BOARD_MAX_GPIO_PINS) {

            uint8_t pin = (uint8_t)val;

            if (!gpio_io_contains_pin(pins, *count, pin)) {

                pins[(*count)++] = pin;

            }

        }

        p = (*end != '\0') ? end : p + 1;

    }

}



void gpio_io_migrate_lists_to_map(const char *out_list, const char *in_list,

                                  char *map, size_t map_len)

{

    uint8_t out_pins[CAMPER_IO_MAX_OUTPUTS];

    uint8_t in_pins[CAMPER_IO_MAX_INPUTS];

    size_t out_n = 0;

    size_t in_n = 0;



    if (map == NULL || map_len == 0) {

        return;

    }



    gpio_io_parse_comma_list(out_list, out_pins, &out_n, CAMPER_IO_MAX_OUTPUTS);

    gpio_io_parse_comma_list(in_list, in_pins, &in_n, CAMPER_IO_MAX_INPUTS);



    for (size_t i = 0; i < in_n;) {

        if (in_pins[i] == CAMPER_BOARD_BOOT_BUTTON_GPIO) {

            in_pins[i] = in_pins[in_n - 1];

            in_n--;

        } else {

            i++;

        }

    }



    gpio_io_format_pin_map(out_pins, out_n, in_pins, in_n, map, map_len);

}



void gpio_io_migrate_legacy_to_map(uint8_t output_pin, uint8_t button_pin,

                                   char *map, size_t map_len)

{

    uint8_t out_pins[CAMPER_IO_MAX_OUTPUTS] = {0};

    uint8_t in_pins[CAMPER_IO_MAX_INPUTS] = {0};

    size_t out_n = 0;

    size_t in_n = 0;



    if (output_pin > 0) {

        out_pins[out_n++] = output_pin;

    }

    if (button_pin > 0 && button_pin != CAMPER_BOARD_BOOT_BUTTON_GPIO) {

        in_pins[in_n++] = button_pin;

    }



    gpio_io_format_pin_map(out_pins, out_n, in_pins, in_n, map, map_len);

}



esp_err_t gpio_io_pin_map_upsert(char *map, size_t cap, uint8_t pin, bool is_output,

                                 uint8_t temp_gpio)

{

    if (map == NULL || cap == 0) {

        return ESP_ERR_INVALID_ARG;

    }

    if (pin == 0 || gpio_io_is_pin_reserved(pin, temp_gpio)) {

        return ESP_ERR_INVALID_ARG;

    }

    if (!gpio_io_role_allowed(pin, is_output)) {

        return ESP_ERR_INVALID_ARG;

    }



    uint8_t out_pins[CAMPER_IO_MAX_OUTPUTS];

    uint8_t in_pins[CAMPER_IO_MAX_INPUTS];

    size_t out_n = 0;

    size_t in_n = 0;



    if (map[0] != '\0' &&

        gpio_io_parse_pin_map(map, temp_gpio, out_pins, &out_n, in_pins, &in_n, NULL) < 0) {

        return ESP_ERR_INVALID_ARG;

    }



    for (size_t i = 0; i < out_n; i++) {

        if (out_pins[i] == pin) {

            out_pins[i] = out_pins[out_n - 1];

            out_n--;

            break;

        }

    }

    for (size_t i = 0; i < in_n; i++) {

        if (in_pins[i] == pin) {

            in_pins[i] = in_pins[in_n - 1];

            in_n--;

            break;

        }

    }



    if (is_output) {

        if (out_n >= CAMPER_IO_MAX_OUTPUTS) {

            return ESP_ERR_NO_MEM;

        }

        out_pins[out_n++] = pin;

    } else {

        if (in_n >= CAMPER_IO_MAX_INPUTS) {

            return ESP_ERR_NO_MEM;

        }

        in_pins[in_n++] = pin;

    }



    gpio_io_format_pin_map(out_pins, out_n, in_pins, in_n, map, cap);

    return ESP_OK;

}

