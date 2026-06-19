"""Constants for CamperNode Config integration."""

DOMAIN = "campernode_config"

MANUFACTURER = "CamperOS"
MODEL = "CamperNode OS"

ZHA_DOMAIN = "zha"

CAMPER_CLUSTER_ID = 0xFB00
CONFIG_ENDPOINT = 10
ATTR_PIN_MAP = 12
ATTR_TEMP_GPIO = 11

BOOT_GPIO = 9
RESERVED_GPIO = frozenset({0, 8, BOOT_GPIO})
PIN_MAP_MAX_LEN = 128
