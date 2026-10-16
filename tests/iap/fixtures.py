#!/usr/bin/env python3
"""Generate a small signed image with the upstream PUBLIC TEST key."""
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
struct.pack_into("<II", body, 0, 0x20020000, 0x08010209)
(out / "fixture.bin").write_bytes(body)
subprocess.run([sys.executable, str(root / "bootloader/mcuboot/scripts/imgtool.py"),
                "sign", "-k", str(root / "bootloader/mcuboot/root-ec-p256.pem"),
                "--header-size", "0x200", "--pad-header", "--align", "4",
                "--slot-size", "0x70000", "--version", "1.2.3+4",
                str(out / "fixture.bin"), str(out / "fixture.signed.bin")], check=True)
image = (out / "fixture.signed.bin").read_bytes()
text = ["/* Generated test fixtures: upstream sample key, never a production key. */"]
bad_signature = image[:-1] + bytes([image[-1] ^ 1])
for name, blob in (("fixture_image", image), ("fixture_sgz", packer.pack(image)),
                   ("fixture_bad_signature_sgz", packer.pack(bad_signature))):
    text.append(f"static const uint8_t {name}[] = {{" + ",".join(str(x) for x in blob) + "};")
(out / "fixture.h").write_text("\n".join(text) + "\n")
