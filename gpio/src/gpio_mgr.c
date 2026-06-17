/**
 * @file gpio_mgr.c
 * @brief Dynamic GPIO manager — digital I/O, buttons, relays.
 */

#include "gpio_mgr.h"
#include "storage.h"
#include "event_types.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdlib.h>
#include <string.h>

#define TAG           "GPIO"
#define SOURCE_ID_GPIO 0x0200U

typedef struct {
    gpio_pin_config_t cfg;
    bool              output_state;
    bool              last_button_level;
    esp_timer_handle_t debounce_timer;
    bool              debounce_pending;
} gpio_slot_t;

struct gpio_mgr {
    event_bus_t *event_bus;
    gpio_slot_t  slots[CAMPER_BOARD_MAX_GPIO_PINS];
    size_t       slot_count;
    bool         isr_service_installed;
};

static gpio_mgr_t *s_mgr_for_isr = NULL;

static bool is_strapping_pin(uint8_t pin)
{
    const uint8_t strapping[] = CAMPER_BOARD_STRAPPING_PINS;
    for (size_t i = 0; i < CAMPER_BOARD_STRAPPING_COUNT; i++) {
        if (strapping[i] == pin) {
            return true;
        }
    }
    return false;
}

static gpio_slot_t *gpio_find_by_bind(gpio_mgr_t *mgr, uint8_t logical_id)
{
    for (size_t i = 0; i < mgr->slot_count; i++) {
        if (mgr->slots[i].cfg.profile_bind == logical_id) {
            return &mgr->slots[i];
        }
    }
    return NULL;
}

static bool gpio_function_is_output(gpio_function_t fn)
{
    return fn == GPIO_FUNC_DIGITAL_OUTPUT || fn == GPIO_FUNC_RELAY ||
           fn == GPIO_FUNC_VALVE || fn == GPIO_FUNC_PUMP;
}

static bool gpio_function_is_input(gpio_function_t fn)
{
    return fn == GPIO_FUNC_DIGITAL_INPUT || fn == GPIO_FUNC_BUTTON;
}

static void gpio_apply_level(gpio_slot_t *slot, bool value)
{
    bool level = value;
    if (slot->cfg.flags & GPIO_FLAG_INVERT) {
        level = !level;
    }
    gpio_set_level((gpio_num_t)slot->cfg.pin, level);
    slot->output_state = value;
}

static void gpio_publish_button(gpio_mgr_t *mgr, gpio_slot_t *slot, event_type_t type)
{
    if (mgr->event_bus == NULL) {
        return;
    }

    event_t evt = {
        .type = type,
        .source_id = SOURCE_ID_GPIO,
        .gpio_id = slot->cfg.profile_bind,
        .data.bool_val = (type == EVT_BUTTON_PRESSED),
    };
    event_bus_post(mgr->event_bus, &evt);
}

static void gpio_debounce_cb(void *arg)
{
    gpio_slot_t *slot = (gpio_slot_t *)arg;
    gpio_mgr_t *mgr = s_mgr_for_isr;
    if (mgr == NULL || slot == NULL) {
        return;
    }

    int level = gpio_get_level((gpio_num_t)slot->cfg.pin);
    bool pressed = (level == 0);
    if (slot->cfg.flags & GPIO_FLAG_INVERT) {
        pressed = !pressed;
    }

    if (pressed != slot->last_button_level) {
        slot->last_button_level = pressed;
        gpio_publish_button(mgr, slot, pressed ? EVT_BUTTON_PRESSED : EVT_BUTTON_RELEASED);
    }
    slot->debounce_pending = false;
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    gpio_slot_t *slot = (gpio_slot_t *)arg;
    gpio_mgr_t *mgr = s_mgr_for_isr;
    if (mgr == NULL || slot == NULL || slot->debounce_timer == NULL) {
        return;
    }

    if (!slot->debounce_pending) {
        slot->debounce_pending = true;
        esp_timer_start_once(slot->debounce_timer,
                             (uint64_t)slot->cfg.debounce_ms * 1000ULL);
    }
}

