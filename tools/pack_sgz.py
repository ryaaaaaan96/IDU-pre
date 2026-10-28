#!/usr/bin/env python3
"""将已签名的 IDU-pre MCUboot BIN 封装为旧版 AES/IAPv3 传输格式。"""

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


DEFAULT_CONFIG = APP / "conf/iapv3.conf"


def legacy_key(config=DEFAULT_CONFIG):
    text = Path(config).read_text()
    match = re.search(r'^CONFIG_IAP_AES_KEY_HEX="([0-9a-fA-F]{32})"$', text, re.M)
    if not match:
        raise ValueError("configuration must provide a 16-byte CONFIG_IAP_AES_KEY_HEX")
    return bytes.fromhex(match[1])


def pack(image, version="IDU-pre", encrypt=True, config=DEFAULT_CONFIG):
    match = re.search(
        r"^CONFIG_IAP_IMAGE_MAX_SIZE=(0x[0-9a-fA-F]+)$", Path(config).read_text(), re.M
    )
    if not match:
        raise ValueError("configuration must provide CONFIG_IAP_IMAGE_MAX_SIZE")
    if (
        not 520 <= len(image) <= int(match[1], 16)
        or struct.unpack_from("<I", image)[0] != 0x96F3B83D
    ):
        raise ValueError("requires a signed MCUboot image, not the legacy bare BIN")
    name = version.encode("ascii")
    if len(name) > 31:
        raise ValueError("SGZ version must contain at most 31 ASCII bytes")
    blocks = bytearray()
    for offset in range(0, len(image), 2048):
        block = image[offset : offset + 2048]
        # 有效的 QuickLZ 1.5 级别 3 原样存储帧；接收器也支持真正的
        # 旧版压缩帧；此处不捆绑 GPL 或商业编码器。
        frame = struct.pack("<BII", 0x4E, len(block) + 9, len(block)) + block
        blocks += struct.pack("<H", len(frame)) + frame
    sub = struct.pack(
        "<IIII32sII32sIII",
        2,
        0,
        len(image),
        zlib.crc32(image),
        name,
        0,
        0,
        b"",
        len(blocks),
        zlib.crc32(blocks),
        132,
    )
    tail = struct.pack("<III", 132 + len(blocks), 132, 1) + sub + blocks
    blob = b"SungrowIAP3.0".ljust(16, b"\0") + struct.pack("<I", zlib.crc32(tail)) + tail
    blob += b"\0" * (-len(blob) % 16)
    if encrypt:
        enc = Cipher(algorithms.AES(legacy_key(config)), modes.ECB()).encryptor()
        blob = blob[:16] + enc.update(blob[16:]) + enc.finalize()
    return blob


def main():
    parser = argparse.ArgumentParser(
        description=("Wrap a signed IDU-pre MCUboot BIN for the legacy AES/IAPv3 transport.")
    )
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True, help="MCUboot public or private PEM")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", default="IDU-pre")
    parser.add_argument(
        "--config",
        type=Path,
        default=DEFAULT_CONFIG,
        help="Application .config with the image limit and AES transport key",
    )
    parser.add_argument(
        "--plain",
        action="store_true",
        help="Emit plaintext SGZ for a host tool that performs AES itself",
    )
    args = parser.parse_args()
    subprocess.run(
        [
            sys.executable,
            str(WORKSPACE / "bootloader/mcuboot/scripts/imgtool.py"),
            "verify",
            "-k",
            str(args.key),
            str(args.bin),
        ],
        check=True,
    )
    blob = pack(args.bin.read_bytes(), args.version, encrypt=not args.plain, config=args.config)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)
    print(
        f"IAPv3 SGZ: {args.output} ({len(blob)} bytes, "
        f"{'plaintext' if args.plain else 'AES transport'})"
    )


if __name__ == "__main__":
    main()
