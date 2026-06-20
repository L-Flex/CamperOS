# CamperNode OTA-Firmware (GitHub / ZHA)

Zigbee-OTA-Images für **CamperNode OS** (Manufacturer `0x147B`, Image Type `0xC602`).

| Datei | Version | Hex |
|-------|---------|-----|
| `147b-c602-00000200-campernode.ota` | **0.2.0** | `0x00000200` |
| `147b-c602-00000201-campernode.ota` | **0.2.1 Probe** | `0x00000201` |

Die **0.2.1 Probe** nutzt dasselbe Binary wie 0.2.0 — nur zum Testen des OTA-Update-Wegs in Home Assistant.

## Home Assistant (automatische Updates von GitHub)

`configuration.yaml`:

```yaml
zha:
  enable_quirks: true
  custom_quirks_path: /config/custom_zha_quirks
  zigpy_config:
    ota:
      extra_providers:
        - type: zigpy_remote
          url: https://raw.githubusercontent.com/L-Flex/CamperOS/main/firmware/ota-index.json
          manufacturer_ids: [0x147B]
```

Nach Push auf GitHub und HA-Neustart erscheinen Updates an der **Firmware**-Kachel am Gerät.

Optional parallel lokal (ohne GitHub):

```yaml
        - type: advanced
          path: /config/zha_ota
          warning: >-
            I understand I can *destroy* my devices by enabling OTA updates from files.
            Some OTA updates can be mistakenly applied to the wrong device, breaking it.
            I am consciously using this at my own risk.
```

`.ota`-Dateien aus diesem Ordner nach `/config/zha_ota/` kopieren.

## Neue Version veröffentlichen

```powershell
cd F:\CamperOS\Esp32
idf.py build
python tools\publish_firmware.py --version "0.2.2:CamperNode OS 0.2.2 — Beschreibung"
git add firmware/
git commit -m "firmware: release 0.2.2"
git push
```

Mehrere Versionen in einem Lauf:

```powershell
python tools\publish_firmware.py --version "0.2.0:Release 0.2.0" --version "0.2.1:Release 0.2.1"
```

Probe-Release (Update-Test, gleiches Binary):

```powershell
python tools\publish_firmware.py --probe
```

Das Script schreibt `.ota`-Dateien und aktualisiert `ota-index.json` inkl. SHA512-Checksum.
