#!/usr/bin/env python3
"""Wrap a signed IDU-pre MCUboot BIN for the legacy AES/IAPv3 transport."""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import sys
import zlib
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

APP = Path(__file__).resolve().parents[1]
WORKSPACE = APP.parents[1]


def legacy_key():
    text = (APP / "app/iap/legacy_aes_key.h").read_text()
    return bytes(int(n, 16) for n in re.findall(r"0x[0-9a-fA-F]+", text))


def pack(image, version="IDU-pre", encrypt=True):
    if not 520 <= len(image) <= 0x6e000 or struct.unpack_from("<I", image)[0] != 0x96f3b83d:
        raise ValueError("requires a signed MCUboot image, not the legacy bare BIN")
    name = version.encode("ascii")
    if len(name) > 31:
        raise ValueError("SGZ version must contain at most 31 ASCII bytes")
    blocks = bytearray()
    for offset in range(0, len(image), 2048):
        block = image[offset:offset + 2048]
        # Valid QuickLZ 1.5 level-3 STORED frame. Receiver also supports actual
        # compressed legacy frames; no GPL/commercial encoder is bundled.
        frame = struct.pack("<BII", 0x4e, len(block) + 9, len(block)) + block
        blocks += struct.pack("<H", len(frame)) + frame
    sub = struct.pack("<IIII32sII32sIII", 2, 0, len(image), zlib.crc32(image), name,
                      0, 0, b"", len(blocks), zlib.crc32(blocks), 132)
    tail = struct.pack("<III", 132 + len(blocks), 132, 1) + sub + blocks
    blob = b"SungrowIAP3.0".ljust(16, b"\0") + struct.pack("<I", zlib.crc32(tail)) + tail
    blob += b"\0" * (-len(blob) % 16)
    if encrypt:
        enc = Cipher(algorithms.AES(legacy_key()), modes.ECB()).encryptor()
        blob = blob[:16] + enc.update(blob[16:]) + enc.finalize()
    return blob


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True, help="MCUboot public or private PEM")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", default="IDU-pre")
    parser.add_argument("--plain", action="store_true",
                        help="Emit plaintext SGZ for a host tool that performs AES itself")
    args = parser.parse_args()
    subprocess.run([sys.executable, str(WORKSPACE / "bootloader/mcuboot/scripts/imgtool.py"),
                    "verify", "-k", str(args.key), str(args.bin)], check=True)
    blob = pack(args.bin.read_bytes(), args.version, encrypt=not args.plain)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)
    print(f"IAPv3 SGZ: {args.output} ({len(blob)} bytes, {'plaintext' if args.plain else 'AES transport'})")


if __name__ == "__main__":
    main()
