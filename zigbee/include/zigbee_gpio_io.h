#pragma once



#include "esp_err.h"

#include "zigbee_camper_cluster.h"

#include <stddef.h>

#include <stdint.h>

#include <stdbool.h>



#ifdef __cplusplus

extern "C" {

#endif



void zigbee_gpio_io_init(const zigbee_camper_deps_t *deps);



esp_err_t zigbee_gpio_io_handle_attr_write(uint16_t attr_id, const uint8_t *value, size_t value_len);



esp_err_t zigbee_gpio_io_apply_legacy_pins(uint8_t button_pin, uint8_t output_pin);



esp_err_t zigbee_gpio_io_set_output_bit(uint8_t index, bool on);



uint16_t zigbee_gpio_io_get_output_state(void);



void zigbee_gpio_io_notify_temp_gpio(uint8_t pin);

void zigbee_gpio_io_on_network_joined(void);

void zigbee_gpio_io_get_attr_ptrs(uint8_t **pin_map, uint16_t **output_state, uint16_t **input_state);



#ifdef __cplusplus

}

#endif

