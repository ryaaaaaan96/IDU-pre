#!/usr/bin/env python3
"""使用上游公开测试密钥生成小型签名镜像。"""

import importlib.util
from pathlib import Path
import struct
import subprocess
import sys

app = Path(__file__).resolve().parents[2]
root = app.parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location("pack_sgz", app / "tools/pack_sgz.py")
packer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packer)
body = bytearray((i * 17) % 256 for i in range(4096))
load_address = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x08010000
ram_end = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x20020000
config = Path(sys.argv[4]) if len(sys.argv) > 4 else packer.DEFAULT_CONFIG
struct.pack_into("<II", body, 0, ram_end, load_address + 0x209)
(out / "fixture.bin").write_bytes(body)
subprocess.run(
    [
        sys.executable,
        str(root / "bootloader/mcuboot/scripts/imgtool.py"),
        "sign",
        "-k",
        str(root / "bootloader/mcuboot/root-ec-p256.pem"),
        "--header-size",
        "0x200",
        "--pad-header",
        "--align",
        "4",
        "--slot-size",
        "0x70000",
        "--version",
        "1.2.3+4",
        str(out / "fixture.bin"),
        str(out / "fixture.signed.bin"),
    ],
    check=True,
)
image = (out / "fixture.signed.bin").read_bytes()
text = ["/* Generated test fixtures: upstream sample key, never a production key. */"]
bad_signature = image[:-1] + bytes([image[-1] ^ 1])
for name, blob in (
    ("fixture_image", image),
    ("fixture_sgz", packer.pack(image, config=config)),
    ("fixture_bad_signature_sgz", packer.pack(bad_signature, config=config)),
):
    text.append(f"static const uint8_t {name}[] = {{" + ",".join(str(x) for x in blob) + "};")
(out / "fixture.h").write_text("\n".join(text) + "\n")
subprocess.run(
    [
        sys.executable,
        str(app / "modules/iap/scripts/public_key.py"),
        str(root / "bootloader/mcuboot/root-ec-p256.pem"),
        str(out / "iap_keys.h"),
        packer.legacy_key(config).hex(),
    ],
    check=True,
)
