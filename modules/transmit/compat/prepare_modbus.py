"""Build-local Zephyr 4.4.2 adaptations. Never modifies the west checkout.

Keep anchors strict: a Zephyr upgrade must review RX scheduling, whole-PDU
writes, and RAW exception validation before accepting a different source.
"""
from pathlib import Path
import hashlib
import sys

EXPECTED_SHA256 = {
    "modbus_core.c": "c9a503faa301f003fea86899126fbedd4e736d0f37883b864e0c90310dabcbb0",
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

for name in EXPECTED_SHA256:
    original = (base / "subsys/modbus" / name).read_text()
    if hashlib.sha256(original.encode()).hexdigest() != EXPECTED_SHA256[name]:
        raise SystemExit(f"{name} differs from reviewed Zephyr 4.4.2; review IDU compat before building")
    source = replace_once(original, "#include <modbus_internal.h>",
                          '#include <modbus_internal.h>\n#include "modbus_hooks.h"')
    if name in ("modbus_raw.c", "modbus_serial.c"):
        source = replace_once(source, "k_work_submit(&ctx->server_work)",
                              "tr_modbus_submit_work(&ctx->server_work)",
                              1 if name == "modbus_raw.c" else 3)
    if name == "modbus_raw.c":
        source = replace_once(source, "(MODBUS_RTU_MIN_MSG_SIZE - 2)", "1")
        source = replace_once(source, "(CONFIG_MODBUS_BUFFER_SIZE - 2)",
                              "sizeof(ctx->rx_adu.data)")
        source = replace_once(source, "\tctx->rx_adu.trans_id = adu->trans_id;",
                              """\tint err = tr_modbus_transport_rx_begin(ctx);
\tif (err) { return err; }
\tctx->rx_adu.trans_id = adu->trans_id;""")
        source = replace_once(source, "\ttr_modbus_submit_work(&ctx->server_work);\n\n\treturn 0;",
                              """\terr = tr_modbus_submit_work(&ctx->server_work);
\tif (err < 0) { tr_modbus_transport_rx_end(ctx); return err; }
\treturn 0;""")
    if name == "modbus_core.c":
        source = replace_once(source, "\tif (ctx->client == true) {",
                              """\tif (!ctx->rx_adu_err) {
\t\tctx->rx_adu_err = tr_modbus_transport_rx_route(ctx);
\t}
\tif (ctx->client == true) {
\t\ttr_modbus_transport_rx_end(ctx);""")
        source = replace_once(source, "\n}\n\nvoid modbus_tx_adu",
                              "\n\tif (!ctx->client) { tr_modbus_transport_rx_end(ctx); }\n}\n\nvoid modbus_tx_adu")
        source = replace_once(source, "void modbus_tx_adu(struct modbus_context *ctx)\n{",
                              """void modbus_tx_adu(struct modbus_context *ctx)
{
\tint err;
\tif (tr_modbus_transport_tx_route(ctx, &err)) {
\t\tif (err) {
\t\t\tLOG_ERR("Virtual-port transmission failed: %d", err);
\t\t\tif (ctx->client) {
\t\t\t\tctx->rx_adu_err = err;
\t\t\t\tk_sem_give(&ctx->client_wait_sem);
\t\t\t}
\t\t}
\t\treturn;
\t}""")
    if name == "modbus_serial.c":
        source = replace_once(source,
                              "\t\tmodbus_serial_rx_fifo_drain(ctx);\n\t\tmodbus_serial_rx_on(ctx);",
                              "\t\tmodbus_serial_rx_fifo_drain(ctx);\n\t\tmodbus_serial_rx_on(ctx);\n"
                              "\t\ttr_modbus_transport_tx_done(ctx);")
        source += """
#ifdef CONFIG_TR_MODBUS_TRANSPORT
/* Interrupt API only: stop all use of the TX buffer before returning failure. */
void tr_modbus_transport_serial_abort(struct modbus_context *ctx)
{
\tunsigned int key = irq_lock();
\tmodbus_serial_disable(ctx);
\tctx->cfg->uart_buf_ctr = 0;
\tctx->cfg->uart_buf_ptr = ctx->cfg->uart_buf;
\tmodbus_serial_rx_fifo_drain(ctx);
\tmodbus_serial_rx_on(ctx);
\tirq_unlock(key);
}
#endif
"""
    if name == "modbus_server.c":
        source = replace_once(source, "if (addr != 0 && addr != ctx->unit_id)",
                              "if (!tr_modbus_accept_request(ctx))")
        source = replace_once(source, "\tswitch (fc) {", "\tif (!tr_modbus_write_request(ctx, &send_reply)) {\n\tswitch (fc) {")
        source = replace_once(source, "\n\tif (addr == 0) {",
                              "\n\t}\n\tif (tr_modbus_suppress_broadcast(ctx)) {")
        source = replace_once(source, "\n\treturn send_reply;",
                              "\n\ttr_modbus_complete_request(ctx, send_reply);\n\treturn send_reply;")
    (output / name).write_text(source)
    (output / (name + ".sha256")).write_text(hashlib.sha256(original.encode()).hexdigest() + "\n")
