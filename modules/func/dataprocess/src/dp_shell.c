/* Shell 只查点表、调 DP 接口和显示结果；通用文本处理归 common。 */
#include "dp_internal.h"
#include <common/value_text.h>
#include <errno.h>
#include <string.h>
#include <zephyr/sys/printk.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>

#define VALUE_TEXT_BUFFER_SIZE (sizeof(value_t) * 5U + 3U)

static int format_value(value_type_t type, size_t size, value_t *value, char *text)
{
    if (type == VALUE_TYPE_STRING) {
        static const char hex[] = "0123456789ABCDEF";
        char *out = text;
        *out++ = '"';
        for (size_t i = 0; i < size && value->bytes[i]; ++i) {
            uint8_t byte = value->bytes[i];
            if (byte >= 32 && byte <= 126 && byte != '"' && byte != '\\') {
                *out++ = (char)byte;
            } else {
                *out++ = '\\';
                *out++ = 'x';
                *out++ = hex[byte >> 4];
                *out++ = hex[byte & 15];
            }
        }
        *out++ = '"';
        *out = '\0';
    } else if (type == VALUE_TYPE_BYTES || type == VALUE_TYPE_WORDS) {
        size_t used = 0;
        const size_t count = type == VALUE_TYPE_BYTES ? size : size / 2;
        for (size_t i = 0; i < count; ++i) {
            used += snprintk(text + used,
                VALUE_TEXT_BUFFER_SIZE - used,
                type == VALUE_TYPE_BYTES ? "%s0x%02X" : "%s0x%04X",
                i ? " " : "",
                type == VALUE_TYPE_BYTES ? value->bytes[i] : value->words[i]);
        }
    } else {
        return value_text_format(type, value, text, VALUE_TEXT_BUFFER_SIZE);
    }
    return 0;
}

static const char *bool_text(bool value)
{
    return value ? "true" : "false";
}

static void print_point_info(
    const struct shell *shell, uint16_t device, uint16_t key, const dp_point_info_t *info)
{
    shell_print(shell, "device: %u", device);
    shell_print(shell, "key: %u", key);
    shell_print(shell, "index: %zu", info->index);
    shell_print(shell, "type: %s", value_text_type_name(info->type));
    shell_print(shell, "size: %zu", info->size);
    shell_print(shell, "flags: 0x%02X", info->flags);
    shell_print(shell, "persistent: %s", bool_text(info->flags & DP_POINT_FLAG_PERSISTENT));
    shell_print(shell, "range-check: %s", bool_text(info->flags & DP_POINT_FLAG_CHECK_RANGE));
    shell_print(shell, "skip-defaults: %s", bool_text(info->flags & DP_POINT_FLAG_SKIP_DEFAULTS));
    shell_print(shell, "access-callback: %s", bool_text(info->has_callback));
}

static int parse_id(const char *text, uint16_t *id)
{
    const char *digits = text;
    if (*digits == '+') {
        ++digits;
    }
    const int base = digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X') ? 16 : 10;
    int err = 0;
    unsigned long parsed = shell_strtoul(text, base, &err);
    if (!err && (parsed == 0 || parsed > UINT16_MAX)) {
        err = -ERANGE;
    }
    if (!err) {
        *id = (uint16_t)parsed;
    }
    return err;
}

static int parse_ids(char **argv, uint16_t *device, uint16_t *key)
{
    uint16_t parsed_device, parsed_key;
    int err = parse_id(argv[1], &parsed_device);
    if (!err) {
        err = parse_id(argv[2], &parsed_key);
    }
    if (!err) {
        *device = parsed_device;
        *key = parsed_key;
    }
    return err;
}

static int cmd_read_key(const struct shell *shell, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    uint16_t device, key;
    dp_point_info_t info;
    int err = parse_ids(argv, &device, &key);
    if (!err) {
        err = dp_point_info_by_key(device, key, &info);
    }
    if (err) {
        shell_error(shell, "Point lookup failed (%d)", err);
        return err;
    }
    const bool callback_requested = strcmp(argv[0], "get-key") == 0;
    const dp_key_access_t access = {
        .device_id = device,
        .signal_key = key,
        .value_type = info.type,
        .flags = callback_requested ? DP_ACCESS_FLAG_GET_CALLBACK : DP_ACCESS_FLAG_NONE,
    };
    value_t value;
    err = dp_get_value_by_key(&access, &value);
    if (err) {
        shell_error(shell, "Read failed (%d)", err);
        return err;
    }
    char text[VALUE_TEXT_BUFFER_SIZE];
    err = format_value(info.type, info.size, &value, text);
    if (err) {
        shell_error(shell, "Value formatting failed (%d)", err);
        return err;
    }
    print_point_info(shell, device, key, &info);
    shell_print(shell, "operation: %s", argv[0]);
    shell_print(shell, "callback-executed: %s", bool_text(callback_requested && info.has_callback));
    shell_print(shell, "value: %s", text);
    return 0;
}

