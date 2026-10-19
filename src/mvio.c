/* MV-IO compatibility with io-interface-board's active DEVICE_CONFIG_MV_IO.
 * Wire addresses are literal (no 40001 adjustment). Normal IDU extensions keep
 * the standard Zephyr behavior; legacy broadcasts/247 never reach extensions.
 */
#include "idu_equip.h"
#include "idu_mvio.h"
#ifdef CONFIG_IDU_IAPV3
#include "idu_iap.h"
#endif
#include <transmit/transmit.h>
#include <transmit/modbus_adapter.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(idu_mvio, LOG_LEVEL_INF);

#if DT_NODE_EXISTS(DT_ALIAS(idu_legacy_primary)) && DT_NODE_EXISTS(DT_ALIAS(idu_legacy_backup))
static int read_legacy(uint8_t id, uint8_t *address)
{
    const struct flash_area *area;
    int err = flash_area_open(id, &area);
    if (err) { return err; }
    uint8_t metadata[4], chunk[32], first[8] = {0};
    if (area->fa_size != 4096) { err = -EINVAL; goto out; }
    err = flash_area_read(area, 4092, metadata, sizeof(metadata));
    if (err) { goto out; }
    uint16_t len = sys_get_le16(metadata), crc = 0xffff;
    if (len == 0xffff && sys_get_le16(metadata + 2) == 0xffff) {
        err = -ENOENT; goto out;
    }
    if (!len || len > 4092) { err = -EBADMSG; goto out; }
    for (size_t offset = 0; offset < len; offset += sizeof(chunk)) {
        size_t n = MIN(sizeof(chunk), len - offset);
        err = flash_area_read(area, offset, chunk, n);
        if (err) { goto out; }
        if (!offset) { memcpy(first, chunk, MIN(n, sizeof(first))); }
        crc = crc16_reflect(0xa001, crc, chunk, n);
    }
    if (crc != sys_get_le16(metadata + 2)) { err = -EBADMSG; goto out; }
    /* ARM AAPCS G_FLASH_T: u8 address, padding, LE16 type, LE32 protocol.
     * A CRC-valid unknown layout must not silently reset or overwrite NVS. */
    if (len != sizeof(first)) { err = -EPROTO; goto out; }
    *address = first[0] <= 246 ? first[0] : 1;
    LOG_INF("Import MV-IO address %u from legacy partition %u", *address, id);
out:
    flash_area_close(area);
    return err;
}
#endif

int idu_mvio_seed_address(const struct dp_point_cfg *cfg, struct dp_value *value)
{
    ARG_UNUSED(cfg);
#if DT_NODE_EXISTS(DT_ALIAS(idu_legacy_primary)) && DT_NODE_EXISTS(DT_ALIAS(idu_legacy_backup))
    uint8_t address;
    int err = read_legacy(DT_FIXED_PARTITION_ID(DT_ALIAS(idu_legacy_primary)), &address);
    if (err == -ENOENT || err == -EBADMSG) {
        err = read_legacy(DT_FIXED_PARTITION_ID(DT_ALIAS(idu_legacy_backup)), &address);
    }
    if (!err) { value->words[0] = address; }
    else if (err != -ENOENT && err != -EBADMSG) { return err; }
    else { LOG_INF("No valid legacy parameters; use MV-IO defaults"); }
#else
    ARG_UNUSED(value); /* Native core tests have no legacy flash partitions. */
#endif
    return 0;
}

int idu_mvio_prepare_address(const struct dp_point_cfg *cfg, struct dp_value *value,
                             const struct dp_value *old)
{
    ARG_UNUSED(cfg); ARG_UNUSED(old);
    /* Boot import historically permits 0; the 30459 write contract is 1..246. */
    return value->words[0] >= 1 && value->words[0] <= 246 ? 0 : -ERANGE;
}

static bool between(uint16_t value, uint16_t first, uint16_t last)
{
    return value >= first && value <= last;
}

static bool read_start(uint16_t address)
{
    return between(address, 8149, 8199) || between(address, 39278, 39286) ||
           between(address, 2503, 2532) || between(address, 4989, 5014) ||
           between(address, 2568, 2571) || address == 30459 || address == 2581 ||
           address == 5486 || address == 20003 || address == 31996 || address == 31997;
}

static bool write_start(uint16_t address)
{
    return between(address, 8149, 8199) || between(address, 39278, 39286) || address == 30459;
}

