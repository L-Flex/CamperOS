"""GPIO-Karte parsing and validation (mirrors firmware rules)."""

from __future__ import annotations

from .const import BOOT_GPIO, PIN_MAP_MAX_LEN, RESERVED_GPIO


def parse_pin_map(map_str: str) -> tuple[list[int], list[int]]:
    """Return (output_gpios, input_gpios) in map order."""
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
    """True when GPIO-Karte has at least one valid output or input."""
    return validate_pin_map(map_str) is None and bool(map_str.strip())


def validate_pin_map(map_str: str) -> str | None:
    """Return an error message, or None when valid."""
    text = map_str.strip()
    if not text:
        return "GPIO-Karte darf nicht leer sein."
    if len(text) >= PIN_MAP_MAX_LEN:
        return f"GPIO-Karte ist zu lang (max. {PIN_MAP_MAX_LEN - 1} Zeichen)."

    seen: set[int] = set()
    has_role = False

    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if ":" not in part:
            return f"Ungültiger Eintrag: {part!r} (Format: GPIO:O oder GPIO:I)"
        pin_s, role = part.split(":", 1)
        try:
            pin = int(pin_s.strip(), 10)
        except ValueError:
            return f"Ungültige GPIO-Nummer: {pin_s!r}"

        role_u = role.strip().upper()
        if role_u not in {"O", "I"}:
            return f"GPIO {pin}: Rolle muss O (Ausgang) oder I (Eingang) sein."

        if pin in RESERVED_GPIO:
            if pin == 0:
                return "GPIO 0 ist reserviert und darf nicht in der Karte stehen."
            if pin == BOOT_GPIO:
                return (
                    f"GPIO {BOOT_GPIO} (BOOT) ist reserviert — nicht eintragen, "
                    "erscheint immer automatisch als Eingang."
                )
            return f"GPIO {pin} ist reserviert (LED/System)."

        if pin in seen:
            return f"GPIO {pin} ist doppelt in der Karte."
        seen.add(pin)
        has_role = True

    if not has_role:
        return "GPIO-Karte enthält keine gültigen Einträge."

    outputs, inputs = parse_pin_map(text)
    if not outputs and not inputs:
        return "Mindestens ein GPIO als O oder I konfigurieren."

    return None


def decode_zcl_string(value) -> str:
    """Decode ZCL char string from zigpy cache value."""
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    if isinstance(value, (bytes, bytearray)):
        if len(value) >= 1:
            length = value[0]
            return bytes(value[1 : 1 + length]).decode("utf-8", errors="replace")
        return ""
    return str(value)
