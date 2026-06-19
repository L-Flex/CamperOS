"""CamperNode Config — GPIO-Karte text entity per ZHA device."""

from __future__ import annotations

import asyncio
import logging
from typing import Any

from homeassistant.components.text import TextEntity, TextMode
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EntityCategory
from homeassistant.core import Event, HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError, ServiceValidationError
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.event import async_call_later

from .const import (
    ATTR_PIN_MAP,
    CAMPER_CLUSTER_ID,
    CONFIG_ENDPOINT,
    DOMAIN,
    MANUFACTURER,
    MODEL,
    PIN_MAP_MAX_LEN,
    ZHA_DOMAIN,
)
from .pin_map import decode_zcl_string, is_pin_map_valid, validate_pin_map

_LOGGER = logging.getLogger(__name__)

RESCAN_DELAYS = (5, 30, 120)
CLUSTER_TYPE_IN = "in"


def _ieee_from_device(device: dr.DeviceEntry) -> str | None:
    for identifier in device.identifiers:
        if identifier[0] == ZHA_DOMAIN:
            return identifier[1]
    return None


def _is_campernode_device(device: dr.DeviceEntry) -> bool:
    if device.manufacturer != MANUFACTURER or device.model != MODEL:
        return False
    return _ieee_from_device(device) is not None


