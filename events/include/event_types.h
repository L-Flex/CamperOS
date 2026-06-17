#pragma once

/**
 * @file event_types.h
 * @brief CamperNode OS internal event type definitions.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sentinel value when an event is not associated with a GPIO */
#define EVENT_GPIO_NONE  0xFF

typedef enum {
    REBOOT_REASON_USER = 0,
    REBOOT_REASON_PROFILE_CHANGE,
    REBOOT_REASON_CONFIG_CHANGE,
    REBOOT_REASON_OTA,
    REBOOT_REASON_WATCHDOG,
    REBOOT_REASON_FACTORY_RESET,
} reboot_reason_t;

typedef enum {
    EVT_BOOT = 0,
    EVT_BUTTON_PRESSED,
    EVT_BUTTON_RELEASED,
    EVT_RELAY_ON,
    EVT_RELAY_OFF,
    EVT_PUMP_ON,
    EVT_PUMP_OFF,
    EVT_ADC_UPDATE,
    EVT_TEMPERATURE_UPDATE,
    EVT_OTA_START,
    EVT_OTA_FINISHED,
    EVT_PROFILE_CHANGED,
    EVT_CONFIG_CHANGED,
    EVT_ZIGBEE_CMD,
    EVT_WATCHDOG_FEED,
    EVT_SYSTEM_REBOOT,
    EVT_FACTORY_RESET,
    EVT_MAX
} event_type_t;

typedef struct {
    event_type_t type;
    uint32_t     timestamp_ms;
    uint16_t     source_id;
    uint16_t     gpio_id;
    union {
        bool     bool_val;
        int32_t  int_val;
        float    float_val;
        void    *ptr_val;
    } data;
} event_t;

#ifdef __cplusplus
}
#endif
