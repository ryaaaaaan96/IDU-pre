#!/usr/bin/env python3
"""Generate only the MCUboot signing PUBLIC key for IAP pre-boot verification."""
import hashlib
from pathlib import Path
import sys
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec


def public_material(path):
    data = Path(path).read_bytes()
    try:
        key = serialization.load_pem_public_key(data)
    except ValueError:
        key = serialization.load_pem_private_key(data, password=None).public_key()
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("IAP requires the same ECDSA P-256 key as MCUboot")
    raw = key.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)[1:]
    der = key.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    return raw, hashlib.sha256(der).digest()


def main():
    raw, digest = public_material(sys.argv[1])
    lines = ["/* Generated public verification material; contains no private key. */"]
    for name, data in (("idu_iap_public_key", raw), ("idu_iap_key_hash", digest)):
        lines.append(f"static const uint8_t {name}[{len(data)}] = {{" +
                     ",".join(f"0x{x:02x}" for x in data) + "};")
    Path(sys.argv[2]).write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
