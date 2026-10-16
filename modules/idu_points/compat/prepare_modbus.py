"""Build-local Zephyr 4.4.2 adaptations. Never modifies the west checkout.

Keep anchors strict: a Zephyr upgrade must review RX scheduling, whole-PDU
writes, and RAW exception validation before accepting a different source.
"""
from pathlib import Path
import hashlib
import sys

EXPECTED_SHA256 = {
    "modbus_raw.c": "8fbec5199b086db07ddebb776c2b5aaa2bb2eff6819c7e261f311b844a3d6e7f",
    "modbus_serial.c": "65c1e0b3b4b012b7ebb6aabaf0e8f6daef23fbd448dc78d79b2e9a5a5d6fed87",
    "modbus_server.c": "3a049c8e971032ba17574fd6d3c69635600ab84371f009fa8d7200697117e5dd",
}

base, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)

def replace_once(text, before, after, count=1):
    if text.count(before) != count:
        raise SystemExit("Zephyr Modbus adaptation anchor changed; review compat/prepare_modbus.py")
    return text.replace(before, after)

for name in ("modbus_raw.c", "modbus_serial.c", "modbus_server.c"):
    original = (base / "subsys/modbus" / name).read_text()
    if hashlib.sha256(original.encode()).hexdigest() != EXPECTED_SHA256[name]:
        raise SystemExit(f"{name} differs from reviewed Zephyr 4.4.2; review IDU compat before building")
    source = replace_once(original, "#include <modbus_internal.h>",
                          '#include <modbus_internal.h>\n#include "modbus_hooks.h"')
    if name in ("modbus_raw.c", "modbus_serial.c"):
        source = replace_once(source, "k_work_submit(&ctx->server_work)",
                              "idu_modbus_submit_work(&ctx->server_work)",
                              1 if name == "modbus_raw.c" else 3)
    if name == "modbus_raw.c":
        source = replace_once(source, "(MODBUS_RTU_MIN_MSG_SIZE - 2)", "1")
        source = replace_once(source, "(CONFIG_MODBUS_BUFFER_SIZE - 2)",
                              "sizeof(ctx->rx_adu.data)")
    if name == "modbus_server.c":
        source = replace_once(source, "if (addr != 0 && addr != ctx->unit_id)",
                              "if (!idu_modbus_accept_request(ctx))")
        source = replace_once(source, "\tswitch (fc) {", "\tif (!idu_modbus_write_request(ctx, &send_reply)) {\n\tswitch (fc) {")
        source = replace_once(source, "\n\tif (addr == 0) {",
                              "\n\t}\n\tif (idu_modbus_suppress_broadcast(ctx)) {")
    (output / name).write_text(source)
    (output / (name + ".sha256")).write_text(hashlib.sha256(original.encode()).hexdigest() + "\n")
