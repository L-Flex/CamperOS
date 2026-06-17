# Home Assistant ZHA — CamperNode OS

CamperNode uses **ZHA** (Zigbee Home Automation) directly — no MQTT bridge required.

## Einfache Einrichtung (ohne Code)

**Für alle ohne Programmierkenntnisse:**

1. Doppelklick auf `tools/einrichtung.bat` (Windows) oder `tools/einrichtung.html` im Browser öffnen
2. Die Schritte durchgehen — am Ende steht, welche Werte du in Home Assistant setzen musst
3. Nach dem Pairing im Gerät **CamperNode OS** diese Entitäten nutzen:

| Entität | Was du einstellst |
|---------|-------------------|
| **Profil** | Relais oder Pumpe |
| **Taster-Pin** | GPIO-Nummer des Tasters (0 = kein Taster) |
| **Ausgangs-Pin** | GPIO-Nummer für Relais/Pumpe |
| **Relais** / **Pumpe** | Schalter zum Testen |

Kein YAML, keine Hex-Daten, kein Python nötig.

> **Firmware:** GPIO-Pins per ZHA setzen funktioniert ab Firmware mit Attributen `0x0008` / `0x0009`. Nach dem Flashen der aktuellen Version neu pairen oder HA neu starten, damit die Quirk die neuen Entitäten lädt.

## Install the custom quirk

1. On your Home Assistant host, create a folder, e.g. `/config/custom_zha_quirks/`
2. Copy `campernode.py` into that folder
3. Add to `configuration.yaml`:

```yaml
zha:
  enable_quirks: true
  custom_quirks_path: /config/custom_zha_quirks/
```

4. Restart Home Assistant

You should see *"Loaded custom quirks"* in the logs.

## Pairing

1. ZHA → **Add device** → permit join
2. Power the CamperNode; it joins automatically (network steering)

## Entities

| Endpoint | What ZHA creates |
|----------|------------------|
| **1** | Standard **switch** (HA on/off light — relay) or **Pump** switch (on/off output device type) |
| **10** | Profile, **Taster-Pin**, **Ausgangs-Pin**, log level, uptime, firmware, RSSI, restart / factory reset / OTA buttons |

Endpoint 1 works without the quirk. The quirk is needed for manufacturer cluster `0xFC00` on endpoint 10.

Advanced users can still write the raw `gpio_config` blob via **ZHA → Manage Zigbee device → Clusters**, or use `tools/campernode_config_wizard.py` for complex GPIO layouts.

## Contributing upstream

If this quirk works well on your hardware, consider opening a PR to [zha-device-handlers](https://github.com/zigpy/zha-device-handlers) so other users get it built-in.
