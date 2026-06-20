#!/usr/bin/env python3
"""Wrap an ESP-IDF app .bin into a Zigbee OTA file for ZHA (CamperNode metadata)."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

OTA_MAGIC = 0x0BEEF11E
HEADER_VERSION = 0x0100
STACK_VERSION = 0x0002
UPGRADE_IMAGE_TAG = 0x0000

# Must match config/include/camper_config.h
DEFAULT_MANUF = 0x147B
DEFAULT_IMAGE_TYPE = 0xC602
DEFAULT_FILE_VERSION = 0x00000200


def semver_to_file_version(version: str) -> int:
    parts = version.split(".")
    if len(parts) != 3:
        raise ValueError(f"expected semver major.minor.patch, got {version!r}")
    major, minor, patch = (int(p) for p in parts)
    return (major << 16) | (minor << 8) | patch


def build_ota_image(
    firmware: bytes,
    *,
    manuf_id: int,
    image_type: int,
    file_version: int,
    header_string: str,
) -> bytes:
    tag_block = struct.pack("<HI", UPGRADE_IMAGE_TAG, len(firmware)) + firmware
    header_str = header_string.encode("ascii")[:32].ljust(32, b"\x00")
    field_control = 0

    header_body = struct.pack(
        "<HHIH",
        manuf_id,
        image_type,
        file_version,
        STACK_VERSION,
    )
    header_body += header_str
    header_body += struct.pack("<I", 0)  # image_size placeholder

    header_len = 4 + 2 + 2 + 2 + len(header_body)
    image_size = header_len + len(tag_block)

    header = struct.pack("<I", OTA_MAGIC)
    header += struct.pack("<H", HEADER_VERSION)
    header += struct.pack("<H", header_len)
    header += struct.pack("<H", field_control)
    header += header_body[:-4]
    header += struct.pack("<I", image_size)

    return header + tag_block


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware_bin", type=Path, help="Path to campernode_os.bin")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Output .ota file (default: <bin>.ota)",
    )
    parser.add_argument("--manuf-id", type=lambda x: int(x, 0), default=DEFAULT_MANUF)
    parser.add_argument("--image-type", type=lambda x: int(x, 0), default=DEFAULT_IMAGE_TYPE)
    parser.add_argument(
        "--file-version",
        type=lambda x: int(x, 0),
        default=DEFAULT_FILE_VERSION,
        help="Zigbee file version (default 0x00000200 = v0.2.0)",
    )
    parser.add_argument(
        "--version",
        dest="semver",
        help="Semver e.g. 0.2.0 (overrides --file-version)",
    )
    parser.add_argument(
        "--header-string",
        default="CamperNode OS",
        help="32-byte OTA header description",
    )
    args = parser.parse_args()

    if not args.firmware_bin.is_file():
        print(f"error: not found: {args.firmware_bin}", file=sys.stderr)
        return 1

    file_version = args.file_version
    if args.semver:
        file_version = semver_to_file_version(args.semver)

    firmware = args.firmware_bin.read_bytes()
    ota = build_ota_image(
        firmware,
        manuf_id=args.manuf_id,
        image_type=args.image_type,
        file_version=file_version,
        header_string=args.header_string,
    )

    out = args.output or args.firmware_bin.with_suffix(".ota")
    out.write_bytes(ota)

    name = f"{args.manuf_id:04x}-{args.image_type:04x}-{file_version:08x}-campernode.ota"
    print(f"Wrote {out} ({len(ota)} bytes)")
    print(f"ZHA filename hint: {name}")
    print("Copy into HA Zigbee OTA folder or use ZHA device OTA provider.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
