/**
 * @file ds18b20_svc.c
 * @brief DS18B20 OneWire reader — publishes EVT_TEMPERATURE_UPDATE.
 */

#include "ds18b20_svc.h"
#include "board_config.h"
#include "event_bus.h"
#include "event_types.h"
#include "logger.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TAG              "DS18B20"
#define SOURCE_ID_DS18B20 0x0400U
#define TEMP_SENSOR_BIND  2U
#define OW_CONVERT_MS     800U

static ds18b20_svc_deps_t s_deps;
static uint8_t s_gpio_pin;
static uint32_t s_poll_sec = 30;
static TaskHandle_t s_task;
static bool s_running;
static bool s_bus_ready;
static portMUX_TYPE s_ow_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_last_scratch[9];
static bool s_last_crc_fail;

static void ds18b20_log_warn(const char *fmt, ...);

static void ow_delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

static bool ds18b20_is_strapping_pin(uint8_t pin)
{
    static const uint8_t straps[] = CAMPER_BOARD_STRAPPING_PINS;

    for (size_t i = 0; i < CAMPER_BOARD_STRAPPING_COUNT; i++) {
        if (straps[i] == pin) {
            return true;
        }
    }
    return false;
}

static esp_err_t ds18b20_bus_init(uint8_t pin)
{
    if (pin == 0 || pin >= CAMPER_BOARD_MAX_GPIO_PINS || ds18b20_is_strapping_pin(pin)) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level((gpio_num_t)pin, 1);
    s_bus_ready = true;
    return ESP_OK;
}

static void ds18b20_bus_release(void)
{
    s_bus_ready = false;
}

static void ow_bus_release(int pin)
{
    gpio_set_level(pin, 1);
    ow_delay_us(100);
}

static bool ow_reset(int pin)
{
    gpio_set_level(pin, 0);
    ow_delay_us(480);

    gpio_set_level(pin, 1);
    ow_delay_us(70);

    bool presence = (gpio_get_level(pin) == 0);
    ow_delay_us(410);
    return presence;
}

static bool ow_reset_retry(int pin, unsigned attempts)
{
    for (unsigned i = 0; i < attempts; i++) {
        ow_bus_release(pin);
        if (i > 0) {
            ow_delay_us(10000);
        }
        if (ow_reset(pin)) {
            return true;
        }
    }
    return false;
}

static void ow_write_bit(int pin, int bit)
{
    gpio_set_level(pin, 0);
    ow_delay_us(bit ? 6 : 60);

    gpio_set_level(pin, 1);
    ow_delay_us(bit ? 64 : 10);
}

static int ow_read_bit(int pin)
{
    gpio_set_level(pin, 0);
    ow_delay_us(3);

    gpio_set_level(pin, 1);
    ow_delay_us(15);

    int bit = gpio_get_level(pin);
    ow_delay_us(45);
    return bit;
}

static void ow_write_byte(int pin, uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        ow_write_bit(pin, byte & 0x01);
        byte >>= 1;
    }
}

static uint8_t ow_read_byte(int pin)
{
    uint8_t byte = 0;

    for (int i = 0; i < 8; i++) {
        byte >>= 1;
        if (ow_read_bit(pin)) {
            byte |= 0x80;
        }
    }
    return byte;
}

static uint8_t ow_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x01) {
                crc = (crc >> 1) ^ 0x8CU;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static bool ds18b20_raw_valid(int16_t raw)
{
    /* 0xFFFF (-0.0625 C) = bus read error / no valid conversion. */
    if (raw == (int16_t)0xFFFF || raw == (int16_t)-1) {
        return false;
    }
    /* Power-on reset scratchpad value is 85.0 C (850 in 1/16 C). */
    if (raw == (int16_t)(85 * 16)) {
        return false;
    }
    return true;
}

static float ds18b20_read_celsius_locked(int pin)
{
    if (!ow_reset_retry(pin, 3)) {
        return NAN;
    }

    ow_write_byte(pin, 0xCC);
    ow_write_byte(pin, 0x44);

    portEXIT_CRITICAL(&s_ow_lock);
    vTaskDelay(pdMS_TO_TICKS(OW_CONVERT_MS));
    portENTER_CRITICAL(&s_ow_lock);

    if (!ow_reset_retry(pin, 3)) {
        return NAN;
    }

    ow_write_byte(pin, 0xCC);
    ow_write_byte(pin, 0xBE);

    uint8_t scratch[9];
    for (size_t i = 0; i < sizeof(scratch); i++) {
        scratch[i] = ow_read_byte(pin);
    }

    if (ow_crc8(scratch, 8) != scratch[8]) {
        memcpy(s_last_scratch, scratch, sizeof(scratch));
        s_last_crc_fail = true;
        return NAN;
    }

    s_last_crc_fail = false;

    int16_t raw = (int16_t)((scratch[1] << 8) | scratch[0]);
    if (!ds18b20_raw_valid(raw)) {
        return NAN;
    }

    return (float)raw / 16.0f;
}

static float ds18b20_read_celsius(int pin)
{
    float temp;

    s_last_crc_fail = false;
    portENTER_CRITICAL(&s_ow_lock);
    temp = ds18b20_read_celsius_locked(pin);
    portEXIT_CRITICAL(&s_ow_lock);

    if (isnan(temp) && s_last_crc_fail) {
        ds18b20_log_warn("GPIO %u: CRC fail [%02X %02X %02X %02X %02X %02X %02X %02X %02X]",
                         (unsigned)pin,
                         s_last_scratch[0], s_last_scratch[1], s_last_scratch[2], s_last_scratch[3],
                         s_last_scratch[4], s_last_scratch[5], s_last_scratch[6],
                         s_last_scratch[7], s_last_scratch[8]);
    }

    return temp;
}

static void ds18b20_log_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (s_deps.logger != NULL) {
        char buf[96];
        vsnprintf(buf, sizeof(buf), fmt, args);
        logger_info(s_deps.logger, TAG, "%s", buf);
    } else {
        esp_log_writev(ESP_LOG_INFO, TAG, fmt, args);
    }
    va_end(args);
}

