"""固定 Zephyr 4.5.0 服务端的边界修复；只作用于构建副本。"""

import re


def apply_server_fixes(source, replace_once):
    # 禁用浮点扩展后，高地址仍须走整数读取，不能停在 while 中。
    source = replace_once(
        source,
        "if (reg_addr < MODBUS_FP_EXTENSIONS_ADDR)",
        "if (reg_addr < MODBUS_FP_EXTENSIONS_ADDR || !IS_ENABLED(CONFIG_MODBUS_FP_EXTENSIONS))",
        2,
    )
    source = replace_once(
        source,
        "static bool mbs_fc15_coils_write(struct modbus_context *ctx)\n{\n"
        "\tconst uint16_t coils_limit = 2000;",
        "static bool mbs_fc15_coils_write(struct modbus_context *ctx)\n{\n"
        "\tconst uint16_t coils_limit = 1968;",
    )
    source = replace_once(
        source,
        "static bool mbs_fc16_hregs_write(struct modbus_context *ctx)\n{\n"
        "\tconst uint16_t regs_limit = 125;",
        "static bool mbs_fc16_hregs_write(struct modbus_context *ctx)\n{\n"
        "\tconst uint16_t regs_limit = 123;",
    )
    source = replace_once(
        source,
        "if ((num_bytes / reg_qty) != sizeof(uint16_t))",
        "if (num_bytes != reg_qty * sizeof(uint16_t))",
    )
    source = replace_once(
        source,
        "\tcoil_val = sys_get_be16(&ctx->rx_adu.data[2]);",
        "\tcoil_val = sys_get_be16(&ctx->rx_adu.data[2]);\n"
        "\tif (coil_val != MODBUS_COIL_OFF_CODE && coil_val != MODBUS_COIL_ON_CODE) {\n"
        "\t\tmbs_exception_rsp(ctx, MODBUS_EXC_ILLEGAL_DATA_VAL);\n\t\treturn true;\n\t}",
    )
    # 保持本产品错误长度返回 03 的契约，且不触发任何数据回调。
    source, count = re.subn(
        r'(\t\tLOG_ERR\("Wrong request length[^\n]*\);\n)\t\treturn false;',
        r"\1\t\tmbs_exception_rsp(ctx, MODBUS_EXC_ILLEGAL_DATA_VAL);\n\t\treturn true;",
        source,
    )
    if count != 9:
        raise SystemExit("Zephyr server length-check anchors changed")
    guard = (
        "\n\tif (num_bytes + 1 > sizeof(ctx->tx_adu.data)) {\n"
        "\t\tmbs_exception_rsp(ctx, MODBUS_EXC_ILLEGAL_DATA_VAL);\n\t\treturn true;\n\t}"
    )
    for statement, count in (
        ("\tnum_bytes = ((coil_qty - 1) / 8) + 1;", 1),
        ("\tnum_bytes = ((di_qty - 1) / 8) + 1;", 1),
        ("\tnum_bytes = (uint8_t)(reg_qty * sizeof(uint16_t));", 2),
    ):
        source = replace_once(source, statement, statement + guard, count)
    return source
