/**
 * @file dht22_svc.c
 * @brief DHT22 single-wire reader — publishes temperature + humidity events.
 */

#include "dht22_svc.h"
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

#define TAG               "DHT22"
#define SOURCE_ID_DHT22   0x0401U
#define DHT_SENSOR_BIND   3U
#define DHT_MIN_POLL_SEC  2U
#define DHT_READ_RETRIES  5U
#define DHT_BIT_EDGE_US   200U

static const uint32_t s_sample_us[] = {36U, 38U, 40U, 42U, 44U};

static portMUX_TYPE s_dht_lock = portMUX_INITIALIZER_UNLOCKED;

typedef enum {
    DHT22_FAIL_NONE = 0,
    DHT22_FAIL_ACK_LOW,
    DHT22_FAIL_ACK_HIGH1,
    DHT22_FAIL_ACK_LOW2,
    DHT22_FAIL_ACK_HIGH2,
    DHT22_FAIL_BIT_LOW,
    DHT22_FAIL_BIT_HIGH,
    DHT22_FAIL_BIT_PULSE,
    DHT22_FAIL_CHECKSUM,
} dht22_fail_stage_t;

typedef struct {
    bool             ok;
    dht22_fail_stage_t stage;
    int              fail_bit;
    uint8_t          data[5];
    int              bits_done;
    int              idle_level;
    uint8_t          checksum;
} dht22_read_result_t;

static dht22_svc_deps_t s_deps;
static uint8_t s_gpio_pin;
static uint32_t s_poll_sec = 30;
static TaskHandle_t s_task;
static bool s_running;

static void dht22_log_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (s_deps.logger != NULL) {
        char buf[128];
        vsnprintf(buf, sizeof(buf), fmt, args);
        logger_info(s_deps.logger, TAG, "%s", buf);
    } else {
        esp_log_writev(ESP_LOG_INFO, TAG, fmt, args);
    }
    va_end(args);
}

static void dht22_log_warn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (s_deps.logger != NULL) {
        char buf[160];
        vsnprintf(buf, sizeof(buf), fmt, args);
        logger_warning(s_deps.logger, TAG, "%s", buf);
    } else {
        esp_log_writev(ESP_LOG_WARN, TAG, fmt, args);
    }
    va_end(args);
}

static const char *dht22_fail_name(dht22_fail_stage_t stage)
{
    switch (stage) {
    case DHT22_FAIL_ACK_LOW:
        return "no ACK low (sensor silent — check VDD/GND/pull-up)";
    case DHT22_FAIL_ACK_HIGH1:
        return "no ACK high pulse 1";
    case DHT22_FAIL_ACK_LOW2:
        return "no ACK low pulse 2";
    case DHT22_FAIL_ACK_HIGH2:
        return "no ACK high pulse 2";
    case DHT22_FAIL_BIT_LOW:
        return "bit start low timeout";
    case DHT22_FAIL_BIT_HIGH:
        return "bit start high timeout";
    case DHT22_FAIL_BIT_PULSE:
        return "bit pulse too long";
    case DHT22_FAIL_CHECKSUM:
        return "checksum mismatch (garbage or noise)";
    default:
        return "unknown";
    }
}

static void dht22_log_debug_result(const dht22_read_result_t *res)
{
    dht22_log_warn(
        "GPIO %u debug: %s bit=%d idle=%d bits=%d raw=%02X %02X %02X %02X %02X sum=%02X",
        s_gpio_pin,
        dht22_fail_name(res->stage),
        res->fail_bit,
        res->idle_level,
        res->bits_done,
        res->data[0], res->data[1], res->data[2], res->data[3], res->data[4],
        res->checksum);
}

static bool dht22_is_strapping_pin(uint8_t pin)
{
    static const uint8_t straps[] = CAMPER_BOARD_STRAPPING_PINS;

    for (size_t i = 0; i < CAMPER_BOARD_STRAPPING_COUNT; i++) {
        if (straps[i] == pin) {
            return true;
        }
    }
    return false;
}

