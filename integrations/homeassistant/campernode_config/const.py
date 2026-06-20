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
SENSOR_GPIO_PINS = frozenset({6, 7})
# ESP32-C6 DevKit: GPIO16/17 = UART0 console when not using USB Serial/JTAG
CONSOLE_UART_GPIO = frozenset({16, 17})
RESERVED_GPIO = frozenset({0, 8, BOOT_GPIO}) | SENSOR_GPIO_PINS | CONSOLE_UART_GPIO
PIN_MAP_MAX_LEN = 128
