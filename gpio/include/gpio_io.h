#pragma once



/**

 * @file gpio_io.h

 * @brief Per-pin I/O map — each GPIO is input or output (configured once).

 */



#include "esp_err.h"

#include "gpio_types.h"

#include <stddef.h>

#include <stdint.h>

#include <stdbool.h>



#ifdef __cplusplus

extern "C" {

#endif



#define CAMPER_IO_MAX_OUTPUTS       16U

#define CAMPER_IO_MAX_INPUTS        16U

#define CAMPER_IO_INPUT_BIND_BASE    64U

#define CAMPER_IO_PIN_MAP_MAX_LEN   128U



/**

 * Pin map format: "3:O,5:Od,10:I" (O=output, Od=open-drain/MOSFET, I=input).

 * GPIO9 (BOOT) is always input and must not appear in the map.

 * Status-LED and temp sensor pins are reserved.

 */



bool gpio_io_is_pin_reserved(uint8_t pin, uint8_t temp_gpio);



/** Parse pin map into output/input pin arrays (map order preserved). Returns -1 on error. */

int gpio_io_parse_pin_map(const char *map, uint8_t temp_gpio,

                          uint8_t *out_pins, size_t *out_count,

                          uint8_t *in_pins, size_t *in_count,

                          uint8_t *out_flags);



esp_err_t gpio_io_build_config_from_map(const char *map, uint8_t temp_gpio,

                                        gpio_pin_config_t *cfg, size_t *count,

                                        size_t cfg_max);



void gpio_io_format_pin_map(const uint8_t *out_pins, size_t out_n,

                            const uint8_t *in_pins, size_t in_n,

                            char *map, size_t map_len);



void gpio_io_migrate_lists_to_map(const char *out_list, const char *in_list,

                                  char *map, size_t map_len);



void gpio_io_migrate_legacy_to_map(uint8_t output_pin, uint8_t button_pin,

                                   char *map, size_t map_len);



esp_err_t gpio_io_pin_map_upsert(char *map, size_t cap, uint8_t pin, bool is_output,

                                 uint8_t temp_gpio);



#ifdef __cplusplus

}

#endif

