#!/usr/bin/env python3
"""构建并导出 IDU-pre MCUboot 配置，不烧录硬件。"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

from intelhex import IntelHex

APP = Path(__file__).resolve().parents[1]
WORKSPACE = APP.parents[1]
IMGTOOL = WORKSPACE / "bootloader/mcuboot/scripts/imgtool.py"
SLOT_START = 0x08010000
SLOT_SIZE = 0x70000


def run(*args):
    subprocess.run([str(a) for a in args], cwd=WORKSPACE, check=True)


def check_vectors(image, base, header=0):
    if len(image) < header + 8:
        raise ValueError("image lacks vectors")
    sp, reset = struct.unpack_from("<II", image, header)
    if not 0x20000000 < sp <= 0x20020000 or sp % 8:
        raise ValueError("image stack outside configured 128 KiB RAM")
    if not reset & 1 or not base + header <= (reset & ~1) < base + len(image):
        raise ValueError("reset vector outside image")
    return {"stack": hex(sp), "reset": hex(reset)}


def export(build, key, destination, iapv3=False, version="IDU-pre"):
    appdir, bootdir = build / "IDU-pre/zephyr", build / "mcuboot/zephyr"
    binary = appdir / "IDU-pre.signed.bin"
    run(sys.executable, IMGTOOL, "verify", "-k", key, binary)
    blob = binary.read_bytes()
    magic, _, header, _, size, flags = struct.unpack_from("<IIHHII", blob)
    if magic != 0x96F3B83D or header != 0x200 or flags != 0:
        raise ValueError("unexpected MCUboot header/flags")
    # 为交换尾部和状态预留一个完整的最大擦除扇区。
    if header + size > len(blob) or len(blob) > SLOT_SIZE - 8192:
        raise ValueError("signed image exceeds the configured slot payload")
    app_vectors = check_vectors(blob[: header + size], SLOT_START, header)
    boot_blob = (bootdir / "zephyr.bin").read_bytes()
    if len(boot_blob) > SLOT_START - 0x08000000:
        raise ValueError("MCUboot exceeds boot partition")
    boot_vectors = check_vectors(boot_blob, 0x08000000)

    # Zephyr 分别签名 HEX 和 BIN，ECDSA 签名的
    # 字节及 DER 长度可能不同；两种导出格式使用同一份标准签名 BIN。
    app_hex = IntelHex()
    app_hex.frombytes(blob, offset=SLOT_START)
    boot_hex = IntelHex(str(bootdir / "zephyr.hex"))
    for hex_data, raw, base in ((app_hex, blob, SLOT_START), (boot_hex, boot_blob, 0x08000000)):
        addresses = set(hex_data.addresses())
        if hex_data.minaddr() != base or hex_data.maxaddr() != base + len(raw) - 1:
            raise ValueError("HEX and BIN extents differ")
        if any(raw[a - base] != hex_data[a] for a in hex_data.addresses()):
            raise ValueError("HEX and BIN bytes differ")
        for offset, byte in enumerate(raw):
            if base + offset not in addresses and byte not in (0, 255):
                raise ValueError("BIN contains non-padding data missing from HEX")
    # 起始地址元数据描述各自的可执行镜像，工厂 HEX 只使用
    # 引导程序入口；任何数据地址重叠都视为错误。
    app_hex.start_addr = None
    merged = IntelHex(boot_hex)
    merged.merge(app_hex, overlap="error")
    destination.mkdir(parents=True, exist_ok=True)
    outputs = {
        "IDU-pre-boot.hex": bootdir / "zephyr.hex",
        "IDU-pre-update.bin": binary,
    }
    for name, source in outputs.items():
        shutil.copyfile(source, destination / name)
    app_hex.write_hex_file(str(destination / "IDU-pre.hex"))
    merged.write_hex_file(str(destination / "IDU-pre-factory.hex"))
    run(
        sys.executable,
        IMGTOOL,
        "getpub",
        "-k",
        key,
        "-e",
        "pem",
        "-o",
        destination / "IDU-pre-public.pem",
    )
    if iapv3:
        run(
            sys.executable,
            APP / "tools/pack_sgz.py",
            "--bin",
            binary,
            "--key",
            key,
            "--output",
            destination / "IDU-pre-update.sgz",
            "--version",
            version,
            "--config",
            appdir / ".config",
        )
    report = {
        "bootloader": "MCUboot",
        "mode": "swap-scratch",
        "signature_verified": True,
        "hardware_verified": False,
        "iap_sgz_supported": iapv3,
        "boot_bytes": len(boot_blob),
        "signed_app_bytes": len(blob),
        "slot_start": hex(SLOT_START),
        "slot_bytes": SLOT_SIZE,
        "boot_vectors": boot_vectors,
        "app_vectors": app_vectors,
        "files_sha256": {
            p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(destination.iterdir())
            if p.suffix in (".hex", ".bin", ".pem", ".sgz")
        },
    }
    (destination / "IDU-pre-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"MCUboot artifacts: {destination}")


def main():
    parser = argparse.ArgumentParser(
        description=("Build and export IDU-pre's MCUboot profile; never programs hardware.")
    )
    parser.add_argument("--key", type=Path, help="Existing ECDSA P-256 private signing key")
    parser.add_argument("--version", default="0.1.0+0")
    parser.add_argument(
        "--extra-conf",
        type=Path,
        action="append",
        default=[],
        help="Append an application .conf (repeatable)",
    )
    parser.add_argument(
        "--extra-overlay",
        type=Path,
        action="append",
        default=[],
        help="Append an application devicetree overlay (repeatable)",
    )
    parser.add_argument("--pristine", choices=("auto", "always"), default="auto")
    upload = parser.add_mutually_exclusive_group()
    upload.add_argument(
        "--iapv3", action="store_true", help="IAPv3 RS485 AES/SGZ transport (default)"
    )
    upload.add_argument("--no-upload", action="store_true", help="Build without an upload receiver")
    args = parser.parse_args()
    args.iapv3 = not args.no_upload
    key = args.key.expanduser().resolve() if args.key else WORKSPACE / "build/keys/IDU-pre-dev.pem"
    if args.key and not key.is_file():
        parser.error("--key must refer to an existing signing key")
    if not key.exists():
        key.parent.mkdir(parents=True, exist_ok=True)
        old_mask = os.umask(0o077)
        try:
            run(sys.executable, IMGTOOL, "keygen", "-t", "ecdsa-p256", "-k", key)
        finally:
            os.umask(old_mask)
    if not args.key:
        print(f"Using local development key: {key}; retain it for subsequent updates.", flush=True)
    suffix = "-iapv3" if args.iapv3 else ""
    build = WORKSPACE / f"build/IDU-pre/mcuboot{suffix}"
    conf = [APP / "bootloader/app.conf"]
    overlays = [APP / "bootloader/app.overlay"]
    if args.iapv3:
        conf.append(APP / "conf/iapv3.conf")
    conf.extend(path.expanduser().resolve() for path in args.extra_conf)
    overlays.extend(path.expanduser().resolve() for path in args.extra_overlay)
    command = [
        "west",
        "build",
        "-p",
        args.pristine,
        "--sysbuild",
        "-b",
        "idu_pre",
        APP,
        "-d",
        build,
        "--",
        f"-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE={json.dumps(str(key))}",
        f"-DIDU-pre_EXTRA_CONF_FILE={';'.join(map(str, conf))}",
        f"-DIDU-pre_EXTRA_DTC_OVERLAY_FILE={';'.join(map(str, overlays))}",
        f"-DIDU-pre_CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION={json.dumps(args.version)}",
    ]
    run(*command)
    export(build, key, WORKSPACE / f"build/hex/IDU-pre-mcuboot{suffix}", args.iapv3, args.version)


if __name__ == "__main__":
    main()
