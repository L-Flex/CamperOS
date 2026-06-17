"""ZHA quirk for CamperNode OS (manufacturer cluster 0xFC00 on endpoint 10)."""

from enum import IntEnum
from typing import Final

import zigpy.types as t
from zigpy.quirks import CustomCluster
from zigpy.quirks.v2 import (
    EntityType,
    QuirkBuilder,
    ReportingConfig,
    SensorDeviceClass,
    SensorStateClass,
)
from zigpy.zcl.clusters.general import OnOff
from zigpy.zcl.foundation import BaseAttributeDefs, BaseCommandDefs, ZCLAttributeDef, ZCLCommandDef

CAMPER_CLUSTER_ID = 0xFC00
CONFIG_ENDPOINT = 10
LOAD_ENDPOINT = 1
HA_ON_OFF_OUTPUT_DEVICE_ID = 0x0002
HA_ON_OFF_LIGHT_DEVICE_ID = 0x0100


class CamperProfile(IntEnum):
    """Active profile IDs stored in NVS."""

    RELAY = 0
    PUMP = 1
    LIGHT = 2
    TANK = 3
    CLIMATE = 4
    FAN = 5
    BATTERY = 6
    SENSOR = 7
    CUSTOM = 8


class CamperNodeCluster(CustomCluster):
    """CamperNode configuration cluster — mirrors firmware zigbee_clusters.h."""

    cluster_id: Final[t.uint16_t] = CAMPER_CLUSTER_ID
    name: str = "CamperNode configuration"

    class AttributeDefs(BaseAttributeDefs):
        node_name: Final = ZCLAttributeDef(id=0x0000, type=t.LVBytes, access="rw")
        profile_id: Final = ZCLAttributeDef(id=0x0001, type=t.uint8_t, access="rw")
        gpio_config: Final = ZCLAttributeDef(id=0x0002, type=t.LVBytes, access="rw")
        calibration: Final = ZCLAttributeDef(id=0x0003, type=t.LVBytes, access="rw")
        uptime_sec: Final = ZCLAttributeDef(id=0x0004, type=t.uint32_t, access="r")
        last_rssi: Final = ZCLAttributeDef(id=0x0005, type=t.int8s, access="r")
        log_level: Final = ZCLAttributeDef(id=0x0006, type=t.uint8_t, access="rw")
        firmware_version: Final = ZCLAttributeDef(
            id=0x0007, type=t.LVBytes, access="r"
        )
        button_gpio: Final = ZCLAttributeDef(id=0x0008, type=t.uint8_t, access="rw")
        output_gpio: Final = ZCLAttributeDef(id=0x0009, type=t.uint8_t, access="rw")

    class ServerCommandDefs(BaseCommandDefs):
        reboot: Final = ZCLCommandDef(id=0x00, schema={}, direction=False)
        factory_reset: Final = ZCLCommandDef(id=0x01, schema={}, direction=False)
        trigger_ota: Final = ZCLCommandDef(id=0x02, schema={}, direction=False)


def _campernode_device_filter(device) -> bool:
    """Match nodes that expose the CamperNode config cluster on endpoint 10."""
    ep = device.endpoints.get(CONFIG_ENDPOINT)
    if ep is None:
        return False
    return CAMPER_CLUSTER_ID in ep.in_clusters


def _load_endpoint_device_type(device) -> int | None:
    ep = device.endpoints.get(LOAD_ENDPOINT)
    if ep is None:
        return None
    return ep.device_type


def _pump_node_filter(device) -> bool:
    return (
        _campernode_device_filter(device)
        and _load_endpoint_device_type(device) == HA_ON_OFF_OUTPUT_DEVICE_ID
    )


def _relay_node_filter(device) -> bool:
    return (
        _campernode_device_filter(device)
        and _load_endpoint_device_type(device) == HA_ON_OFF_LIGHT_DEVICE_ID
    )


