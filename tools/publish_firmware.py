#!/usr/bin/env python3
"""Build OTA image(s) into firmware/ and regenerate ota-index.json for GitHub + ZHA."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE_DIR = ROOT / "firmware"
DEFAULT_BIN = ROOT / "build" / "campernode_os.bin"
CREATE_OTA = ROOT / "tools" / "create_zha_ota.py"
INDEX_PATH = FIRMWARE_DIR / "ota-index.json"

# GitHub raw base — branch main, repo root = Esp32 project
GITHUB_RAW_BASE = (
    "https://raw.githubusercontent.com/L-Flex/CamperOS/main/firmware"
)

MANUFACTURER_ID = 0x147B
IMAGE_TYPE = 0xC602


def semver_to_file_version(version: str) -> int:
    parts = version.split(".")
    if len(parts) != 3:
        raise ValueError(f"expected semver major.minor.patch, got {version!r}")
    major, minor, patch = (int(p) for p in parts)
    return (major << 16) | (minor << 8) | patch


def ota_filename(file_version: int) -> str:
    return f"{MANUFACTURER_ID:04x}-{IMAGE_TYPE:04x}-{file_version:08x}-campernode.ota"


def build_ota(bin_path: Path, version: str, changelog: str) -> Path:
    FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
    file_version = semver_to_file_version(version)
    out = FIRMWARE_DIR / ota_filename(file_version)
    cmd = [
        sys.executable,
        str(CREATE_OTA),
        str(bin_path),
        "--version",
        version,
        "-o",
        str(out),
    ]
    subprocess.run(cmd, check=True)
    return out


def index_entry(ota_path: Path, file_version: int, changelog: str) -> dict:
    data = ota_path.read_bytes()
    return {
        "binary_url": f"{GITHUB_RAW_BASE}/{ota_path.name}",
        "manufacturer_id": MANUFACTURER_ID,
        "image_type": IMAGE_TYPE,
        "file_version": file_version,
        "file_size": len(data),
        "checksum": "sha512:" + hashlib.sha512(data).hexdigest(),
        "changelog": changelog,
    }


def write_index(entries: list[dict]) -> None:
    entries.sort(key=lambda e: e["file_version"])
    payload = {"firmwares": entries}
    INDEX_PATH.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--bin",
        type=Path,
        default=DEFAULT_BIN,
        help="ESP-IDF app binary (default: build/campernode_os.bin)",
    )
    parser.add_argument(
        "--version",
        action="append",
        dest="versions",
        metavar="SEMVER:CHANGELOG",
        help='Release e.g. "0.2.0:CamperNode OS 0.2.0 (MOSFET :Od)" — repeat for multiple',
    )
    parser.add_argument(
        "--probe",
        action="store_true",
        help="Also publish 0.2.1-probe (same bin, higher file_version for OTA update tests)",
    )
    args = parser.parse_args()

    if not args.bin.is_file():
        print(f"error: firmware bin not found: {args.bin}", file=sys.stderr)
        print("Run: idf.py build", file=sys.stderr)
        return 1

    releases: list[tuple[str, str]] = []
    if args.versions:
        for item in args.versions:
            if ":" in item:
                ver, changelog = item.split(":", 1)
            else:
                ver, changelog = item, f"CamperNode OS {item}"
            releases.append((ver.strip(), changelog.strip()))
    else:
        releases.append(("0.2.0", "CamperNode OS 0.2.0 (MOSFET :Od, Zigbee OTA)"))

    if args.probe:
        releases.append(("0.2.1", "Probe-Release 0.2.1 — OTA-Update-Test (identisches Image)"))

    entries: list[dict] = []
    for version, changelog in releases:
        ota_path = build_ota(args.bin, version, changelog)
        fv = semver_to_file_version(version)
        entries.append(index_entry(ota_path, fv, changelog))
        print(f"published {ota_path.name} (v{version})")

    write_index(entries)
    print(f"wrote {INDEX_PATH}")
    print(f"ZHA: zigpy_remote url = {GITHUB_RAW_BASE}/ota-index.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
