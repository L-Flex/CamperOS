"""ZHA quirk for CamperNode OS (custom cluster 0xFB00 on endpoint 10)."""

from __future__ import annotations

import logging
from typing import Any, Final

import attrs
import zigpy.types as t
from zigpy.quirks import CustomCluster
from zigpy.quirks.v2 import (
    BinarySensorMetadata,
    CustomDeviceV2,
    EntityType,
    QuirkBuilder,
    ReportingConfig,
    SensorDeviceClass,
    SensorStateClass,
    SwitchMetadata,
)
from zigpy.zcl.clusters.general import OnOff
from zigpy.zcl.foundation import BaseAttributeDefs, BaseCommandDefs, ZCLAttributeDef, ZCLCommandDef

CAMPER_CLUSTER_ID = 0xFB00
CONFIG_ENDPOINT = 10
LOAD_ENDPOINT = 1
HA_ON_OFF_LIGHT_DEVICE_ID = 0x0100
BOOT_GPIO = 9
MAX_IO = 16
PIN_MAP_ATTR_ID = 0x000C

_LOGGER = logging.getLogger(__name__)


def decode_zcl_string(value: Any) -> str:
    """Decode ZCL char string or plain str/bytes from zigpy."""
    if value is None:
        return ""
    if isinstance(value, t.CharacterString):
        return str(value)
    if isinstance(value, str):
        return value
    if isinstance(value, (bytes, bytearray)):
        if len(value) >= 1:
            length = value[0]
            payload = value[1 : 1 + length]
            return payload.decode("utf-8", errors="replace")
        return ""
    return str(value)


def encode_zcl_char_string(value: Any) -> t.CharacterString:
    """Encode a GPIO-Karte string for ZCL char_string attribute writes."""
    if isinstance(value, t.CharacterString):
        return value
    if isinstance(value, (list, tuple)):
        raw = bytes(int(b) & 0xFF for b in value)
        if raw:
            parsed, _ = t.CharacterString.deserialize(raw)
            return parsed
        return t.CharacterString("")
    if isinstance(value, (bytes, bytearray)):
        if value:
            parsed, _ = t.CharacterString.deserialize(bytes(value))
            return parsed
        return t.CharacterString("")
    return t.CharacterString(decode_zcl_string(value))


def parse_pin_map(map_str: str) -> tuple[list[int], list[int]]:
    """Return (output_gpios, input_gpios) in GPIO-Karte order."""
    outputs: list[int] = []
    inputs: list[int] = []
    for part in map_str.split(","):
        part = part.strip()
        if not part or ":" not in part:
            continue
        pin_s, role = part.split(":", 1)
        try:
            pin = int(pin_s.strip(), 10)
        except ValueError:
            continue
        role_u = role.strip().upper()
        if role_u == "O":
            outputs.append(pin)
        elif role_u == "I":
            inputs.append(pin)
    return outputs, inputs


def is_pin_map_valid(map_str: str) -> bool:
    """True when GPIO-Karte has at least one configured output or input."""
    if not map_str.strip():
        return False
    outputs, inputs = parse_pin_map(map_str)
    return bool(outputs or inputs)


def io_slot_counts(map_str: str) -> tuple[int, int]:
    """Return (num_switches, num_binary_sensors) for a valid GPIO-Karte."""
    outputs, inputs = parse_pin_map(map_str)
    num_switches = len(outputs)
    # input_1 = GPIO 9 BOOT (always); input_2+ = configured :I pins.
    num_binary = 1 + len(inputs)
    return num_switches, num_binary


def gpio_name_maps_from_pin_map(
    map_str: str,
) -> tuple[dict[int, str], dict[int, str]]:
    """Build display names keyed by bitmap index (0-based)."""
    outputs, inputs = parse_pin_map(map_str)
    out_names = {idx: f"GPIO {pin}" for idx, pin in enumerate(outputs)}
    in_names: dict[int, str] = {0: f"GPIO {BOOT_GPIO} BOOT"}
    for idx, pin in enumerate(inputs):
        in_names[idx + 1] = f"GPIO {pin}"
    return out_names, in_names


