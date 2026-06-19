#pragma once

/**
 * @file board_gpio.h
 * @brief Board GPIO allow-list (strapping pins vs. permitted functions).
 */

#include "board_config.h"
#include "gpio_types.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief True if @p pin may be used for @p function.
 *
 * GPIO9 (DevKit BOOT) is allowed for GPIO_FUNC_BUTTON only. It remains a
 * strapping pin at chip reset — do not hold it low while resetting/powering on.
 */
static inline bool camper_board_gpio_allowed(uint8_t pin, gpio_function_t function)
{
    if (pin == 0 || pin >= CAMPER_BOARD_MAX_GPIO_PINS) {
        return false;
    }

    static const uint8_t straps[] = CAMPER_BOARD_STRAPPING_PINS;
    for (size_t i = 0; i < CAMPER_BOARD_STRAPPING_COUNT; i++) {
        if (straps[i] != pin) {
            continue;
        }
#if CAMPER_BOARD_BOOT_BUTTON_GPIO < CAMPER_BOARD_MAX_GPIO_PINS
        if (pin == CAMPER_BOARD_BOOT_BUTTON_GPIO && function == GPIO_FUNC_BUTTON) {
            return true;
        }
#endif
        return false;
    }
    return true;
}

#ifdef __cplusplus
}
#endif