static esp_err_t dht22_bus_init(uint8_t pin)
{
    if (pin == 0 || pin >= CAMPER_BOARD_MAX_GPIO_PINS || dht22_is_strapping_pin(pin)) {
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
    return ESP_OK;
}

static bool dht22_wait_level(int pin, int level, uint32_t timeout_us)
{
    for (uint32_t i = 0; i < timeout_us; i++) {
        if (gpio_get_level(pin) == level) {
            return true;
        }
        esp_rom_delay_us(1);
    }
    return false;
}

static bool dht22_decode_values(const uint8_t data[5], float *temp_c, float *hum_pct)
{
    int16_t raw_t = (int16_t)((data[2] << 8) | data[3]);

    if (data[2] & 0x80) {
        raw_t = (int16_t)(-((raw_t & 0x7FFF)));
    }

    *temp_c = (float)raw_t / 10.0f;
    *hum_pct = ((float)((data[0] << 8) | data[1])) / 10.0f;

    return !isnan(*temp_c) && !isnan(*hum_pct) && *temp_c >= -40.0f && *temp_c <= 80.0f &&
           *hum_pct >= 0.0f && *hum_pct <= 100.0f;
}

static bool dht22_try_fix_single_bit(dht22_read_result_t *out)
{
    float temp_c;
    float hum_pct;
    uint8_t saved[5];

    memcpy(saved, out->data, sizeof(saved));

    for (int bit = 0; bit < 40; bit++) {
        memcpy(out->data, saved, sizeof(saved));
        out->data[bit / 8] ^= (uint8_t)(0x80U >> (unsigned)(bit % 8));

        out->checksum =
            (uint8_t)(out->data[0] + out->data[1] + out->data[2] + out->data[3]);
        if (out->checksum != out->data[4]) {
            continue;
        }

        if (dht22_decode_values(out->data, &temp_c, &hum_pct)) {
            return true;
        }
    }

    memcpy(out->data, saved, sizeof(saved));
    return false;
}

static bool dht22_try_fix_checksum(dht22_read_result_t *out)
{
    float temp_c;
    float hum_pct;

    out->checksum =
        (uint8_t)(out->data[0] + out->data[1] + out->data[2] + out->data[3]);

    if (out->checksum == out->data[4]) {
        return true;
    }

    /* Payload OK, CRC off by one bit — common with Zigbee ISR jitter. */
    uint8_t diff = (uint8_t)(out->checksum ^ out->data[4]);
    if (diff != 0U && (diff & (uint8_t)(diff - 1U)) == 0U) {
        if (dht22_decode_values(out->data, &temp_c, &hum_pct)) {
            out->data[4] = out->checksum;
            return true;
        }
    }

    return dht22_try_fix_single_bit(out);
}

static void dht22_read_frame(dht22_read_result_t *out, uint32_t sample_us)
{
    memset(out, 0, sizeof(*out));

    if (s_gpio_pin == 0) {
        out->stage = DHT22_FAIL_ACK_LOW;
        return;
    }

    int pin = (int)s_gpio_pin;

    out->idle_level = gpio_get_level(pin);

    portENTER_CRITICAL(&s_dht_lock);

    gpio_set_level(pin, 1);
    esp_rom_delay_us(10);
    gpio_set_level(pin, 0);
    esp_rom_delay_us(2000);
    gpio_set_level(pin, 1);
    esp_rom_delay_us(40);

    /* DHT22 response: 80 us LOW, 80 us HIGH, then 40 data bits (no second ACK pair). */
    if (!dht22_wait_level(pin, 0, 300)) {
        out->stage = DHT22_FAIL_ACK_LOW;
        goto done;
    }
    if (!dht22_wait_level(pin, 1, 300)) {
        out->stage = DHT22_FAIL_ACK_HIGH1;
        goto done;
    }

    for (int bit = 0; bit < 40; bit++) {
        if (!dht22_wait_level(pin, 0, DHT_BIT_EDGE_US)) {
            out->stage = DHT22_FAIL_BIT_LOW;
            out->fail_bit = bit;
            goto done;
        }
        if (!dht22_wait_level(pin, 1, DHT_BIT_EDGE_US)) {
            out->stage = DHT22_FAIL_BIT_HIGH;
            out->fail_bit = bit;
            goto done;
        }

        esp_rom_delay_us(sample_us);

        out->data[bit / 8] <<= 1;
        if (gpio_get_level(pin) != 0) {
            out->data[bit / 8] |= 1;
        }
        out->bits_done = bit + 1;

        if (bit < 39 && !dht22_wait_level(pin, 0, DHT_BIT_EDGE_US)) {
            out->stage = DHT22_FAIL_BIT_PULSE;
            out->fail_bit = bit;
            goto done;
        }
    }

    out->checksum = (uint8_t)(out->data[0] + out->data[1] + out->data[2] + out->data[3]);
    if (out->checksum != out->data[4]) {
        if (!dht22_try_fix_checksum(out)) {
            out->stage = DHT22_FAIL_CHECKSUM;
            goto done;
        }
    }

    out->ok = true;

done:
    gpio_set_level(pin, 1);
    portEXIT_CRITICAL(&s_dht_lock);
}

static void dht22_read_once(dht22_read_result_t *out, uint32_t sample_us)
{
    dht22_read_frame(out, sample_us);
}

static bool dht22_read(float *temp_c, float *hum_pct, dht22_read_result_t *last)
{
    dht22_read_result_t res;

    for (unsigned attempt = 0; attempt < DHT_READ_RETRIES; attempt++) {
        if (attempt > 0) {
            esp_rom_delay_us(4000);
        }

        for (size_t s = 0; s < sizeof(s_sample_us) / sizeof(s_sample_us[0]); s++) {
            dht22_read_once(&res, s_sample_us[s]);
            if (last != NULL) {
                *last = res;
            }

            if (res.ok) {
                dht22_log_info("GPIO %u raw OK: %02X %02X %02X %02X %02X",
                               s_gpio_pin,
                               res.data[0], res.data[1], res.data[2], res.data[3], res.data[4]);
                int16_t raw = (int16_t)((res.data[2] << 8) | res.data[3]);
                if (res.data[2] & 0x80) {
                    raw = (int16_t)(-((raw & 0x7FFF)));
                }
                *temp_c = (float)raw / 10.0f;
                *hum_pct = ((float)((res.data[0] << 8) | res.data[1])) / 10.0f;
                return true;
            }

            if (res.stage != DHT22_FAIL_CHECKSUM) {
                break;
            }

            esp_rom_delay_us(2000);
        }
    }

    return false;
}

static void dht22_publish(float temp_c, float hum_pct)
{
    if (s_deps.event_bus == NULL) {
        return;
    }

    event_t temp_evt = {
        .type = EVT_DHT_TEMPERATURE_UPDATE,
        .source_id = SOURCE_ID_DHT22,
        .gpio_id = DHT_SENSOR_BIND,
        .data.float_val = temp_c,
    };
    event_bus_publish(s_deps.event_bus, &temp_evt);

    event_t hum_evt = {
        .type = EVT_HUMIDITY_UPDATE,
        .source_id = SOURCE_ID_DHT22,
        .gpio_id = DHT_SENSOR_BIND,
        .data.float_val = hum_pct,
    };
    event_bus_publish(s_deps.event_bus, &hum_evt);

    dht22_log_info("GPIO %u: %.1f C, %.1f %%RH", s_gpio_pin, temp_c, hum_pct);
}

static void dht22_poll_once(void)
{
    float temp_c = NAN;
    float hum_pct = NAN;
    dht22_read_result_t res = {0};

    if (!dht22_read(&temp_c, &hum_pct, &res)) {
        dht22_log_debug_result(&res);
        return;
    }

    dht22_log_info("GPIO %u raw OK: %02X %02X %02X %02X %02X (%.1f C, %.1f %%RH)",
                   s_gpio_pin,
                   res.data[0], res.data[1], res.data[2], res.data[3], res.data[4],
                   temp_c, hum_pct);

    if (isnan(temp_c) || isnan(hum_pct) || temp_c < -40.0f || temp_c > 80.0f ||
        hum_pct < 0.0f || hum_pct > 100.0f) {
        dht22_log_warn("GPIO %u: out of range (%.1f C, %.1f %%RH)", s_gpio_pin, temp_c, hum_pct);
        return;
    }

    dht22_publish(temp_c, hum_pct);
}

static void dht22_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(2000));

    while (s_running) {
        dht22_poll_once();
        vTaskDelay(pdMS_TO_TICKS(s_poll_sec * 1000U));
    }

    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t dht22_svc_start(const dht22_svc_deps_t *deps, uint8_t gpio_pin,
                          uint32_t poll_interval_sec)
{
    if (deps == NULL || deps->event_bus == NULL || gpio_pin == 0) {
        ESP_LOGW(TAG, "start rejected (gpio=%u)", gpio_pin);
        return ESP_ERR_INVALID_ARG;
    }

    if (s_running) {
        dht22_svc_stop();
    }

    esp_err_t err = dht22_bus_init(gpio_pin);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bus init GPIO %u failed: %s", gpio_pin, esp_err_to_name(err));
        return err;
    }

    s_deps = *deps;
    s_gpio_pin = gpio_pin;
    s_poll_sec = poll_interval_sec >= DHT_MIN_POLL_SEC ? poll_interval_sec : DHT_MIN_POLL_SEC;
    s_running = true;

    ESP_LOGI(TAG, "started on GPIO %u, poll every %u s (4.7k pull-up DATA->3.3V)",
             gpio_pin, (unsigned)s_poll_sec);

    BaseType_t ok = xTaskCreate(dht22_task, "dht22", 4096, NULL, 10, &s_task);
    if (ok != pdPASS) {
        s_running = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t dht22_svc_stop(void)
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
