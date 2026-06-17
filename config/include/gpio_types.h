#pragma once

/**
 * @file gpio_types.h
 * @brief GPIO configuration types for CamperNode OS.
 */

#include <stdint.h>
#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GPIO_FUNC_UNUSED = 0,
    GPIO_FUNC_DIGITAL_INPUT,
    GPIO_FUNC_DIGITAL_OUTPUT,
    GPIO_FUNC_RELAY,
    GPIO_FUNC_BUTTON,
    GPIO_FUNC_PWM,
    GPIO_FUNC_ADC,
    GPIO_FUNC_DS18B20,
    GPIO_FUNC_RGB_LED,
    GPIO_FUNC_FAN,
    GPIO_FUNC_VALVE,
    GPIO_FUNC_PUMP,
    GPIO_FUNC_I2C,
    GPIO_FUNC_UART,
    GPIO_FUNC_MAX
} gpio_function_t;

typedef enum {
    GPIO_FLAG_NONE       = 0,
    GPIO_FLAG_INVERT     = (1 << 0),
    GPIO_FLAG_PULLUP     = (1 << 1),
    GPIO_FLAG_PULLDOWN   = (1 << 2),
    GPIO_FLAG_OPEN_DRAIN = (1 << 3),
} gpio_flags_t;

typedef struct {
    uint8_t  pin;
    uint8_t  function;
    uint8_t  flags;
    uint8_t  profile_bind;
    uint16_t debounce_ms;
    uint16_t pwm_freq_hz;
    uint8_t  i2c_addr;
    uint8_t  reserved[5];
} gpio_pin_config_t;

#ifdef __cplusplus
}
#endif