static int cmd_write_key(const struct shell *shell, size_t argc, char **argv)
{
    uint16_t device, key;
    dp_point_info_t info;
    int err = parse_ids(argv, &device, &key);
    if (!err) {
        err = dp_point_info_by_key(device, key, &info);
    }
    if (err) {
        shell_error(shell, "Point lookup failed (%d)", err);
        return err;
    }
    value_t value = {0};
    const size_t count = argc - 3;
    if (info.type == VALUE_TYPE_STRING) {
        if (count != 1) {
            err = -EINVAL;
        } else if (strlen(argv[3]) > info.size) {
            err = -EMSGSIZE;
        } else {
            memcpy(value.bytes, argv[3], strlen(argv[3]));
        }
    } else if (info.type == VALUE_TYPE_BYTES || info.type == VALUE_TYPE_WORDS) {
        const size_t expected = info.type == VALUE_TYPE_BYTES ? info.size : info.size / 2;
        if (count != expected) {
            err = -EINVAL;
        }
        for (size_t i = 0; !err && i < count; ++i) {
            value_t element;
            err = value_text_parse(info.type == VALUE_TYPE_BYTES ? VALUE_TYPE_U8 : VALUE_TYPE_U16,
                argv[i + 3],
                &element);
            if (!err) {
                if (info.type == VALUE_TYPE_BYTES) {
                    value.bytes[i] = element.u8;
                } else {
                    value.words[i] = element.u16;
                }
            }
        }
    } else {
        err = count == 1 ? value_text_parse(info.type, argv[3], &value) : -EINVAL;
    }
    if (err) {
        shell_error(shell,
            "Invalid %s input, expected size=%zu (%d)",
            value_text_type_name(info.type),
            info.size,
            err);
        return err;
    }
    const bool callback_requested = strcmp(argv[0], "set-key") == 0;
    const dp_key_access_t access = {
        .device_id = device,
        .signal_key = key,
        .value_type = info.type,
        .flags = callback_requested ? DP_ACCESS_FLAG_SET_CALLBACK | DP_ACCESS_FLAG_CHANGE_CALLBACK
                                    : DP_ACCESS_FLAG_NONE,
    };
    err = dp_set_value_by_key(&access, &value);
    if (err) {
        shell_error(shell, "Write failed (%d)", err);
        return err;
    }
    char text[VALUE_TEXT_BUFFER_SIZE];
    err = format_value(info.type, info.size, &value, text);
    if (err) {
        shell_error(shell, "Value formatting failed (%d)", err);
        return err;
    }
    /* 直接显示 set 返回值，避免额外触发读回调。 */
    print_point_info(shell, device, key, &info);
    shell_print(shell, "operation: %s", argv[0]);
    shell_print(shell, "callback-executed: %s", bool_text(callback_requested && info.has_callback));
    shell_print(shell, "value: %s", text);
    shell_print(shell, "result: RAM updated (not flushed)");
    return 0;
}

static int cmd_flush(const struct shell *shell, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    int err = dp_flush();
    if (err) {
        shell_error(shell, "Flush failed (%d)", err);
    } else {
        shell_print(shell, "Pending parameters flushed");
    }
    return err;
}

/* 显式字符串保留命令连字符，避免格式化器把宏参数当成减法表达式。 */
SHELL_STATIC_SUBCMD_SET_CREATE(dp_commands,
    {.syntax = "flush",
        .help = "Save pending parameters",
        .handler = cmd_flush,
        .args = {.mandatory = 1, .optional = 0}},
    {.syntax = "get-key",
        .help = "Read with callback: <device> <key>",
        .handler = cmd_read_key,
        .args = {.mandatory = 3, .optional = 0}},
    {.syntax = "peek-key",
        .help = "Read RAM without callback: <device> <key>",
        .handler = cmd_read_key,
        .args = {.mandatory = 3, .optional = 0}},
    {.syntax = "poke-key",
        .help = "Write without callback: <device> <key> <value...>",
        .handler = cmd_write_key,
        .args = {.mandatory = 4, .optional = VALUE_WORDS * 2 - 1}},
    {.syntax = "set-key",
        .help = "Write with callback: <device> <key> <value...>",
        .handler = cmd_write_key,
        .args = {.mandatory = 4, .optional = VALUE_WORDS * 2 - 1}},
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(dp, &dp_commands, "DP access using point-table types", NULL);
