#!/usr/bin/env python3
"""Read-only check of the current 512 KiB/128 KiB IDU-pre image layout.

Checks the vectors consumed by the legacy BOOT and HEX/BIN agreement. This is
not an SGZ packer and cannot establish real hardware boot/update compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

APP_START = 0x08004000
FLASH_END = 0x08080000
RAM_START = 0x20000000
RAM_END = 0x20020000


def read_hex(text):
    memory, base, ended = {}, 0, False
    for line_no, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if ended or not line.startswith(":"):
            raise ValueError(f"invalid HEX record at line {line_no}")
        record = bytes.fromhex(line[1:])
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 0xff:
            raise ValueError(f"invalid HEX length/checksum at line {line_no}")
        size, kind = record[0], record[3]
        offset = int.from_bytes(record[1:3], "big")
        payload = record[4:-1]
        if kind == 0:
            if offset + size > 0x10000:
                raise ValueError("HEX record crosses a 64 KiB window")
            for i, byte in enumerate(payload):
                address = base + offset + i
                if not APP_START <= address < FLASH_END or address in memory:
                    raise ValueError("HEX touches BOOT/outside APP or overlaps another record")
                memory[address] = byte
        elif kind == 1 and size == 0 and offset == 0:
            ended = True
        elif kind in (2, 4) and size == 2 and offset == 0:
            base = int.from_bytes(payload, "big") << (4 if kind == 2 else 16)
        elif kind in (3, 5) and size == 4 and offset == 0:
            pass  # Execution metadata is not a vector table or loadable data.
        else:
            raise ValueError(f"unsupported/malformed HEX record at line {line_no}")
    if not ended or not memory or min(memory) != APP_START:
        raise ValueError("HEX needs EOF and an application beginning at 0x08004000")
    return memory


def validate(binary, hex_text):
    if not 8 <= len(binary) <= FLASH_END - APP_START:
        raise ValueError("BIN length exceeds the 496 KiB application partition or lacks vectors")
    stack, reset = struct.unpack_from("<II", binary)
    if not RAM_START < stack <= RAM_END or stack & 7:
        raise ValueError("initial SP must be 8-byte aligned and inside the configured 128 KiB SRAM")
    if not reset & 1 or not APP_START <= (reset & ~1) < APP_START + len(binary):
        raise ValueError("reset vector must target Thumb code inside this application")
    memory = read_hex(hex_text)
    if max(memory) != APP_START + len(binary) - 1:
        raise ValueError("HEX and BIN image extents differ")
    if not all(APP_START + i in memory for i in range(8)) or (reset & ~1) not in memory:
        raise ValueError("HEX omits vectors or reset entry")
    for address, byte in memory.items():
        if binary[address - APP_START] != byte:
            raise ValueError(f"HEX/BIN mismatch at {address:#010x}")
    # objcopy may fill gaps; require that they contain no hidden executable data.
    if any(byte not in (0, 0xff) for i, byte in enumerate(binary) if APP_START + i not in memory):
        raise ValueError("BIN contains non-padding data omitted by HEX")
    return {
        "static_layout_valid": True, "hardware_verified": False,
        "application_start": hex(APP_START), "image_bytes": len(binary),
        "initial_sp": hex(stack), "reset_vector": hex(reset),
        "bin_crc32": f"{zlib.crc32(binary):08x}",
        "bin_sha256": hashlib.sha256(binary).hexdigest(),
        "iap_sgz_supported": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--hex", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = validate(args.bin.read_bytes(), args.hex.read_text(encoding="ascii"))
    except (ValueError, OSError) as exc:
        parser.exit(1, f"BOOT image check failed: {exc}\n")
    result["hex_sha256"] = hashlib.sha256(args.hex.read_bytes()).hexdigest()
    report = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(report, encoding="utf-8")
    print(report, end="")


if __name__ == "__main__":
    main()