static void gpio_output_event_handler(const event_t *evt, void *ctx)
{
    gpio_mgr_t *mgr = (gpio_mgr_t *)ctx;
    if (mgr == NULL || evt == NULL) {
        return;
    }

    bool turn_on = (evt->type == EVT_RELAY_ON || evt->type == EVT_PUMP_ON);
    gpio_slot_t *slot = gpio_find_by_bind(mgr, (uint8_t)evt->gpio_id);
    if (slot == NULL) {
        return;
    }

    if (!gpio_function_is_output((gpio_function_t)slot->cfg.function)) {
        return;
    }

    gpio_apply_level(slot, turn_on);
    ESP_LOGI(TAG, "output bind=%u pin=%u -> %s",
             slot->cfg.profile_bind, slot->cfg.pin, turn_on ? "ON" : "OFF");
}

static esp_err_t gpio_configure_slot(gpio_mgr_t *mgr, gpio_slot_t *slot)
{
    gpio_config_t io = {0};
    io.pin_bit_mask = (1ULL << slot->cfg.pin);

    if (gpio_function_is_output((gpio_function_t)slot->cfg.function)) {
        io.mode = GPIO_MODE_OUTPUT;
        if (slot->cfg.flags & GPIO_FLAG_OPEN_DRAIN) {
            io.mode = GPIO_MODE_OUTPUT_OD;
        }
        ESP_ERROR_CHECK(gpio_config(&io));
        gpio_apply_level(slot, false);
        return ESP_OK;
    }

    if (gpio_function_is_input((gpio_function_t)slot->cfg.function)) {
        io.mode = GPIO_MODE_INPUT;
        if (slot->cfg.flags & GPIO_FLAG_PULLUP) {
            io.pull_up_en = GPIO_PULLUP_ENABLE;
        }
        if (slot->cfg.flags & GPIO_FLAG_PULLDOWN) {
            io.pull_down_en = GPIO_PULLDOWN_ENABLE;
        }
        if (slot->cfg.function == GPIO_FUNC_BUTTON) {
            io.intr_type = GPIO_INTR_ANYEDGE;
        }
        ESP_ERROR_CHECK(gpio_config(&io));

        if (slot->cfg.function == GPIO_FUNC_BUTTON) {
            if (!mgr->isr_service_installed) {
                esp_err_t err = gpio_install_isr_service(0);
                if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                    return err;
                }
                mgr->isr_service_installed = true;
                s_mgr_for_isr = mgr;
            }

            if (slot->cfg.debounce_ms == 0) {
                slot->cfg.debounce_ms = 50;
            }

            esp_timer_create_args_t timer_args = {
                .callback = gpio_debounce_cb,
                .arg = slot,
                .name = "gpio_db",
            };
            ESP_ERROR_CHECK(esp_timer_create(&timer_args, &slot->debounce_timer));
            ESP_ERROR_CHECK(gpio_isr_handler_add((gpio_num_t)slot->cfg.pin, gpio_isr_handler, slot));

            int level = gpio_get_level((gpio_num_t)slot->cfg.pin);
            slot->last_button_level = (level == 0);
            if (slot->cfg.flags & GPIO_FLAG_INVERT) {
                slot->last_button_level = !slot->last_button_level;
            }
        }
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

gpio_mgr_t *gpio_mgr_create(event_bus_t *event_bus)
{
    gpio_mgr_t *mgr = calloc(1, sizeof(gpio_mgr_t));
    if (mgr != NULL) {
        mgr->event_bus = event_bus;
    }
    return mgr;
}

void gpio_mgr_destroy(gpio_mgr_t *mgr)
{
    if (mgr == NULL) {
        return;
    }

    for (size_t i = 0; i < mgr->slot_count; i++) {
        gpio_slot_t *slot = &mgr->slots[i];
        if (slot->debounce_timer != NULL) {
            esp_timer_stop(slot->debounce_timer);
            esp_timer_delete(slot->debounce_timer);
        }
        if (slot->cfg.function == GPIO_FUNC_BUTTON) {
            gpio_isr_handler_remove((gpio_num_t)slot->cfg.pin);
        }
    }

    if (s_mgr_for_isr == mgr) {
        s_mgr_for_isr = NULL;
    }
    free(mgr);
}

esp_err_t gpio_mgr_init(gpio_mgr_t *mgr)
{
    if (mgr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mgr->event_bus != NULL) {
        event_bus_subscribe(mgr->event_bus, EVT_RELAY_ON, gpio_output_event_handler, mgr);
        event_bus_subscribe(mgr->event_bus, EVT_RELAY_OFF, gpio_output_event_handler, mgr);
        event_bus_subscribe(mgr->event_bus, EVT_PUMP_ON, gpio_output_event_handler, mgr);
        event_bus_subscribe(mgr->event_bus, EVT_PUMP_OFF, gpio_output_event_handler, mgr);
    }

    ESP_LOGI(TAG, "initialized");
    return ESP_OK;
}

esp_err_t gpio_mgr_load_from_storage(gpio_mgr_t *mgr, storage_t *storage)
{
    if (mgr == NULL || storage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_pin_config_t cfg[CAMPER_BOARD_MAX_GPIO_PINS];
    size_t count = 0;
    esp_err_t err = storage_load_gpio_config(storage, cfg, &count);
    if (err != ESP_OK) {
        return err;
    }

    if (count == 0) {
        ESP_LOGI(TAG, "no GPIO config in NVS");
        return ESP_OK;
    }

    return gpio_mgr_apply_config(mgr, cfg, count);
}

esp_err_t gpio_mgr_apply_config(gpio_mgr_t *mgr, const gpio_pin_config_t *cfg, size_t count)
{
    if (mgr == NULL || (count > 0 && cfg == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (count > CAMPER_BOARD_MAX_GPIO_PINS) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < mgr->slot_count; i++) {
        gpio_slot_t *slot = &mgr->slots[i];
        if (slot->debounce_timer != NULL) {
            esp_timer_stop(slot->debounce_timer);
            esp_timer_delete(slot->debounce_timer);
            slot->debounce_timer = NULL;
        }
        if (slot->cfg.function == GPIO_FUNC_BUTTON) {
            gpio_isr_handler_remove((gpio_num_t)slot->cfg.pin);
        }
    }
    memset(mgr->slots, 0, sizeof(mgr->slots));
    mgr->slot_count = 0;

    for (size_t i = 0; i < count; i++) {
        if (cfg[i].function == GPIO_FUNC_UNUSED) {
            continue;
        }
        if (is_strapping_pin(cfg[i].pin)) {
            ESP_LOGW(TAG, "skipping strapping pin %u", cfg[i].pin);
            continue;
        }

        mgr->slots[mgr->slot_count].cfg = cfg[i];
        esp_err_t err = gpio_configure_slot(mgr, &mgr->slots[mgr->slot_count]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "pin %u config failed: %s", cfg[i].pin, esp_err_to_name(err));
            return err;
        }
        mgr->slot_count++;
    }

    ESP_LOGI(TAG, "applied %u GPIO slots", (unsigned)mgr->slot_count);
    return ESP_OK;
}

esp_err_t gpio_mgr_write(gpio_mgr_t *mgr, uint8_t logical_id, bool value)
{
    gpio_slot_t *slot = gpio_find_by_bind(mgr, logical_id);
    if (slot == NULL || !gpio_function_is_output((gpio_function_t)slot->cfg.function)) {
        return ESP_ERR_NOT_FOUND;
    }
    gpio_apply_level(slot, value);
    return ESP_OK;
}

esp_err_t gpio_mgr_read(gpio_mgr_t *mgr, uint8_t logical_id, bool *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_slot_t *slot = gpio_find_by_bind(mgr, logical_id);
    if (slot == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    if (gpio_function_is_output((gpio_function_t)slot->cfg.function)) {
        *value = slot->output_state;
        return ESP_OK;
    }

    int level = gpio_get_level((gpio_num_t)slot->cfg.pin);
    *value = (level != 0);
    if (slot->cfg.flags & GPIO_FLAG_INVERT) {
        *value = !*value;
    }
    return ESP_OK;
}

esp_err_t gpio_mgr_set_pwm(gpio_mgr_t *mgr, uint8_t logical_id, uint8_t percent)
{
    (void)percent;
    gpio_slot_t *slot = gpio_find_by_bind(mgr, logical_id);
    if (slot == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (slot->cfg.function != GPIO_FUNC_PWM &&
        slot->cfg.function != GPIO_FUNC_FAN &&
        slot->cfg.function != GPIO_FUNC_RGB_LED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_ERR_NOT_SUPPORTED;
}
