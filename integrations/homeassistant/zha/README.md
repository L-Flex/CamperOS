# Home Assistant ZHA — CamperNode OS

CamperNode uses **ZHA** (Zigbee Home Automation) directly — no MQTT bridge required.

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
| **1** | Standard **switch** (HA on/off cluster — relay) |
| **10** | Profile select, log level, uptime, firmware, RSSI, restart / factory reset / OTA buttons |

Endpoint 1 works without the quirk. The quirk is needed for manufacturer cluster `0xFC00` on endpoint 10.

GPIO and calibration blobs are writable ZCL attributes but not exposed as HA entities yet (use **ZHA → Manage Zigbee device → Clusters** to read/write them, or extend the quirk).

## Contributing upstream

If this quirk works well on your hardware, consider opening a PR to [zha-device-handlers](https://github.com/zigpy/zha-device-handlers) so other users get it built-in.
