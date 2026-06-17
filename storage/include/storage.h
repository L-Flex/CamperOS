#pragma once

/**
 * @file storage.h
 * @brief NVS-backed persistent storage for CamperNode OS.
 */

#include "esp_err.h"
#include "gpio_types.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct storage storage_t;

/** NVS namespace identifiers */
#define CAMPER_NVS_NS_SYSTEM   "system"
#define CAMPER_NVS_NS_PROFILE  "profile"
#define CAMPER_NVS_NS_GPIO     "gpio"
#define CAMPER_NVS_NS_CALIB    "calib"
#define CAMPER_NVS_NS_SETTINGS "settings"
#define CAMPER_NVS_NS_STATE    "state"
#define CAMPER_NVS_NS_ZIGBEE   "zigbee"

/** NVS keys */
#define CAMPER_KEY_SCHEMA_VER   "schema_ver"
#define CAMPER_KEY_NODE_NAME    "node_name"
#define CAMPER_KEY_PROFILE_ID   "active_id"
#define CAMPER_KEY_GPIO_BLOB    "pin_cfg"
#define CAMPER_KEY_BOOT_COUNT   "boot_count"
#define CAMPER_KEY_OTA_VERSION  "ota_ver"
#define CAMPER_KEY_RELAY_STATE  "relay_on"

storage_t *storage_create(void);
void storage_destroy(storage_t *storage);

esp_err_t storage_init(storage_t *storage);
esp_err_t storage_migrate(storage_t *storage);

esp_err_t storage_get_string(storage_t *storage, const char *ns, const char *key,
                             char *out, size_t out_len);
esp_err_t storage_set_string(storage_t *storage, const char *ns, const char *key,
                             const char *value);

esp_err_t storage_get_u8(storage_t *storage, const char *ns, const char *key, uint8_t *out);
esp_err_t storage_set_u8(storage_t *storage, const char *ns, const char *key, uint8_t value);

esp_err_t storage_get_u32(storage_t *storage, const char *ns, const char *key, uint32_t *out);
esp_err_t storage_set_u32(storage_t *storage, const char *ns, const char *key, uint32_t value);

esp_err_t storage_get_blob(storage_t *storage, const char *ns, const char *key,
                           void *out, size_t *inout_len);
esp_err_t storage_set_blob(storage_t *storage, const char *ns, const char *key,
                           const void *data, size_t len);

esp_err_t storage_erase_namespace(storage_t *storage, const char *ns);
esp_err_t storage_factory_reset(storage_t *storage);

esp_err_t storage_get_active_profile(storage_t *storage, uint8_t *profile_id);
esp_err_t storage_set_active_profile(storage_t *storage, uint8_t profile_id);

esp_err_t storage_load_gpio_config(storage_t *storage, gpio_pin_config_t *cfg,
                                   size_t *count);
esp_err_t storage_save_gpio_config(storage_t *storage, const gpio_pin_config_t *cfg,
                                     size_t count);

esp_err_t storage_increment_boot_count(storage_t *storage, uint32_t *count);

#ifdef __cplusplus
}
#endif
