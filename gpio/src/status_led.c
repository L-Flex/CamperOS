/**
 * @file status_led.c
 * @brief Onboard WS2812 status LED — slow blink for Zigbee Identify (HA "Identifizieren").
 */

#include "status_led.h"
#include "board_config.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"

#define TAG "STATUS_LED"

/** 10 MHz RMT resolution (1 tick = 0.1 µs) — required for WS2812 timing. */
#define STATUS_LED_RMT_RES_HZ  (10U * 1000U * 1000U)

static esp_timer_handle_t s_blink_timer;
static esp_timer_handle_t s_stop_timer;
static led_strip_handle_t s_led_strip;
static bool s_active;
static bool s_led_on;
static bool s_strip_ready;

static bool status_led_hw_enabled(void)
{
    return CAMPER_BOARD_STATUS_LED_GPIO < CAMPER_BOARD_MAX_GPIO_PINS;
}

static void status_led_hw_set(bool on)
{
    if (!s_strip_ready || s_led_strip == NULL) {
        return;
    }

    if (on) {
        (void)led_strip_set_pixel(s_led_strip, 0, 0, 32, 0);
    } else {
        (void)led_strip_set_pixel(s_led_strip, 0, 0, 0, 0);
    }
    (void)led_strip_refresh(s_led_strip);
    s_led_on = on;
}

static void status_led_blink_cb(void *arg)
{
    (void)arg;
    if (!s_active) {
        return;
    }
    status_led_hw_set(!s_led_on);
}

static void status_led_stop_cb(void *arg)
{
    (void)arg;
    status_led_identify_stop();
}

esp_err_t status_led_init(void)
{
    esp_err_t err = ESP_OK;

    if (!status_led_hw_enabled()) {
        ESP_LOGI(TAG, "status LED disabled");
        return ESP_OK;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = (int)CAMPER_BOARD_STATUS_LED_GPIO,
        .max_leds = 1,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = STATUS_LED_RMT_RES_HZ,
        .flags.with_dma = false,
    };

    err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WS2812 init failed: %s", esp_err_to_name(err));
        return err;
    }

    (void)led_strip_clear(s_led_strip);
    s_strip_ready = true;
    ESP_LOGI(TAG, "WS2812 status LED on GPIO %u", (unsigned)CAMPER_BOARD_STATUS_LED_GPIO);

    const esp_timer_create_args_t blink_args = {
        .callback = status_led_blink_cb,
        .name = "status_led_blink",
    };
    err = esp_timer_create(&blink_args, &s_blink_timer);
    if (err != ESP_OK) {
        return err;
    }

    const esp_timer_create_args_t stop_args = {
        .callback = status_led_stop_cb,
        .name = "status_led_stop",
    };
    return esp_timer_create(&stop_args, &s_stop_timer);
}

void status_led_identify_start(uint32_t seconds)
{
    if (!status_led_hw_enabled() || !s_strip_ready) {
        return;
    }
    if (seconds == 0U) {
        seconds = CAMPER_IDENTIFY_DURATION_SEC;
    }

    if (s_active && s_blink_timer != NULL) {
        esp_timer_stop(s_blink_timer);
    }
    if (s_stop_timer != NULL) {
        esp_timer_stop(s_stop_timer);
    }

    s_active = true;
    s_led_on = false;
    status_led_hw_set(true);

    if (s_blink_timer != NULL) {
        esp_timer_start_periodic(s_blink_timer, (uint64_t)CAMPER_IDENTIFY_BLINK_MS * 1000ULL);
    }
    if (s_stop_timer != NULL) {
        esp_timer_start_once(s_stop_timer, (uint64_t)seconds * 1000000ULL);
    }

    ESP_LOGI(TAG, "identify blink %u s", (unsigned)seconds);
}

void status_led_identify_stop(void)
{
    if (!s_active) {
        return;
    }

    s_active = false;
    if (s_blink_timer != NULL) {
        esp_timer_stop(s_blink_timer);
    }
    if (s_stop_timer != NULL) {
        esp_timer_stop(s_stop_timer);
    }
    status_led_hw_set(false);
    ESP_LOGI(TAG, "identify blink stopped");
}