def _rebuild_cluster_attribute_registry(cluster_cls: type[CamperNodeCluster]) -> None:
    """Re-sync zigpy lookup tables after dynamic AttributeDefs changes."""
    cluster_cls._attributes_by_id = {}
    for attr_def in cluster_cls.AttributeDefs:
        if attr_def.id not in cluster_cls._attributes_by_id:
            cluster_cls._attributes_by_id[attr_def.id] = {True: {}, False: {}, None: {}}
        is_manuf = attr_def.is_manufacturer_specific
        cluster_cls._attributes_by_id[attr_def.id][is_manuf][
            attr_def.manufacturer_code
        ] = attr_def

    cluster_cls.attributes = {attr.id: attr for attr in cluster_cls.AttributeDefs}
    cluster_cls.attributes_by_name = {attr.name: attr for attr in cluster_cls.AttributeDefs}


def _register_io_attributes(cluster_cls: type[CamperNodeCluster]) -> None:
    """Register output_1..N and input_1..N ZCL attribute defs."""
    for i in range(1, MAX_IO + 1):
        out_name = f"output_{i}"
        in_name = f"input_{i}"
        setattr(
            cluster_cls.AttributeDefs,
            out_name,
            ZCLAttributeDef(
                id=0x0100 + i - 1, type=t.Bool, access="rw", name=out_name
            ),
        )
        setattr(
            cluster_cls.AttributeDefs,
            in_name,
            ZCLAttributeDef(
                id=0x0110 + i - 1, type=t.Bool, access="r", name=in_name
            ),
        )
    cluster_cls._OUTPUT_ATTRS = tuple(
        getattr(cluster_cls.AttributeDefs, f"output_{i}") for i in range(1, MAX_IO + 1)
    )
    cluster_cls._INPUT_ATTRS = tuple(
        getattr(cluster_cls.AttributeDefs, f"input_{i}") for i in range(1, MAX_IO + 1)
    )
    _rebuild_cluster_attribute_registry(cluster_cls)


