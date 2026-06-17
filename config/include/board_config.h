#pragma once

/**
 * @file board_config.h
 * @brief Board-level hardware constants for CamperNode OS.
 *
 * Compile-time limits only. No ESP-IDF driver includes — runtime GPIO
 * assignment is stored in NVS and applied by the GPIO manager.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum configurable GPIO slots per node */
#define CAMPER_BOARD_MAX_GPIO_PINS  24

/** ESP32-C6 strapping pins that must not be reassigned */
#define CAMPER_BOARD_STRAPPING_PINS  { 4, 5, 8, 9, 15 }

/** Number of strapping pins */
#define CAMPER_BOARD_STRAPPING_COUNT  5

/** Default serial log UART port (USB-JTAG on C6 devkit) */
#define CAMPER_BOARD_LOG_UART_NUM     0

#ifdef __cplusplus
}
#endif
