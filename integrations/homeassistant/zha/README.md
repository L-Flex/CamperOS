# Home Assistant ZHA — CamperNode OS

CamperNode uses **ZHA** (Zigbee Home Automation) directly — no MQTT bridge required.

## Schnellstart

1. **Quirk** installieren: `campernode.py` → `/config/custom_zha_quirks/`
2. **GPIO-Karte-Integration** installieren: `campernode_config/` → `/config/custom_components/campernode_config/`  
   (Details: [campernode_config/README.md](../campernode_config/README.md))
3. In `configuration.yaml` nur den Quirk-Pfad (kein `campernode_config:`):

```yaml
zha:
  enable_quirks: true
  custom_quirks_path: /config/custom_zha_quirks/
```

4. Home Assistant neu starten, CamperNode koppeln
5. **Einstellungen → Geräte & Dienste → Integration hinzufügen → „CamperNode Config“**
6. Auf der Geräteseite **GPIO-Karte** eintragen (z. B. `2:I,3:O,10:I,16:O,20:O,23:O`)
7. ~30 s warten → am Gerät **Neu konfigurieren** → GPIO-Schalter/Eingänge prüfen

## GPIO-Karte

| Syntax | Bedeutung |
|--------|-----------|
| `3:O` | GPIO 3 = Ausgang (Schalter) |
| `10:I` | GPIO 10 = Eingang (Binary Sensor) |

Nicht eintragen: **GPIO 0**, **GPIO 8** (LED), **GPIO 9** (BOOT — erscheint **immer** als Eingang, nicht in Karte eintragen).

## Entitäten (nach gültiger GPIO-Karte + Neu konfigurieren)

| Entität | Beschreibung |
|---------|--------------|
| **GPIO-Karte** | Pin-Belegung (Textfeld) |
| **GPIO 3**, **GPIO 16**, … | Schalter pro `:O`-Pin |
| **GPIO 9 BOOT**, **GPIO 2**, … | Binary Sensor — BOOT immer, weitere pro `:I`-Pin |
| Temperatur aktiv, Log level, Uptime, … | Konfiguration / Diagnose |

## Mehrere ESP32

Pro CamperNode erscheint automatisch eine eigene **GPIO-Karte** auf der jeweiligen Geräteseite — kein Entwicklerwerkzeug nötig.

## Pairing

1. ZHA → **Gerät hinzufügen** → Join erlauben
2. CamperNode einschalten (Network Steering)

## Contributing upstream

If this quirk works well on your hardware, consider opening a PR to [zha-device-handlers](https://github.com/zigpy/zha-device-handlers).