class CamperNodeCluster(CustomCluster):
    """CamperNode configuration cluster — mirrors firmware zigbee_clusters.h."""

    cluster_id: Final[t.uint16_t] = CAMPER_CLUSTER_ID
    name: str = "CamperNode configuration"

    class AttributeDefs(BaseAttributeDefs):
        node_name: Final = ZCLAttributeDef(id=0x0000, type=t.CharacterString, access="rw")
        gpio_config: Final = ZCLAttributeDef(id=0x0002, type=t.LVBytes, access="rw")
        calibration: Final = ZCLAttributeDef(id=0x0003, type=t.LVBytes, access="rw")
        uptime_sec: Final = ZCLAttributeDef(id=0x0004, type=t.uint32_t, access="r")
        last_rssi: Final = ZCLAttributeDef(id=0x0005, type=t.int8s, access="r")
        log_level: Final = ZCLAttributeDef(id=0x0006, type=t.uint8_t, access="rw")
        firmware_version: Final = ZCLAttributeDef(
            id=0x0007, type=t.CharacterString, access="r"
        )
        button_gpio: Final = ZCLAttributeDef(id=0x0008, type=t.uint8_t, access="rw")
        output_gpio: Final = ZCLAttributeDef(id=0x0009, type=t.uint8_t, access="rw")
        feature_flags: Final = ZCLAttributeDef(id=0x000A, type=t.uint8_t, access="rw")
        temp_gpio: Final = ZCLAttributeDef(id=0x000B, type=t.uint8_t, access="r")
        pin_map: Final = ZCLAttributeDef(id=0x000C, type=t.CharacterString, access="rw")
        output_state: Final = ZCLAttributeDef(id=0x000E, type=t.uint16_t, access="rw")
        input_state: Final = ZCLAttributeDef(id=0x000F, type=t.uint16_t, access="r")

    class ServerCommandDefs(BaseCommandDefs):
        reboot: Final = ZCLCommandDef(id=0x00, schema={})
        factory_reset: Final = ZCLCommandDef(id=0x01, schema={})
        trigger_ota: Final = ZCLCommandDef(id=0x02, schema={})

    _OUTPUT_ATTRS: tuple[ZCLAttributeDef, ...] = ()
    _INPUT_ATTRS: tuple[ZCLAttributeDef, ...] = ()

    def _sync_virtual_io_from_bitmaps(self) -> None:
        out = int(self.get(self.AttributeDefs.output_state.id, 0) or 0)
        inp = int(self.get(self.AttributeDefs.input_state.id, 0) or 0)
        for idx, attr in enumerate(self._OUTPUT_ATTRS):
            self._update_attribute(attr.id, bool(out & (1 << idx)))
        for idx, attr in enumerate(self._INPUT_ATTRS):
            self._update_attribute(attr.id, bool(inp & (1 << idx)))

    def _update_attribute(self, attrid: int, value: Any) -> None:
        super()._update_attribute(attrid, value)
        if attrid in (
            self.AttributeDefs.output_state.id,
            self.AttributeDefs.input_state.id,
        ):
            self._sync_virtual_io_from_bitmaps()

    async def write_attributes(
        self,
        attributes: dict[str | int | ZCLAttributeDef, Any],
        **kwargs: Any,
    ) -> list:
        out_state = int(self.get(self.AttributeDefs.output_state.id, 0) or 0)
        forwarded: dict[str | int | ZCLAttributeDef, Any] = {}
        touched_output = False

        for attr, value in attributes.items():
            if attr is None:
                raise ValueError(
                    "Missing cluster attribute id — use attribute 12 (pin_map) "
                    "in zha.set_zigbee_cluster_attribute"
                )
            attr_def = self.find_attribute(attr)
            bit_index = next(
                (i for i, a in enumerate(self._OUTPUT_ATTRS) if a.id == attr_def.id),
                None,
            )
            if bit_index is not None:
                touched_output = True
                if value:
                    out_state |= 1 << bit_index
                else:
                    out_state &= ~(1 << bit_index)
                continue
            if attr_def.id in {a.id for a in self._INPUT_ATTRS}:
                continue
            if attr_def.id == self.AttributeDefs.pin_map.id:
                forwarded[attr] = encode_zcl_char_string(value)
                continue
            if attr_def.id == self.AttributeDefs.node_name.id:
                forwarded[attr] = encode_zcl_char_string(value)
                continue
            forwarded[attr] = value

        if touched_output:
            forwarded[self.AttributeDefs.output_state.id] = out_state

        result = await super().write_attributes(forwarded, **kwargs)
        self._sync_virtual_io_from_bitmaps()
        return result

    async def apply_custom_configuration(self) -> None:
        """Read GPIO-Karte from device before ZHA creates switch entities."""
        attrs_to_read = [
            self.AttributeDefs.pin_map.id,
            self.AttributeDefs.output_state.id,
            self.AttributeDefs.input_state.id,
        ]
        try:
            await self.read_attributes(attrs_to_read, only_cache=False)
        except Exception as exc:  # noqa: BLE001 — zigpy errors vary
            _LOGGER.warning("CamperNode: GPIO-Karte read failed: %s", exc)
        self._sync_virtual_io_from_bitmaps()
        map_str = decode_zcl_string(self.get(self.AttributeDefs.pin_map.id, ""))
        if is_pin_map_valid(map_str):
            _LOGGER.info("CamperNode GPIO-Karte: %s", map_str)
        else:
            _LOGGER.debug("CamperNode GPIO-Karte empty or invalid at configure time")
        await super().apply_custom_configuration()


_register_io_attributes(CamperNodeCluster)


