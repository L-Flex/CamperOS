#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize onboard status LED (no-op if disabled in board_config.h). */
esp_err_t status_led_init(void);

/** Slow blink for @p seconds (default: CAMPER_IDENTIFY_DURATION_SEC). */
void status_led_identify_start(uint32_t seconds);

/** Stop identify blink and turn LED off. */
void status_led_identify_stop(void);

#ifdef __cplusplus
}
#endif
