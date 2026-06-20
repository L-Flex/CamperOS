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

/** DevKitC-1 onboard BOOT button (GPIO9). Button input only; set 255 to disable. */
#define CAMPER_BOARD_BOOT_BUTTON_GPIO  9U

/** ESP32-C6 strapping pins that must not be reassigned */
#define CAMPER_BOARD_STRAPPING_PINS  { 4, 5, 8, 9, 15 }

/** Number of strapping pins */
#define CAMPER_BOARD_STRAPPING_COUNT  5

/** Default serial log UART port (USB-JTAG on C6 devkit) */
#define CAMPER_BOARD_LOG_UART_NUM     0

/** Onboard WS2812 RGB LED (ESP32-C6-DevKitC-1: GPIO 8). Set to 255 to disable. */
#define CAMPER_BOARD_STATUS_LED_GPIO        8U

/** Optional sensor data pins (not part of GPIO-Karte). */
#define CAMPER_BOARD_SENSOR_GPIO_6          6U
#define CAMPER_BOARD_SENSOR_GPIO_7          7U
/** @deprecated aliases */
#define CAMPER_BOARD_SENSOR_GPIO            CAMPER_BOARD_SENSOR_GPIO_6
#define CAMPER_BOARD_TEMP_GPIO              CAMPER_BOARD_SENSOR_GPIO_6

/** ESP32-C6 DevKit UART0 console (idf.py monitor) — do not use in GPIO-Karte. */
#define CAMPER_BOARD_CONSOLE_TX_GPIO        16U
#define CAMPER_BOARD_CONSOLE_RX_GPIO        17U

/** 1 = LED is active-low (an to GND), 0 = active-high */
#define CAMPER_BOARD_STATUS_LED_ACTIVE_LOW  0

/** Identify blink duration when HA "Identifizieren" is pressed */
#define CAMPER_IDENTIFY_DURATION_SEC  30U

/** Slow blink half-period (on/off each) in milliseconds */
#define CAMPER_IDENTIFY_BLINK_MS        500U

#ifdef __cplusplus
}
#endif