static void ds18b20_log_warn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (s_deps.logger != NULL) {
        char buf[96];
        vsnprintf(buf, sizeof(buf), fmt, args);
        logger_warning(s_deps.logger, TAG, "%s", buf);
    } else {
        esp_log_writev(ESP_LOG_WARN, TAG, fmt, args);
    }
    va_end(args);
}

static void ds18b20_publish_reading(float temp)
{
    if (s_deps.event_bus == NULL) {
        return;
    }

    event_t evt = {
        .type = EVT_TEMPERATURE_UPDATE,
        .source_id = SOURCE_ID_DS18B20,
        .gpio_id = TEMP_SENSOR_BIND,
        .data.float_val = temp,
    };
    event_bus_publish(s_deps.event_bus, &evt);
    ds18b20_log_info("GPIO %u: %.2f C", s_gpio_pin, temp);
}

static void ds18b20_poll_once(void)
{
    if (s_gpio_pin == 0 || !s_bus_ready) {
        return;
    }

    float temp = ds18b20_read_celsius((int)s_gpio_pin);
    if (isnan(temp)) {
        int level = gpio_get_level((gpio_num_t)s_gpio_pin);
        ds18b20_log_warn("GPIO %u: read failed (line=%d) — check VDD/GND/DQ and 4.7k pull-up DQ->3.3V",
                         s_gpio_pin, level);
        return;
    }
    if (temp <= -55.0f || temp >= 125.0f) {
        ds18b20_log_warn("GPIO %u: out of range (%.2f C)", s_gpio_pin, temp);
        return;
    }

    ds18b20_publish_reading(temp);
}

static void ds18b20_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(1000));

    while (s_running) {
        ds18b20_poll_once();
        vTaskDelay(pdMS_TO_TICKS(s_poll_sec * 1000U));
    }

    ds18b20_bus_release();
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t ds18b20_svc_set_pin(uint8_t gpio_pin)
{
    s_gpio_pin = gpio_pin;
    return ESP_OK;
}

esp_err_t ds18b20_svc_start(const ds18b20_svc_deps_t *deps, uint8_t gpio_pin,
                            uint32_t poll_interval_sec)
{
    if (deps == NULL || deps->event_bus == NULL || gpio_pin == 0) {
        ESP_LOGW(TAG, "start rejected (gpio=%u)", gpio_pin);
        return ESP_ERR_INVALID_ARG;
    }

    if (s_running) {
        ds18b20_svc_stop();
    }

    esp_err_t err = ds18b20_bus_init(gpio_pin);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bus init GPIO %u failed: %s", gpio_pin, esp_err_to_name(err));
        return err;
    }

    s_deps = *deps;
    s_gpio_pin = gpio_pin;
    s_poll_sec = poll_interval_sec > 0 ? poll_interval_sec : 30;
    s_running = true;

    ESP_LOGI(TAG, "started on GPIO %u, poll every %u s (use 4.7k pull-up on DQ if reads fail)",
             gpio_pin, (unsigned)s_poll_sec);

    BaseType_t ok = xTaskCreate(ds18b20_task, "ds18b20", 4096, NULL, 4, &s_task);
    if (ok != pdPASS) {
        s_running = false;
        ds18b20_bus_release();
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ds18b20_svc_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    s_running = false;
    while (s_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}
