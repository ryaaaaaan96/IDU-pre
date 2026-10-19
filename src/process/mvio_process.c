#include "idu_mvio.h"
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(idu_mvio_process, LOG_LEVEL_INF);

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