static bool extension(uint16_t address)
{
    return between(address, 10000, 10023) || between(address, 31000, 31008) ||
           between(address, 31500, 31502) || between(address, 31090, 31093) ||
           between(address, 32000, 32003) || address == 30460;
}

static bool accept(const struct modbus_adu *rx, uint8_t own, void *user)
{
    ARG_UNUSED(user);
    if (rx->length < 4 || (rx->fc != 3 && rx->fc != 4 && rx->fc != 6 && rx->fc != 16)) {
        return false;
    }
    if (rx->unit_id == 0 && rx->fc != 4 && rx->fc != 16) { return false; }
    if (rx->unit_id == own || rx->unit_id == 0) { return true; }
    return rx->unit_id == 247 && sys_get_be16(rx->data) == 30459;
}

struct legacy_span { uint16_t first, points; uint8_t width; };
/* These represent ordered callback tables, not contiguous address windows.
 * The old reader advances to the next table entry across address gaps. */
static const struct legacy_span input[] = {
    {2503,30,1}, {2568,4,1}, {2581,1,15}, {4805,10,1}, {4989,26,1},
    {5486,2,1}, {8149,1,1}, {8150,1,2}, {8152,48,1},
};
static const struct legacy_span holding[] = {
    {4885,1,1}, {8149,10,1}, {8178,10,1}, {20003,1,1}, {30459,1,1},
    {31996,2,1}, {39278,6,1}, {39284,1,2},
};

static int legacy_read(int iface, uint8_t fc, uint16_t address, uint16_t count,
                        struct modbus_adu *tx)
{
    const struct legacy_span *spans = fc == 4 ? input : holding;
    size_t nspans = fc == 4 ? ARRAY_SIZE(input) : ARRAY_SIZE(holding), span;
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
        if (fc == 4 || reg == 30459) {
            if (tr_read(iface, fc, reg, words, width)) { return TR_MODBUS_SILENT; }
        }
        /* Advertise only the initialized, explicitly enabled IAP transport. */
#ifdef CONFIG_IDU_IAPV3
        if (fc == 3 && idu_iap_ready()) {
            if (reg == 20003) { words[0] = 4; }
            else if (reg == 31996) { words[0] = 0x1200; }
        }
#endif
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
    ARG_UNUSED(user);
    if (rx->length < 4 || rx->length > sizeof(rx->data)) { return TR_MODBUS_SILENT; }
    uint16_t address = sys_get_be16(rx->data), count = sys_get_be16(rx->data + 2);
#ifdef CONFIG_IDU_IAPV3
    int iap = idu_iap_route(iface, rx, tx);
    if (iap != -ENOENT) { return iap; }
#endif
    if (rx->unit_id != 0 && rx->unit_id != 247 && extension(address)) {
        return tr_dispatch(iface, rx, tx, NULL);
    }
    if (rx->unit_id == 0 && rx->fc != 4 && rx->fc != 16) { return TR_MODBUS_SILENT; }
    if (rx->fc == 3 || rx->fc == 4) {
        if (rx->length != 4 || !count || count > 125 || 1 + count * 2 > sizeof(tx->data) ||
            !read_start(address)) { return TR_MODBUS_SILENT; }
        return legacy_read(iface, rx->fc, address, count, tx);
    }
    if (rx->fc == 6) {
        if (rx->length != 4) { return TR_MODBUS_SILENT; }
        count = 1;
    } else if (rx->fc == 16) {
        if (!count || count > 123 || rx->length != 5 + 2 * count ||
            rx->data[4] != 2 * count) { return TR_MODBUS_SILENT; }
    } else { return TR_MODBUS_SILENT; }
    if (!write_start(address) || (uint32_t)address + count > 65536) { return TR_MODBUS_SILENT; }
    /* MV-IO has one effective writable legacy point. Unknown entries within
     * these accepted windows are ACK-only, exactly as swSetRegData_4xReg. */
    if (address == 30459) {
        uint16_t value = sys_get_be16(rx->data + (rx->fc == 6 ? 2 : 5));
        if (value >= 1 && value <= 246 && tr_write(iface, address, &value, 1)) {
            return TR_MODBUS_SILENT; /* Never ACK a failed durable write. */
        }
    }
    tx->length = 4;
    memcpy(tx->data, rx->data, 4);
    return 0;
}

int idu_mvio_attach(int iface)
{
    const struct tr_modbus_policy policy = {.accept = accept, .legacy_broadcast = true};
    int err = tr_modbus_set_router(iface, route, NULL);
    return err ? err : tr_modbus_set_policy(iface, &policy);
}