def _discover_entities(hass: HomeAssistant) -> list[CamperNodePinMapText]:
    entities: list[CamperNodePinMapText] = []
    for device in dr.async_get(hass).devices.values():
        if not _is_campernode_device(device):
            continue
        ieee = _ieee_from_device(device)
        if ieee is None:
            continue
        entities.append(CamperNodePinMapText(hass, device.id, ieee))
    return entities


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Discover GPIO-Karte entities for all CamperNode ZHA devices."""
    known: set[str] = set()

    def _add_new(entities: list[CamperNodePinMapText]) -> None:
        fresh = [e for e in entities if e.ieee not in known]
        for entity in fresh:
            known.add(entity.ieee)
        if fresh:
            _LOGGER.info(
                "GPIO-Karte Entität(en) hinzugefügt: %s",
                ", ".join(e.ieee for e in fresh),
            )
            async_add_entities(fresh, update_before_add=True)

    def _rescan(_now=None) -> None:
        found = _discover_entities(hass)
        if found:
            _add_new(found)
        else:
            _LOGGER.debug(
                "Kein CamperNode gefunden (manufacturer=%s, model=%s)",
                MANUFACTURER,
                MODEL,
            )

    _rescan()

    @callback
    def _device_registry_updated(event: Event) -> None:
        if event.data.get("action") not in ("create", "update"):
            return
        device_id = event.data.get("device_id")
        if device_id is None:
            return
        device = dr.async_get(hass).async_get(device_id)
        if device is None or not _is_campernode_device(device):
            return
        ieee = _ieee_from_device(device)
        if ieee is None or ieee in known:
            return
        _add_new([CamperNodePinMapText(hass, device.id, ieee)])

    entry.async_on_unload(
        hass.bus.async_listen(dr.EVENT_DEVICE_REGISTRY_UPDATED, _device_registry_updated)
    )

    @callback
    def _component_loaded(event: Event) -> None:
        if event.data.get("domain") == ZHA_DOMAIN:
            _rescan()

    entry.async_on_unload(hass.bus.async_listen("component_loaded", _component_loaded))

    @callback
    def _delayed_rescan(_now) -> None:
        _rescan()

    for delay in RESCAN_DELAYS:
        entry.async_on_unload(async_call_later(hass, delay, _delayed_rescan))


class CamperNodePinMapText(TextEntity):
    """Writable GPIO-Karte on the CamperNode device page."""

    _attr_has_entity_name = True
    _attr_name = "GPIO-Karte"
    _attr_entity_category = EntityCategory.CONFIG
    _attr_mode = TextMode.TEXT
    _attr_native_max = PIN_MAP_MAX_LEN - 1
    _attr_icon = "mdi:map"
    _attr_translation_key = "gpio_pin_map"
    _attr_native_value = ""

    def __init__(self, hass: HomeAssistant, device_id: str, ieee: str) -> None:
        self.hass = hass
        self.device_id = device_id
        self.ieee = ieee
        self._attr_unique_id = f"{ieee}_campernode_pin_map"
        self._attr_device_info = DeviceInfo(identifiers={(ZHA_DOMAIN, ieee)})

    @property
    def available(self) -> bool:
        zha_device = self._get_zha_device()
        if zha_device is None:
            return True
        return bool(getattr(zha_device, "available", True))

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        await self.async_update()

    async def async_set_value(self, value: str) -> None:
        normalized = value.strip()
        err = validate_pin_map(normalized)
        if err is not None:
            raise ServiceValidationError(err)

        zha_device = self._get_zha_device()
        if zha_device is None:
            raise HomeAssistantError(
                "CamperNode nicht in ZHA gefunden. Gerät online und gekoppelt?"
            )

        try:
            response = await zha_device.write_zigbee_attribute(
                CONFIG_ENDPOINT,
                CAMPER_CLUSTER_ID,
                "pin_map",
                normalized,
                cluster_type=CLUSTER_TYPE_IN,
            )
        except ValueError as exc:
            raise HomeAssistantError(
                "Cluster 0xfb00 auf Endpoint 10 nicht gefunden — Quirk installiert?"
            ) from exc
        except Exception as exc:
            _LOGGER.exception("GPIO-Karte write failed for %s", self.ieee)
            raise HomeAssistantError(f"GPIO-Karte schreiben fehlgeschlagen: {exc}") from exc

        _raise_on_write_failure(response)

        self._attr_native_value = normalized
        self.async_write_ha_state()
        self.hass.async_create_task(self._async_refresh_gpio_entities())

    async def _async_refresh_gpio_entities(self) -> None:
        """Re-discover GPIO switches after ESP reboot (rejoin skips full init)."""
        for delay in (12, 30, 60):
            await asyncio.sleep(delay)
            zha_device = self._get_zha_device()
            if zha_device is None or not getattr(zha_device, "available", False):
                continue
            cluster = self._get_config_cluster()
            if cluster is None:
                continue
            map_str = decode_zcl_string(cluster.get(ATTR_PIN_MAP))
            if not is_pin_map_valid(map_str):
                try:
                    await cluster.read_attributes([ATTR_PIN_MAP], only_cache=False)
                    map_str = decode_zcl_string(cluster.get(ATTR_PIN_MAP))
                except Exception:  # noqa: BLE001
                    continue
            if not is_pin_map_valid(map_str):
                continue
            try:
                await zha_device.recompute_entities()
                _LOGGER.info(
                    "GPIO-Schalter neu erkannt für %s (Karte: %s)", self.ieee, map_str
                )
                return
            except Exception as exc:  # noqa: BLE001
                _LOGGER.debug("recompute_entities failed for %s: %s", self.ieee, exc)

    async def async_update(self) -> None:
        try:
            value = await self._async_read_pin_map()
        except Exception as exc:  # noqa: BLE001
            _LOGGER.debug("GPIO-Karte update failed for %s: %s", self.ieee, exc)
            return
        if value is not None:
            self._attr_native_value = value

    async def _async_read_pin_map(self) -> str | None:
        cluster = self._get_config_cluster()
        if cluster is None:
            return None

        try:
            await cluster.read_attributes([ATTR_PIN_MAP], only_cache=False)
        except Exception as exc:  # noqa: BLE001
            _LOGGER.debug("pin_map read failed for %s: %s", self.ieee, exc)

        raw = cluster.get(ATTR_PIN_MAP)
        return decode_zcl_string(raw)

    def _get_zha_device_proxy(self) -> Any | None:
        try:
            from homeassistant.components.zha.helpers import (
                async_get_zha_device_proxy,
                get_zha_gateway_proxy,
            )
            from zigpy.types import EUI64
        except ImportError:
            return None

        try:
            return async_get_zha_device_proxy(self.hass, self.device_id)
        except (KeyError, StopIteration, ValueError):
            pass

        try:
            gateway_proxy = get_zha_gateway_proxy(self.hass)
            return gateway_proxy.get_device_proxy(EUI64.convert(self.ieee))
        except (KeyError, ValueError):
            return None

    def _get_zha_device(self) -> Any | None:
        """Return zha.zigbee.device.Device for this CamperNode."""
        try:
            from homeassistant.components.zha.helpers import get_zha_gateway
            from zigpy.types import EUI64
        except ImportError:
            return None
        try:
            return get_zha_gateway(self.hass).get_device(EUI64.convert(self.ieee))
        except (KeyError, ValueError):
            return None

    def _get_config_cluster(self) -> Any | None:
        zha_device = self._get_zha_device()
        if zha_device is None:
            return None
        try:
            return zha_device.async_get_cluster(
                CONFIG_ENDPOINT, CAMPER_CLUSTER_ID, CLUSTER_TYPE_IN
            )
        except KeyError:
            return None


def _raise_on_write_failure(response: Any) -> None:
    """Raise HomeAssistantError when the device rejects the write."""
    if response is None:
        return

    try:
        from zigpy.zcl.foundation import Status
    except ImportError:
        return

    records = response
    if not isinstance(records, list):
        records = getattr(response, "attributes", None) or getattr(response, "record", None)
        if records is None:
            return
        if not isinstance(records, list):
            records = [records]

    for record in records:
        status = getattr(record, "status", None)
        if status is not None and status != Status.SUCCESS:
            name = getattr(status, "name", status)
            raise HomeAssistantError(
                f"ESP32 hat GPIO-Karte abgelehnt ({name}). "
                "GPIO 0/8/9 nicht eintragen. Beispiel: 2:I,3:O,10:I,16:O,20:O,23:O"
            )
