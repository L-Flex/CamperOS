#!/usr/bin/env python3
"""
CamperNode OS — interaktiver Konfigurations-Assistent.

Fragt Schritt für Schritt ab (Name, Profil, GPIO, Log-Level, …) und erzeugt:
  - campernode_config.json          — lesbare Zusammenfassung
  - campernode_zigbee_writes.json   — Attribute + Hex-Payloads
  - apply_campernode_ha.yaml        — Home-Assistant-Skript (ZHA)
  - apply_campernode_zigpy.py       — optional: direkt per zigpy (Coordinator nötig)

Verwendung:
  python tools/campernode_config_wizard.py

Cluster 0xFC00 auf Endpoint 10 — siehe zigbee/include/zigbee_clusters.h
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import traceback
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
DEFAULT_OUT_DIR = PROJECT_ROOT / "campernode_config_output"

CAMPER_CLUSTER_ID = 0xFC00
CONFIG_ENDPOINT = 10

STRAPPING_PINS = {4, 5, 8, 9, 15}
MAX_GPIO_PINS = 24
GPIO_STRUCT = struct.Struct("<BBBBHHB5s")  # gpio_pin_config_t (ESP32)

PROFILES = {
    0: ("relay", "Relais (Schalter, Endpoint 1 = Relay)", True),
    1: ("pump", "Pumpe (Schalter, Endpoint 1 = Pump)", True),
    2: ("light", "Licht (Profil noch nicht implementiert)", False),
    3: ("tank", "Tank (Profil noch nicht implementiert)", False),
    4: ("climate", "Klima (Profil noch nicht implementiert)", False),
    5: ("fan", "Lüfter (Profil noch nicht implementiert)", False),
    6: ("battery", "Batterie (Profil noch nicht implementiert)", False),
    7: ("sensor", "Sensor (Profil noch nicht implementiert)", False),
    8: ("custom", "Custom (Profil noch nicht implementiert)", False),
}

GPIO_FUNCTIONS = {
    0: "unused",
    1: "digital_input",
    2: "digital_output",
    3: "relay",
    4: "button",
    5: "pwm",
    6: "adc",
    7: "ds18b20",
    8: "rgb_led",
    9: "fan",
    10: "valve",
    11: "pump",
    12: "i2c",
    13: "uart",
}

GPIO_FLAGS = {
    "none": 0,
    "invert": 1 << 0,
    "pullup": 1 << 1,
    "pulldown": 1 << 2,
    "open_drain": 1 << 3,
}

LOG_LEVELS = {
    0: "debug",
    1: "info",
    2: "warning",
    3: "error",
}

ATTR = {
    "node_name": 0x0000,
    "profile_id": 0x0001,
    "gpio_config": 0x0002,
    "calibration": 0x0003,
    "log_level": 0x0006,
}

ZB_CMD = {
    "reboot": 0x00,
    "factory_reset": 0x01,
    "trigger_ota": 0x02,
}


@dataclass
class GpioPin:
    pin: int
    function: int
    profile_bind: int
    flags: int = 0
    debounce_ms: int = 50
    pwm_freq_hz: int = 0
    i2c_addr: int = 0

    def to_bytes(self) -> bytes:
        return GPIO_STRUCT.pack(
            self.pin,
            self.function,
            self.flags,
            self.profile_bind,
            self.debounce_ms,
            self.pwm_freq_hz,
            self.i2c_addr,
            b"\x00" * 5,
        )


@dataclass
class CamperConfig:
    node_name: str = "campernode"
    profile_id: int = 0
    log_level: int = 1
    gpio_pins: list[GpioPin] = field(default_factory=list)
    calibration_hex: str = ""
    device_ieee: str = ""
    reboot_after: bool = True


def prompt(text: str, default: str | None = None) -> str:
    suffix = f" [{default}]" if default is not None else ""
    while True:
        try:
            raw = input(f"{text}{suffix}: ").strip()
        except EOFError:
            raise EOFError("Eingabe abgebrochen (Fenster geschlossen oder leere Pipe).") from None
        if raw == "" and default is not None:
            return default
        if raw != "":
            return raw
        if default is None:
            print("  Bitte einen Wert eingeben.")


def prompt_yes_no(text: str, default: bool = True) -> bool:
    default_s = "j" if default else "n"
    while True:
        raw = input(f"{text} (j/n) [{default_s}]: ").strip().lower()
        if raw == "":
            return default
        if raw in ("j", "ja", "y", "yes"):
            return True
        if raw in ("n", "nein", "no"):
            return False
        print("  j oder n bitte.")


def prompt_int(text: str, default: int | None = None, min_v: int | None = None, max_v: int | None = None) -> int:
    while True:
        raw = prompt(text, str(default) if default is not None else None)
        try:
            value = int(raw)
        except ValueError:
            print("  Bitte eine Zahl eingeben.")
            continue
        if min_v is not None and value < min_v:
            print(f"  Mindestens {min_v}.")
            continue
        if max_v is not None and value > max_v:
            print(f"  Höchstens {max_v}.")
            continue
        return value


def prompt_gpio_pin(allow_strapping: bool = False) -> int:
    while True:
        pin = prompt_int("  GPIO-Nummer (ESP32-C6)", min_v=0, max_v=30)
        if pin in STRAPPING_PINS and not allow_strapping:
            print(f"  Warnung: GPIO {pin} ist ein Strapping-Pin ({sorted(STRAPPING_PINS)}).")
            if not prompt_yes_no("  Trotzdem verwenden?", default=False):
                continue
        return pin


def prompt_flags() -> int:
    print("  Flags (mehrere mit Komma, z.B. pullup,invert):")
    print("    none, invert, pullup, pulldown, open_drain")
    raw = prompt("  Flags", "none").lower().replace(" ", "")
    if raw in ("", "none"):
        return 0
    flags = 0
    for part in raw.split(","):
        if part not in GPIO_FLAGS:
            print(f"  Unbekannt: {part}")
            return prompt_flags()
        flags |= GPIO_FLAGS[part]
    return flags


def zcl_char_string(text: str) -> bytes:
    data = text.encode("utf-8")
    if len(data) > 32:
        raise ValueError("Node-Name max. 32 Zeichen")
    return bytes([len(data)]) + data


def zcl_long_octet(payload: bytes) -> bytes:
    if len(payload) > 0xFFFF:
        raise ValueError("Blob zu groß")
    return len(payload).to_bytes(2, "little") + payload


def gpio_blob_raw(pins: list[GpioPin]) -> bytes:
    if len(pins) > MAX_GPIO_PINS:
        raise ValueError(f"Max. {MAX_GPIO_PINS} GPIO-Einträge")
    body = bytes([len(pins)])
    for p in pins:
        body += p.to_bytes()
    return body


def build_zigbee_writes(cfg: CamperConfig) -> list[dict[str, Any]]:
    writes: list[dict[str, Any]] = []

    if cfg.node_name:
        payload = zcl_char_string(cfg.node_name)
        writes.append({
            "name": "node_name",
            "attribute_id": ATTR["node_name"],
            "zcl_type": "char_string",
            "value": cfg.node_name,
            "hex": payload.hex(),
            "reboot_required": False,
        })

    if cfg.gpio_pins:
        raw = gpio_blob_raw(cfg.gpio_pins)
        payload = zcl_long_octet(raw)
        writes.append({
            "name": "gpio_config",
            "attribute_id": ATTR["gpio_config"],
            "zcl_type": "long_octet_string",
            "gpio_count": len(cfg.gpio_pins),
            "hex": payload.hex(),
            "reboot_required": False,
        })

    if cfg.calibration_hex:
        try:
            cal = bytes.fromhex(cfg.calibration_hex.replace(" ", "").replace(":", ""))
        except ValueError as exc:
            raise ValueError(f"Ungueltige Kalibrierungs-Hex: {exc}") from exc
        payload = zcl_long_octet(cal)
        writes.append({
            "name": "calibration",
            "attribute_id": ATTR["calibration"],
            "zcl_type": "long_octet_string",
            "hex": payload.hex(),
            "reboot_required": False,
        })

    writes.append({
        "name": "log_level",
        "attribute_id": ATTR["log_level"],
        "zcl_type": "uint8",
        "value": cfg.log_level,
        "hex": bytes([cfg.log_level]).hex(),
        "reboot_required": False,
    })

    writes.append({
        "name": "profile_id",
        "attribute_id": ATTR["profile_id"],
        "zcl_type": "uint8",
        "value": cfg.profile_id,
        "hex": bytes([cfg.profile_id]).hex(),
        "note": "Profilwechsel → Gerät startet neu; Zigbee-Endpoint 1 ändert sich",
        "reboot_required": True,
    })

    return writes


def wizard_gpio_for_relay_or_pump(cfg: CamperConfig, output_func: int) -> None:
    print("\n--- GPIO: Taster (optional) ---")
    if prompt_yes_no("Physischen Taster verwenden?", default=True):
        print("  Logische ID profile_bind=0 (Standard im Relais/Pumpen-Profil)")
        pin = prompt_gpio_pin()
        flags = prompt_flags()
        if not (flags & GPIO_FLAGS["pullup"]):
            if prompt_yes_no("  Pull-up aktivieren? (üblich bei Taster gegen GND)", default=True):
                flags |= GPIO_FLAGS["pullup"]
        debounce = prompt_int("  Entprellung (ms)", default=50, min_v=1, max_v=5000)
        cfg.gpio_pins.append(
            GpioPin(pin=pin, function=4, profile_bind=0, flags=flags, debounce_ms=debounce)
        )

    label = "Relais/MOSFET" if output_func == 3 else "Pumpen-Ausgang"
    print(f"\n--- GPIO: {label} ---")
    print("  Logische ID profile_bind=1 (Standard im Relais/Pumpen-Profil)")
    pin = prompt_gpio_pin()
    flags = prompt_flags()
    if prompt_yes_no("  Open-Drain (manche MOSFET-Treiber)?", default=False):
        flags |= GPIO_FLAGS["open_drain"]
    if prompt_yes_no("  Signal invertieren?", default=False):
        flags |= GPIO_FLAGS["invert"]
    cfg.gpio_pins.append(
        GpioPin(pin=pin, function=output_func, profile_bind=1, flags=flags, debounce_ms=0)
    )

    while prompt_yes_no("\nWeiteren GPIO hinzufügen?", default=False):
        wizard_extra_gpio(cfg)


def wizard_extra_gpio(cfg: CamperConfig) -> None:
    print("\n  Verfügbare Funktionen:")
    for k, v in GPIO_FUNCTIONS.items():
        if k != 0:
            print(f"    {k:2d} = {v}")
    func = prompt_int("  Funktion (Nummer)", min_v=1, max_v=13)
    pin = prompt_gpio_pin()
    bind = prompt_int("  profile_bind (logische ID für Profil)", min_v=0, max_v=255)
    flags = prompt_flags()
    debounce = 50
    if func == 4:
        debounce = prompt_int("  Entprellung (ms)", default=50, min_v=1, max_v=5000)
    cfg.gpio_pins.append(
        GpioPin(pin=pin, function=func, profile_bind=bind, flags=flags, debounce_ms=debounce)
    )


def run_wizard(out_dir: Path) -> CamperConfig:
    print("=" * 60)
    print(" CamperNode OS — Konfigurations-Assistent")
    print("=" * 60)
    print(f"\nAusgabe-Ordner:\n  {out_dir.resolve()}\n")
    print("Dieses Tool erzeugt Zigbee-Schreibbefehle fuer Cluster 0xFC00")
    print(f"(Endpoint {CONFIG_ENDPOINT}). Senden z.B. ueber Home Assistant ZHA.")
    print("\nTipp: Alle Fragen durchgehen bis zur Zusammenfassung am Ende.")
    print("      Erst dann werden die Dateien geschrieben.\n")

    cfg = CamperConfig()

    print("--- Schritt 1: Knotenname ---")
    cfg.node_name = prompt("Name in NVS / Anzeige", "campernode")

    print("\n--- Schritt 2: Profil (was soll der Chip tun?) ---")
    for pid, (slug, desc, impl) in PROFILES.items():
        mark = "[+]" if impl else "[ ]"
        print(f"  {pid} = {slug:8s} {mark}  {desc}")
    cfg.profile_id = prompt_int("Profil-ID wählen", default=0, min_v=0, max_v=8)
    _slug, _desc, implemented = PROFILES[cfg.profile_id]
    if not implemented:
        print("\n  Hinweis: Dieses Profil ist in der Firmware noch ein Stub.")
        if not prompt_yes_no("  Trotzdem in NVS speichern?", default=False):
            cfg.profile_id = prompt_int("Profil-ID (implementiert: 0=relay, 1=pump)", default=0, min_v=0, max_v=1)

    print("\n--- Schritt 3: Log-Level (Seriell-Log auf dem Chip) ---")
    for lid, name in LOG_LEVELS.items():
        print(f"  {lid} = {name}")
    cfg.log_level = prompt_int("Log-Level", default=1, min_v=0, max_v=3)

    print("\n--- Schritt 4: GPIO-Belegung ---")
    if cfg.profile_id in (0, 1):
        out_func = 3 if cfg.profile_id == 0 else 11  # RELAY or PUMP
        wizard_gpio_for_relay_or_pump(cfg, out_func)
    else:
        if prompt_yes_no("GPIO manuell konfigurieren?", default=True):
            while True:
                wizard_extra_gpio(cfg)
                if not prompt_yes_no("Weiteren GPIO?", default=False):
                    break
        else:
            print("  (Keine GPIO-Einträge — Chip schaltet keine Pins bis zur Konfiguration.)")

    print("\n--- Schritt 5: Kalibrierung (optional) ---")
    if prompt_yes_no("Kalibrierungs-Blob schreiben?", default=False):
        cfg.calibration_hex = prompt("Hex-Bytes (z.B. deadbeef oder leer lassen)", "")

    print("\n--- Schritt 6: Zigbee-Ziel (für Apply-Skripte) ---")
    print("  IEEE des Nodes aus ZHA (Geraet -> Zigbee-Info), Format 00:11:22:...")
    cfg.device_ieee = prompt("IEEE-Adresse (optional, Enter zum Überspringen)", "")

    print("\n--- Schritt 7: Abschluss ---")
    cfg.reboot_after = prompt_yes_no(
        "Nach Profil-Schreiben ist Reboot automatisch — zusätzlich Reboot-Befehl (0x00) anhängen?",
        default=False,
    )

    return cfg


def gpio_pins_summary(pins: list[GpioPin]) -> list[dict[str, Any]]:
    rows = []
    for p in pins:
        rows.append({
            "pin": p.pin,
            "function": GPIO_FUNCTIONS.get(p.function, str(p.function)),
            "profile_bind": p.profile_bind,
            "flags": p.flags,
            "debounce_ms": p.debounce_ms,
        })
    return rows


def generate_ha_yaml(cfg: CamperConfig, writes: list[dict[str, Any]]) -> str:
    lines = [
        "# CamperNode OS — ZHA Attribut-Schreibvorgänge",
        f"# Erzeugt: {datetime.now(timezone.utc).isoformat()}",
        "# In Home Assistant: Entwicklerwerkzeuge → Dienste",
        "# Reihenfolge: GPIO/Name/Log zuerst, Profil zuletzt (löst Reboot aus).",
        "",
        "sequence:",
    ]

    ieee_val = cfg.device_ieee.strip() or "00:00:00:00:00:00:00:00"

    for w in writes:
        attr = w["attribute_id"]
        hex_val = w["hex"]
        lines.append(f"  # {w['name']} (attr 0x{attr:04X}) hex={hex_val}")
        lines.append("  - service: zha.set_zigbee_cluster_attribute")
        lines.append("    data:")
        lines.append(f'      ieee: "{ieee_val}"')
        lines.append(f"      endpoint_id: {CONFIG_ENDPOINT}")
        lines.append(f"      cluster_id: {CAMPER_CLUSTER_ID}")
        lines.append("      cluster_type: in")
        lines.append(f"      attribute: {attr}")
        if w["zcl_type"] == "uint8":
            lines.append(f"      value: {w['value']}")
        else:
            byte_list = ", ".join(f"0x{b:02x}" for b in bytes.fromhex(hex_val))
            lines.append(f"      value: [{byte_list}]")
        lines.append("")

    if cfg.reboot_after:
        lines.append("  # Expliziter Reboot-Befehl (nach Profilwechsel meist unnötig)")
        lines.append("  - service: zha.issue_zigbee_cluster_command")
        lines.append("    data:")
        lines.append(f'      ieee: "{ieee_val}"')
        lines.append(f"      endpoint_id: {CONFIG_ENDPOINT}")
        lines.append(f"      cluster_id: {CAMPER_CLUSTER_ID}")
        lines.append(f"      command: {ZB_CMD['reboot']}")
        lines.append("      command_type: client")
        lines.append("      args: []")
        lines.append("")

    return "\n".join(lines)


def generate_zigpy_script(cfg: CamperConfig) -> str:
    ieee = cfg.device_ieee.strip() or "00:00:00:00:00:00:00:00"
    return f'''#!/usr/bin/env python3
"""
CamperNode OS — Zigbee-Konfiguration (Referenz).