class CamperNodeDevice(CustomDeviceV2):
    """Expose only GPIO-Karte I/O slots; names from configured pins."""

    def __init__(self, *args, **kwargs):
        # zigpy appends entity metadata during super().__init__; must not wrap then.
        self._exposes_metadata_building = True
        super().__init__(*args, **kwargs)
        self._exposes_metadata_building = False

    def _config_cluster(self) -> CamperNodeCluster | None:
        ep = self.endpoints.get(CONFIG_ENDPOINT)
        if ep is None:
            return None
        return ep.in_clusters.get(CAMPER_CLUSTER_ID)

    def _pin_map_str(self) -> str:
        cluster = self._config_cluster()
        if cluster is None:
            return ""
        raw = cluster.get(CamperNodeCluster.AttributeDefs.pin_map.id, b"")
        return decode_zcl_string(raw)

    def _io_layout(self) -> tuple[int, int, dict[int, str], dict[int, str]] | None:
        map_str = self._pin_map_str()
        if not is_pin_map_valid(map_str):
            return None
        num_out, num_in = io_slot_counts(map_str)
        out_names, in_names = gpio_name_maps_from_pin_map(map_str)
        return num_out, num_in, out_names, in_names

    def _filtered_exposes_metadata(self):
        layout = self._io_layout()
        filtered: dict[tuple[int, int, Any], list[Any]] = {}

        for key, metas in self._exposes_metadata.items():
            new_metas = []
            for meta in metas:
                if meta is None:
                    continue
                patched = self._filter_io_entity(meta, layout)
                if patched is not None:
                    new_metas.append(patched)
            if new_metas:
                filtered[key] = new_metas
        return filtered

    @property
    def exposes_metadata(self):
        if getattr(self, "_exposes_metadata_building", False):
            return self._exposes_metadata
        return self._filtered_exposes_metadata()

    @staticmethod
    def _filter_io_entity(meta, layout):
        attr_name = getattr(meta, "attribute_name", None)

        if isinstance(meta, SwitchMetadata) and attr_name and attr_name.startswith(
            "output_"
        ):
            slot = int(attr_name.split("_", 1)[1])
            if layout is None:
                return None
            num_out, _, out_names, _ = layout
            if slot > num_out:
                return None
            return attrs.evolve(meta, fallback_name=out_names[slot - 1])

        if isinstance(meta, BinarySensorMetadata) and attr_name and attr_name.startswith(
            "input_"
        ):
            slot = int(attr_name.split("_", 1)[1])
            # GPIO 9 BOOT is always input_1 (bitmap bit 0), even without :I in GPIO-Karte.
            if slot == 1:
                return attrs.evolve(meta, fallback_name=f"GPIO {BOOT_GPIO} BOOT")
            if layout is None:
                return None
            _, num_in, _, in_names = layout
            if slot > num_in:
                return None
            return attrs.evolve(meta, fallback_name=in_names[slot - 1])

        return meta


def _relay_node_filter(device) -> bool:
    ep = device.endpoints.get(LOAD_ENDPOINT)
    if ep is None:
        return False
    return ep.device_type == HA_ON_OFF_LIGHT_DEVICE_ID


def _add_io_entities(builder: QuirkBuilder) -> QuirkBuilder:
    for i in range(1, MAX_IO + 1):
        builder = builder.switch(
            attribute_name=f"output_{i}",
            cluster_id=CAMPER_CLUSTER_ID,
            endpoint_id=CONFIG_ENDPOINT,
            translation_key="gpio_output",
            fallback_name=f"Ausgang {i}",
            translation_placeholders={"n": str(i)},
            entity_type=EntityType.STANDARD,
        ).binary_sensor(
            attribute_name=f"input_{i}",
            cluster_id=CAMPER_CLUSTER_ID,
            endpoint_id=CONFIG_ENDPOINT,
            translation_key="gpio_input",
            fallback_name=f"Eingang {i}",
            translation_placeholders={"n": str(i)},
            unique_id_suffix=f"input_{i}",
            entity_type=EntityType.STANDARD,
        )
    return builder


def _config_cluster_entities(builder: QuirkBuilder) -> QuirkBuilder:
    builder = (
        builder.replaces(CamperNodeCluster, endpoint_id=CONFIG_ENDPOINT)
        .prevent_default_entity_creation(
            endpoint_id=LOAD_ENDPOINT,
            cluster_id=OnOff.cluster_id,
        )
        # GPIO-Karte: use custom integration campernode_config (text entity per device).
        # See integrations/homeassistant/campernode_config/
        .switch(
            attribute_name=CamperNodeCluster.AttributeDefs.feature_flags.name,
            cluster_id=CAMPER_CLUSTER_ID,
            endpoint_id=CONFIG_ENDPOINT,
            on_value=1,
            off_value=0,
            translation_key="temperature_active",
            fallback_name="Temperatur aktiv",
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
    return _add_io_entities(builder)


(
    _config_cluster_entities(
        QuirkBuilder("CamperOS", "CamperNode OS")
        .filter(_relay_node_filter)
        .device_class(CamperNodeDevice)
    ).add_to_registry()
)
