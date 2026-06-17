/**
 * @file storage.c
 * @brief NVS-backed persistent storage for CamperNode OS.
 */

#include "storage.h"
#include "camper_config.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "STORAGE";

struct storage {
    bool initialized;
};

storage_t *storage_create(void)
{
    return calloc(1, sizeof(storage_t));
}

void storage_destroy(storage_t *storage)
{
    free(storage);
}

static esp_err_t storage_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    return nvs_open(ns, mode, handle);
}

esp_err_t storage_init(storage_t *storage)
{
    if (storage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "erasing NVS partition");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err == ESP_OK) {
        storage->initialized = true;
    }

    return err;
}

esp_err_t storage_migrate(storage_t *storage)
{
    if (storage == NULL || !storage->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t schema = 0;
    esp_err_t err = storage_get_u8(storage, CAMPER_NVS_NS_SYSTEM, CAMPER_KEY_SCHEMA_VER, &schema);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = storage_set_u8(storage, CAMPER_NVS_NS_SYSTEM, CAMPER_KEY_SCHEMA_VER,
                             CAMPER_NVS_SCHEMA_VERSION);
        if (err != ESP_OK) {
            return err;
        }

        char default_name[CAMPER_NODE_NAME_MAX_LEN] = "campernode";
        storage_set_string(storage, CAMPER_NVS_NS_SYSTEM, CAMPER_KEY_NODE_NAME, default_name);
        storage_set_u8(storage, CAMPER_NVS_NS_PROFILE, CAMPER_KEY_PROFILE_ID, 0);
        ESP_LOGI(TAG, "initialized NVS schema v%d", CAMPER_NVS_SCHEMA_VERSION);
        return ESP_OK;
    }

    if (err != ESP_OK) {
        return err;
    }

    if (schema != CAMPER_NVS_SCHEMA_VERSION) {
        ESP_LOGW(TAG, "schema migration %u -> %d not required yet", schema, CAMPER_NVS_SCHEMA_VERSION);
        storage_set_u8(storage, CAMPER_NVS_NS_SYSTEM, CAMPER_KEY_SCHEMA_VER, CAMPER_NVS_SCHEMA_VERSION);
    }

    return ESP_OK;
}

esp_err_t storage_get_string(storage_t *storage, const char *ns, const char *key,
                             char *out, size_t out_len)
{
    if (storage == NULL || !storage->initialized || ns == NULL || key == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = storage_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = out_len;
    err = nvs_get_str(handle, key, out, &len);
    nvs_close(handle);
    return err;
}

esp_err_t storage_set_string(storage_t *storage, const char *ns, const char *key,
                             const char *value)
{
    if (storage == NULL || !storage->initialized || ns == NULL || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = storage_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_get_u8(storage_t *storage, const char *ns, const char *key, uint8_t *out)
{
    if (storage == NULL || !storage->initialized || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = storage_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_u8(handle, key, out);
    nvs_close(handle);
    return err;
}

esp_err_t storage_set_u8(storage_t *storage, const char *ns, const char *key, uint8_t value)
{
    if (storage == NULL || !storage->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = storage_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_get_u32(storage_t *storage, const char *ns, const char *key, uint32_t *out)
{
    if (storage == NULL || !storage->initialized || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = storage_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_u32(handle, key, out);
    nvs_close(handle);
    return err;
}

esp_err_t storage_set_u32(storage_t *storage, const char *ns, const char *key, uint32_t value)
{
    if (storage == NULL || !storage->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = storage_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_get_blob(storage_t *storage, const char *ns, const char *key,
                           void *out, size_t *inout_len)
{
    if (storage == NULL || !storage->initialized || inout_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = storage_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(handle, key, out, inout_len);
    nvs_close(handle);
    return err;
}

esp_err_t storage_set_blob(storage_t *storage, const char *ns, const char *key,
                           const void *data, size_t len)
{
    if (storage == NULL || !storage->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = storage_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, key, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_erase_namespace(storage_t *storage, const char *ns)
{
    if (storage == NULL || !storage->initialized || ns == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = storage_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_factory_reset(storage_t *storage)
{
    if (storage == NULL || !storage->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "factory reset — erasing NVS");
    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_flash_init();
    if (err == ESP_OK) {
        storage->initialized = true;
        err = storage_migrate(storage);
    }
    return err;
}

esp_err_t storage_get_active_profile(storage_t *storage, uint8_t *profile_id)
{
    return storage_get_u8(storage, CAMPER_NVS_NS_PROFILE, CAMPER_KEY_PROFILE_ID, profile_id);
}

esp_err_t storage_set_active_profile(storage_t *storage, uint8_t profile_id)
{
    return storage_set_u8(storage, CAMPER_NVS_NS_PROFILE, CAMPER_KEY_PROFILE_ID, profile_id);
}

esp_err_t storage_load_gpio_config(storage_t *storage, gpio_pin_config_t *cfg, size_t *count)
{
    if (storage == NULL || cfg == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t stored_count = 0;
    size_t blob_len = sizeof(stored_count) + (sizeof(gpio_pin_config_t) * CAMPER_BOARD_MAX_GPIO_PINS);
    uint8_t *blob = malloc(blob_len);
    if (blob == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = storage_get_blob(storage, CAMPER_NVS_NS_GPIO, CAMPER_KEY_GPIO_BLOB, blob, &blob_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *count = 0;
        free(blob);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        free(blob);
        return err;
    }

    if (blob_len < sizeof(stored_count)) {
        free(blob);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(&stored_count, blob, sizeof(stored_count));
    if (stored_count > CAMPER_BOARD_MAX_GPIO_PINS) {
        free(blob);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t expected = sizeof(stored_count) + (stored_count * sizeof(gpio_pin_config_t));
    if (blob_len < expected) {
        free(blob);
        return ESP_ERR_INVALID_SIZE;
    }

    if (stored_count > 0) {
        memcpy(cfg, blob + sizeof(stored_count), stored_count * sizeof(gpio_pin_config_t));
    }
    *count = stored_count;
    free(blob);
    return ESP_OK;
}

esp_err_t storage_save_gpio_config(storage_t *storage, const gpio_pin_config_t *cfg, size_t count)
{
    if (storage == NULL || (count > 0 && cfg == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (count > CAMPER_BOARD_MAX_GPIO_PINS) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t stored_count = (uint8_t)count;
    size_t blob_len = sizeof(stored_count) + (count * sizeof(gpio_pin_config_t));
    uint8_t *blob = malloc(blob_len);
    if (blob == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(blob, &stored_count, sizeof(stored_count));
    if (count > 0) {
        memcpy(blob + sizeof(stored_count), cfg, count * sizeof(gpio_pin_config_t));
    }

    esp_err_t err = storage_set_blob(storage, CAMPER_NVS_NS_GPIO, CAMPER_KEY_GPIO_BLOB, blob, blob_len);
    free(blob);
    return err;
}

esp_err_t storage_increment_boot_count(storage_t *storage, uint32_t *count)
{
    if (storage == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t value = 0;
    esp_err_t err = storage_get_u32(storage, CAMPER_NVS_NS_SYSTEM, CAMPER_KEY_BOOT_COUNT, &value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    value++;
    err = storage_set_u32(storage, CAMPER_NVS_NS_SYSTEM, CAMPER_KEY_BOOT_COUNT, value);
    if (err == ESP_OK) {
        *count = value;
    }
    return err;
}