Lädt campernode_zigbee_writes.json aus demselben Ordner.
Für echtes Senden: apply_campernode_ha.yaml in Home Assistant verwenden.

Optional: Dieses Skript mit zigpy + deinem Radio erweitern.
"""

import json
from pathlib import Path

DEVICE_IEEE = "{ieee}"
ENDPOINT = {CONFIG_ENDPOINT}
CLUSTER_ID = {CAMPER_CLUSTER_ID}

WRITES = json.loads(
    Path(__file__).with_name("campernode_zigbee_writes.json").read_text(encoding="utf-8")
)


def main() -> None:
    print("CamperNode Zigbee writes für IEEE", DEVICE_IEEE)
    for w in WRITES:
        print(f"  {{w['name']:14s}} attr=0x{{w['attribute_id']:04X}}  {{w['hex']}}")
    print("\\n-> In Home Assistant: apply_campernode_ha.yaml ausfuehren.")


if __name__ == "__main__":
    main()
'''


def print_summary(cfg: CamperConfig, writes: list[dict[str, Any]], out_dir: Path) -> None:
    print("\n" + "=" * 60)
    print(" Zusammenfassung")
    print("=" * 60)
    slug = PROFILES[cfg.profile_id][0]
    print(f"  Name:     {cfg.node_name}")
    print(f"  Profil:   {cfg.profile_id} ({slug})")
    print(f"  Log:      {LOG_LEVELS[cfg.log_level]}")
    print(f"  GPIO:     {len(cfg.gpio_pins)} Eintrag/äge")
    for row in gpio_pins_summary(cfg.gpio_pins):
        print(f"    - GPIO {row['pin']}: {row['function']}, bind={row['profile_bind']}")
    print(f"\n  Dateien in: {out_dir.resolve()}")
    print("\n  Zigbee-Schreibvorgänge (Reihenfolge beachten):")
    for w in writes:
        reboot = " [REBOOT]" if w.get("reboot_required") else ""
        print(f"    0x{w['attribute_id']:04X} {w['name']:14s} hex={w['hex']}{reboot}")


def write_output_files(cfg: CamperConfig, writes: list[dict[str, Any]], out_dir: Path) -> list[Path]:
    out_dir.mkdir(parents=True, exist_ok=True)

    summary = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "node_name": cfg.node_name,
        "profile_id": cfg.profile_id,
        "profile": PROFILES[cfg.profile_id][0],
        "log_level": cfg.log_level,
        "log_level_name": LOG_LEVELS[cfg.log_level],
        "device_ieee": cfg.device_ieee,
        "gpio": gpio_pins_summary(cfg.gpio_pins),
        "calibration_hex": cfg.calibration_hex or None,
        "zigbee": {
            "endpoint": CONFIG_ENDPOINT,
            "cluster_id": f"0x{CAMPER_CLUSTER_ID:04X}",
        },
    }

    files = {
        "campernode_config.json": json.dumps(summary, indent=2, ensure_ascii=False),
        "campernode_zigbee_writes.json": json.dumps(writes, indent=2, ensure_ascii=False),
        "apply_campernode_ha.yaml": generate_ha_yaml(cfg, writes),
        "apply_campernode_zigpy.py": generate_zigpy_script(cfg),
    }

    written: list[Path] = []
    for name, content in files.items():
        path = out_dir / name
        path.write_text(content, encoding="utf-8")
        written.append(path.resolve())
    return written


def example_config_pump() -> CamperConfig:
    """Beispiel-Konfiguration ohne interaktive Eingabe."""
    return CamperConfig(
        node_name="camper-pumpe",
        profile_id=1,
        log_level=1,
        gpio_pins=[
            GpioPin(pin=10, function=4, profile_bind=0, flags=GPIO_FLAGS["pullup"], debounce_ms=50),
            GpioPin(pin=6, function=11, profile_bind=1, flags=0, debounce_ms=0),
        ],
        device_ieee="",
        reboot_after=False,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="CamperNode OS Zigbee-Konfigurations-Assistent")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUT_DIR,
        help=f"Zielordner (Standard: {DEFAULT_OUT_DIR})",
    )
    parser.add_argument(
        "--quick",
        action="store_true",
        help="Beispiel-Pumpen-Konfiguration ohne Fragen schreiben (zum Testen)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    out_dir = args.output_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    try:
        if args.quick:
            print(f"Schnellmodus: Beispiel-Konfiguration -> {out_dir}")
            cfg = example_config_pump()
        else:
            cfg = run_wizard(out_dir)
    except (KeyboardInterrupt, EOFError) as exc:
        print(f"\nAbgebrochen: {exc}")
        print(f"\nKeine Dateien geschrieben. Ordner waere gewesen:\n  {out_dir}")
        return 1
    except Exception as exc:
        print(f"\nFehler: {exc}")
        traceback.print_exc()
        return 1

    try:
        writes = build_zigbee_writes(cfg)
        written = write_output_files(cfg, writes, out_dir)
    except Exception as exc:
        print(f"\nFehler beim Erzeugen der Dateien: {exc}")
        traceback.print_exc()
        return 1

    print_summary(cfg, writes, out_dir)

    print("\n--- Geschriebene Dateien ---")
    for path in written:
        print(f"  {path}")

    print("\n--- Naechste Schritte ---")
    print("  1. IEEE in apply_campernode_ha.yaml eintragen (falls noch leer)")
    print("  2. In HA: Entwicklerwerkzeuge -> Dienste -> YAML-Skript ausfuehren")
    print("  3. Nach Profilwechsel (pump/relay): ggf. in ZHA neu pairen")
    print("  4. Schalter auf Endpoint 1 testen (Relay oder Pump)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
