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
                              "mb_support_submit_work(&ctx->server_work)",
                              1 if name == "modbus_raw.c" else 3)
    if name == "modbus_raw.c":
        source = replace_once(source, "(MODBUS_RTU_MIN_MSG_SIZE - 2)", "1")
        source = replace_once(source, "(CONFIG_MODBUS_BUFFER_SIZE - 2)", "sizeof(ctx->rx_adu.data)")
        source = replace_once(source,
            "\tctx->rawcb.raw_tx_cb(iface, &ctx->tx_adu, ctx->rawcb.user_data);\n\n\treturn 0;",
            "\treturn ctx->rawcb.raw_tx_cb(iface, &ctx->tx_adu, ctx->rawcb.user_data);")
        source = replace_once(source, "\tctx->rx_adu.trans_id = adu->trans_id;",
            "\tif (!adu || adu->length > sizeof(ctx->rx_adu.data)) { return -EMSGSIZE; }\n"
            "\tint err = mb_raw_rx_begin(ctx);\n\tif (err) { return err; }\n\tctx->rx_adu.trans_id = adu->trans_id;")
        source = replace_once(source, "\tmb_support_submit_work(&ctx->server_work);\n\n\treturn 0;",
            "\terr = mb_support_submit_work(&ctx->server_work);\n"
            "\tif (err < 0) { mb_raw_rx_end(ctx); return err; }\n\treturn 0;")
        source = replace_once(source, "\tctx->rx_adu.fc = adu->fc;", "\tctx->rx_adu.fc = adu->fc;\n\tctx->rx_adu.crc = adu->crc;")
    if name == "modbus_core.c":
        source = replace_once(source, "K_USEC(ctx->rxwait_to)",
                              "mb_client_response_timeout(ctx)")
        # Ordinary missing peers are reported by application state transitions;
        # retain per-attempt detail at debug level without flooding the console.
        source = replace_once(source, 'LOG_WRN("Client wait-for-RX timeout");',
                              'LOG_DBG("Client wait-for-RX timeout");')
        source = replace_once(source, "static void modbus_rx_handler", '#include "core_io.h"\n\nstatic void modbus_rx_handler')
        source = replace_once(source, "\tif (ctx->client == true) {",
            "\tif (mb_serial_dispatch(ctx)) { return; }\n"
            "\tif (ctx->client == true) {\n\t\tmb_raw_rx_end(ctx);")
        source = replace_once(source, "\n}\n\nvoid modbus_tx_adu",
            "\n\tif (!ctx->client) { mb_raw_rx_end(ctx); }\n}\n\nvoid modbus_tx_adu")
        source = replace_once(source,
            '\t\tif (IS_ENABLED(CONFIG_MODBUS_RAW_ADU) &&\n\t\t    modbus_raw_tx_adu(ctx)) {\n\t\t\tLOG_ERR("Unsupported MODBUS raw mode");\n\t\t}',
            '\t\tif (IS_ENABLED(CONFIG_MODBUS_RAW_ADU)) {\n'
            '\t\t\tint err = modbus_raw_tx_adu(ctx);\n'
            '\t\t\tif (err && ctx->client) { ctx->rx_adu_err = err; k_sem_give(&ctx->client_wait_sem); }\n'
            '\t\t}')
        source = replace_once(source, "\tk_work_cancel_sync(&ctx->server_work, &work_sync);",
            "\tk_work_cancel_sync(&ctx->server_work, &work_sync);\n\tmb_raw_rx_end(ctx);")
        source += "\n" + (Path(__file__).parent / "serial_open.h").read_text()
        source += """
int mb_support_check_available(int iface)
{
    if (iface < 0 || iface >= ARRAY_SIZE(mb_ctx_tbl)) { return -EINVAL; }
    return atomic_test_bit(&mb_ctx_tbl[iface].state, MODBUS_STATE_CONFIGURED) ? -EALREADY : 0;
}
"""
    if name == "modbus_serial.c":
        source = replace_once(source,
            "\t\t\t/* Buffer full. Disable interrupt until timeout. */",
            "\t\t\t/* Reject an overlong frame, including a valid CRC prefix. */\n"
            "\t\t\tmb_serial_overflow(ctx);\n\t\t\tmodbus_serial_rx_fifo_drain(ctx);")
        source = replace_once(source,
            "\t\tmodbus_serial_rx_fifo_drain(ctx);\n\t\tmodbus_serial_rx_on(ctx);",
            "\t\tmodbus_serial_rx_fifo_drain(ctx);\n\t\tmodbus_serial_rx_on(ctx);\n\t\tmb_serial_tx_done(ctx);")
        source += """
void mb_serial_abort(struct modbus_context *ctx)
{
    unsigned int key = irq_lock();
    modbus_serial_disable(ctx);
    ctx->cfg->uart_buf_ctr = 0;
    ctx->cfg->uart_buf_ptr = ctx->cfg->uart_buf;
    modbus_serial_rx_fifo_drain(ctx);
    modbus_serial_rx_on(ctx);
    irq_unlock(key);
}
"""
    if name == "modbus_server.c":
        source = replace_once(source, "if (addr != 0 && addr != ctx->unit_id)",
                              "if (!mb_support_accept_request(ctx))")
        source = replace_once(source, "\tswitch (fc) {", "\tif (!mb_support_write_request(ctx, &send_reply)) {\n\tswitch (fc) {")
        source = replace_once(source, "\n\tif (addr == 0) {",
                              "\n\t}\n\tif (mb_support_suppress_broadcast(ctx)) {")
        source = replace_once(source, "\n\treturn send_reply;",
                              "\n\tmb_support_complete_request(ctx, send_reply);\n\treturn send_reply;")
    (output / name).write_text(source)
    (output / (name + ".sha256")).write_text(hashlib.sha256(original.encode()).hexdigest() + "\n")
