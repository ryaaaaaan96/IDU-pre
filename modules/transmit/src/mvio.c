/* MV-IO compatibility with io-interface-board's active DEVICE_CONFIG_MV_IO.
 * Wire addresses are literal (no 40001 adjustment). Configured extensions keep
 * the standard Zephyr behavior; legacy broadcasts/247 never reach extensions.
 */
#include <transmit/mvio.h>
#include <transmit/transmit.h>
#include <transmit/modbus_adapter.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

static bool between(uint16_t value, uint16_t first, uint16_t last)
{
    return value >= first && value <= last;
}

static bool in_ranges(uint16_t address, const struct tr_register_range *ranges, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (between(address, ranges[i].first, ranges[i].last)) { return true; }
    }
    return false;
}

static bool accept(const struct modbus_adu *rx, uint8_t own, void *user)
{
    ARG_UNUSED(user);
    if (rx->length < 4 || (rx->fc != 3 && rx->fc != 4 && rx->fc != 6 && rx->fc != 16)) {
        return false;
    }
    if (rx->unit_id == 0 && rx->fc != 4 && rx->fc != 16) { return false; }
    if (rx->unit_id == own || rx->unit_id == 0) { return true; }
    return rx->unit_id == 247 && sys_get_be16(rx->data) == TR_MVIO_REG_ADDRESS;
}

static int legacy_read(const struct tr_mvio_config *cfg, int iface, uint8_t fc, uint16_t address, uint16_t count,
                        struct modbus_adu *tx)
{
    const struct tr_legacy_span *spans = fc == 4 ? cfg->input : cfg->holding;
    size_t nspans = fc == 4 ? cfg->input_count : cfg->holding_count, span;
    uint16_t entry = 0;
    for (span = 0; span < nspans; ++span) {
        if (between(address, spans[span].first, spans[span].first + spans[span].points - 1)) {
            entry = address - spans[span].first; break;
        }
    }
    for (size_t offset = 0; offset < count;) {
        if (span == nspans) { return TR_MODBUS_SILENT; }
        uint16_t reg = spans[span].first + entry, words[15] = {0};
        size_t width = spans[span].width;
        /* The legacy version string is an indivisible 15-register request. */
        if (width == 15 && (offset || count != 15)) { return TR_MODBUS_SILENT; }
        if (fc == 4 || reg == TR_MVIO_REG_ADDRESS) {
            if (tr_read(iface, fc, reg, words, width)) { return TR_MODBUS_SILENT; }
        }
        /* Advertise only the initialized, explicitly enabled IAP transport. */
        if (fc == 3 && cfg->upgrade_ready && cfg->upgrade_ready()) {
            if (reg == TR_MVIO_REG_IAP_CAPABILITY) { words[0] = 4; }
            else if (reg == TR_MVIO_REG_IAP_VERSION) { words[0] = 0x1200; }
        }
        size_t n = MIN(width, count - offset);
        for (size_t i = 0; i < n; ++i) { sys_put_be16(words[i], tx->data + 1 + 2 * (offset + i)); }
        offset += n;
        if (++entry == spans[span].points) { entry = 0; ++span; }
    }
    tx->data[0] = count * 2;
    tx->length = 1 + count * 2;
    return 0;
}

static int route(int iface, const struct modbus_adu *rx, struct modbus_adu *tx, void *user)
{
    const struct tr_mvio_config *cfg = user;
    if (rx->length < 4 || rx->length > sizeof(rx->data)) { return TR_MODBUS_SILENT; }
    uint16_t address = sys_get_be16(rx->data), count = sys_get_be16(rx->data + 2);
    if (cfg->upgrade_route) {
        int err = cfg->upgrade_route(iface, rx, tx);
        if (err != -ENOENT) { return err; }
    }
    if (rx->unit_id != 0 && rx->unit_id != 247 && in_ranges(address, cfg->extensions, cfg->extension_count)) {
        return tr_register_route(iface, rx, tx, (void *)cfg->registers);
    }
    if (rx->unit_id == 0 && rx->fc != 4 && rx->fc != 16) { return TR_MODBUS_SILENT; }
    if (rx->fc == 3 || rx->fc == 4) {
        if (rx->length != 4 || !count || count > 125 || 1 + count * 2 > sizeof(tx->data) ||
            !in_ranges(address, cfg->read_starts, cfg->read_start_count)) { return TR_MODBUS_SILENT; }
        return legacy_read(cfg, iface, rx->fc, address, count, tx);
    }
    if (rx->fc == 6) {
        if (rx->length != 4) { return TR_MODBUS_SILENT; }
        count = 1;
    } else if (rx->fc == 16) {
        if (!count || count > 123 || rx->length != 5 + 2 * count ||
            rx->data[4] != 2 * count) { return TR_MODBUS_SILENT; }
    } else { return TR_MODBUS_SILENT; }
    if (!in_ranges(address, cfg->write_starts, cfg->write_start_count) || (uint32_t)address + count > 65536) { return TR_MODBUS_SILENT; }
    /* MV-IO has one effective writable legacy point. Unknown entries within
     * these accepted windows are ACK-only, exactly as swSetRegData_4xReg. */
    if (address == TR_MVIO_REG_ADDRESS) {
        uint16_t value = sys_get_be16(rx->data + (rx->fc == 6 ? 2 : 5));
        if (value >= 1 && value <= 246 && tr_write(iface, address, &value, 1)) {
            return TR_MODBUS_SILENT; /* Never ACK a failed durable write. */
        }
    }
    tx->length = 4;
    memcpy(tx->data, rx->data, 4);
    return 0;
}

static bool valid_spans(const struct tr_legacy_span *spans, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (!spans[i].points || !spans[i].width || spans[i].width > 15 ||
            (uint32_t)spans[i].first + spans[i].points > 65536) { return false; }
    }
    return true;
}

static bool valid_ranges(const struct tr_register_range *ranges, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (ranges[i].first > ranges[i].last) { return false; }
    }
    return true;
}

int tr_mvio_attach(int iface, const void *config)
{
    const struct tr_mvio_config *cfg = config;
    if (!cfg || (cfg->input_count && !cfg->input) ||
        (cfg->holding_count && !cfg->holding) ||
        (cfg->read_start_count && !cfg->read_starts) ||
        (cfg->write_start_count && !cfg->write_starts) ||
        (cfg->extension_count && !cfg->extensions)) { return -EINVAL; }
    if (!valid_spans(cfg->input, cfg->input_count) || !valid_spans(cfg->holding, cfg->holding_count) ||
        !valid_ranges(cfg->read_starts, cfg->read_start_count) ||
        !valid_ranges(cfg->write_starts, cfg->write_start_count) ||
        !valid_ranges(cfg->extensions, cfg->extension_count)) { return -EINVAL; }
    const struct tr_modbus_policy policy = {.accept = accept, .legacy_broadcast = true};
    int err = tr_modbus_set_router(iface, route, (void *)cfg);
    return err ? err : tr_modbus_set_policy(iface, &policy);
}
