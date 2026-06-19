# CamperNode Config — GPIO-Karte in Home Assistant

Schreibbares **GPIO-Karte**-Textfeld auf der CamperNode-Geräteseite.

## Installation

1. Ordner `campernode_config` nach `/config/custom_components/campernode_config/` kopieren  
   (nur `.py`, `manifest.json`, `strings.json`, `translations/` — **kein** `__pycache__`)
2. Quirk: `campernode.py` → `/config/custom_zha_quirks/`
3. **Home Assistant neu starten**
4. **Einstellungen → Geräte & Dienste → Integration hinzufügen → „CamperNode Config“**

**Kein Eintrag in `configuration.yaml` nötig** — nur über die Integration-Oberfläche einrichten.

## Nutzung

1. CamperNode in ZHA koppeln
2. Gerät **CamperNode OS** → **Konfiguration** → **GPIO-Karte**
3. z. B. `2:I,3:O,10:I,16:O,20:O,23:O` eintragen und speichern
4. ESP startet neu → **Neu konfigurieren** am Gerät (ZHA-Zahnrad)

## Format

| Teil | Bedeutung |
|------|-----------|
| `3:O` | GPIO 3 als Ausgang |
| `10:I` | GPIO 10 als Eingang |

Nicht eintragen: GPIO 0, GPIO 8 (LED), GPIO 9 (BOOT).
