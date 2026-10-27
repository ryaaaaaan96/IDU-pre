#!/usr/bin/env python3
"""使用 MCUboot 验证器检查真实导出镜像，包含反向用例。
请在 tools/build_mcuboot.py 后运行，
不访问设备或硬件传输。
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from intelhex import IntelHex

ROOT = Path(__file__).resolve().parents[3]
IMGTOOL = ROOT / "bootloader/mcuboot/scripts/imgtool.py"


def verified(image, key):
    result = subprocess.run([sys.executable, str(IMGTOOL), "verify", "-k", str(key),
                             str(image)], capture_output=True, text=True)
    return result.returncode == 0


def check(folder):
    binary = folder / "IDU-pre-update.bin"
    key = folder / "IDU-pre-public.pem"
    assert verified(binary, key), "exported image signature rejected"
    blob = binary.read_bytes()
    app = IntelHex(str(folder / "IDU-pre.hex"))
    factory = IntelHex(str(folder / "IDU-pre-factory.hex"))
    boot = IntelHex(str(folder / "IDU-pre-boot.hex"))
    app_addresses = set(app.addresses())
    boot_addresses = set(boot.addresses())
    assert app_addresses == set(range(0x08010000, 0x08010000 + len(blob)))
    assert app.tobinstr() == blob, "HEX must contain the exact uploaded signature"
    assert boot_addresses and all(0x08000000 <= a < 0x08010000 for a in boot_addresses)
    assert set(factory.addresses()) == app_addresses | boot_addresses
    assert all(factory[a] == app[a] for a in app_addresses)
    assert all(factory[a] == boot[a] for a in boot_addresses)
    header_size = struct.unpack_from("<H", blob, 8)[0]
    protected_size = struct.unpack_from("<H", blob, 10)[0]
    body_size = struct.unpack_from("<I", blob, 12)[0]
    tlv = header_size + body_size + protected_size
    magic, tlv_length = struct.unpack_from("<HH", blob, tlv)
    assert magic == 0x6907
    signature_offset = None
    cursor = tlv + 4
    while cursor < tlv + tlv_length:
        kind, length = struct.unpack_from("<HH", blob, cursor)
        if kind == 0x22:  # ECDSA256 签名条目类型：IMAGE_TLV_ECDSA256。
            signature_offset = cursor + 4
        cursor += 4 + length
    assert signature_offset is not None
    with tempfile.TemporaryDirectory(prefix="idu-signature-test-") as tmp:
        altered = Path(tmp) / "altered.bin"
        for offset in (0, header_size + 32, signature_offset):
            damaged = bytearray(blob)
            damaged[offset] ^= 0x40
            altered.write_bytes(damaged)
            assert not verified(altered, key), f"corrupt image accepted at offset {offset}"
        altered.write_bytes(blob[:header_size + 16])
        assert not verified(altered, key), "truncated image accepted"
    # 构建器生成的开发密钥不能是 MCUboot 的
    # 公开示例密钥；还须验证使用此错误密钥时签名校验失败。
    assert not verified(binary, ROOT / "bootloader/mcuboot/root-ec-p256.pem")
    print(f"PASS {folder.name}: signature, HEX/factory ranges, 5 rejection cases")


if __name__ == "__main__":
    folders = [Path(p) for p in sys.argv[1:]] or [ROOT / "build/hex/IDU-pre-mcuboot-iapv3"]
    for folder in folders:
        check(folder.resolve())