def _config_cluster_entities(builder: QuirkBuilder) -> QuirkBuilder:
    return (
        builder
        .replaces(CamperNodeCluster, endpoint_id=CONFIG_ENDPOINT)
        .enum(
            attribute_name=CamperNodeCluster.AttributeDefs.profile_id.name,
            enum_class=CamperProfile,
            cluster_id=CAMPER_CLUSTER_ID,
            endpoint_id=CONFIG_ENDPOINT,
            translation_key="profile",
            fallback_name="Profil",
        )
        .number(
            attribute_name=CamperNodeCluster.AttributeDefs.button_gpio.name,
            cluster_id=CAMPER_CLUSTER_ID,
            endpoint_id=CONFIG_ENDPOINT,
            min_value=0,
            max_value=23,
            step=1,
            translation_key="button_gpio",
            fallback_name="Taster-Pin",
            entity_type=EntityType.CONFIG,
        )
        .number(
            attribute_name=CamperNodeCluster.AttributeDefs.output_gpio.name,
            cluster_id=CAMPER_CLUSTER_ID,
            endpoint_id=CONFIG_ENDPOINT,
            min_value=0,
            max_value=23,
            step=1,
            translation_key="output_gpio",
            fallback_name="Ausgangs-Pin",
            entity_type=EntityType.CONFIG,
        )
        .number(
            attribute_name=CamperNodeCluster.AttributeDefs.log_level.name,
            cluster_id=CAMPER_CLUSTER_ID,
            endpoint_id=CONFIG_ENDPOINT,
            min_value=0,
            max_value=3,
            step=1,
            translation_key="log_level",
            fallback_name="Log level",
            entity_type=EntityType.CONFIG,
        )
        .sensor(
            attribute_name=CamperNodeCluster.AttributeDefs.uptime_sec.name,
            cluster_id=CAMPER_CLUSTER_ID,
            endpoint_id=CONFIG_ENDPOINT,
            state_class=SensorStateClass.TOTAL_INCREASING,
            device_class=SensorDeviceClass.DURATION,
            translation_key="uptime",
            fallback_name="Uptime",
            entity_type=EntityType.DIAGNOSTIC,
            reporting_config=ReportingConfig(
                min_interval=60, max_interval=300, reportable_change=60
            ),
        )
        .sensor(
            attribute_name=CamperNodeCluster.AttributeDefs.firmware_version.name,
            cluster_id=CAMPER_CLUSTER_ID,
            endpoint_id=CONFIG_ENDPOINT,
            translation_key="firmware_version",
            fallback_name="Firmware version",
            entity_type=EntityType.DIAGNOSTIC,
        )
        .sensor(
            attribute_name=CamperNodeCluster.AttributeDefs.last_rssi.name,
            cluster_id=CAMPER_CLUSTER_ID,
            endpoint_id=CONFIG_ENDPOINT,
            device_class=SensorDeviceClass.SIGNAL_STRENGTH,
            state_class=SensorStateClass.MEASUREMENT,
            translation_key="rssi",
            fallback_name="RSSI",
            entity_type=EntityType.DIAGNOSTIC,
        )
        .command_button(
            command_name=CamperNodeCluster.ServerCommandDefs.reboot.name,
            cluster_id=CAMPER_CLUSTER_ID,
            endpoint_id=CONFIG_ENDPOINT,
            translation_key="restart",
            fallback_name="Restart",
            entity_type=EntityType.CONFIG,
        )
        .command_button(
            command_name=CamperNodeCluster.ServerCommandDefs.factory_reset.name,
            cluster_id=CAMPER_CLUSTER_ID,
            endpoint_id=CONFIG_ENDPOINT,
            translation_key="factory_reset",
            fallback_name="Factory reset",
            entity_type=EntityType.CONFIG,
        )
        .command_button(
            command_name=CamperNodeCluster.ServerCommandDefs.trigger_ota.name,
            cluster_id=CAMPER_CLUSTER_ID,
            endpoint_id=CONFIG_ENDPOINT,
            translation_key="trigger_ota",
            fallback_name="Trigger OTA",
            entity_type=EntityType.CONFIG,
        )
    )


(
    _config_cluster_entities(
        QuirkBuilder("CamperOS", "CamperNode OS").filter(_relay_node_filter)
    )
    .switch(
        attribute_name=OnOff.AttributeDefs.on_off.name,
        cluster_id=OnOff.cluster_id,
        endpoint_id=LOAD_ENDPOINT,
        translation_key="relay",
        fallback_name="Relay",
    )
    .add_to_registry()
)

(
    _config_cluster_entities(
        QuirkBuilder("CamperOS", "CamperNode OS").filter(_pump_node_filter)
    )
    .switch(
        attribute_name=OnOff.AttributeDefs.on_off.name,
        cluster_id=OnOff.cluster_id,
        endpoint_id=LOAD_ENDPOINT,
        translation_key="pump",
        fallback_name="Pump",
    )
    .add_to_registry()
)
