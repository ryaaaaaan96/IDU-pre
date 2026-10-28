#include "dp_internal.h"
#include "parameter_store.h"
#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#define VALUE_MAGIC 0x56554449U
#define META_MAGIC 0x4d554449U
#define SLOT_FIRST 0x100U
#define SLOT_COUNT 1024U
#define VALUE_MAX (DP_STORE_VALUE_HEADER + 2U * DP_STORE_MAX_WORDS)

static bool valid_value(uint8_t type, uint8_t width)
{
    if (type > DP_U8 || !width || width > DP_STORE_MAX_WORDS) {
        return false;
    }
    return type == DP_WORDS || width == ((type == DP_U32 || type == DP_S32) ? 2 : 1);
}

static int read_history(
    struct nvs_fs *fs, uint16_t id, uint8_t *data, size_t capacity, bool *recovered)
{
    for (uint16_t i = 0; i < 8; ++i) {
        ssize_t len = nvs_read_hist(fs, id, data, capacity, i);
        if (len == -EIO) {
            *recovered = true;
            continue;
        }
        if (len >= 0 && (size_t)len > capacity) {
            return -EMSGSIZE;
        }
        /* 最新条目损坏不能当作键不存在或新键处理。 */
        return len == -ENOENT && i ? -EIO : (int)len;
    }
    return -EIO;
}

static int find_slot(
    struct nvs_fs *fs, dp_key_t key, uint16_t *id, uint8_t *record, bool *recovered)
{
    uint8_t bytes[4];
    sys_put_le16(key.device_id, bytes);
    sys_put_le16(key.signal_id, bytes + 2);
    uint16_t start = crc32_ieee(bytes, sizeof(bytes)) % SLOT_COUNT;
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        *id = SLOT_FIRST + (start + i) % SLOT_COUNT;
        int len = read_history(fs, *id, record, VALUE_MAX, recovered);
        if (len == -ENOENT) {
            return len;
        }
        if (len < 0) {
            return len;
        }
        if (len < DP_STORE_VALUE_HEADER || sys_get_le32(record) != VALUE_MAGIC ||
            sys_get_le16(record + 4) != DP_STORE_VERSION || !sys_get_le16(record + 6) ||
            !sys_get_le16(record + 8) || !valid_value(record[10], record[11]) ||
            len != DP_STORE_VALUE_HEADER + 2U * record[11]) {
            return -EPROTO;
        }
        if (sys_get_le16(record + 6) == key.device_id &&
            sys_get_le16(record + 8) == key.signal_id) {
            return len;
        }
    }
    return -ENOSPC;
}

int dp_store_read(struct nvs_fs *fs, dp_key_t key, dp_stored_value_t *value, bool *recovered)
{
    uint8_t record[VALUE_MAX];
    uint16_t id;
    int len = find_slot(fs, key, &id, record, recovered);
    if (len < 0) {
        return len;
    }
    *value = (dp_stored_value_t){
        .type = record[10], .width = record[11], .sequence = sys_get_le32(record + 12)};
    for (size_t w = 0; w < value->width; ++w) {
        value->words[w] = sys_get_le16(record + DP_STORE_VALUE_HEADER + w * 2);
    }
    return 0;
}

int dp_store_validate_image(const uint8_t *image, size_t length)
{
    if (length < DP_STORE_IMAGE_HEADER || sys_get_le32(image) != DP_STORE_IMAGE_MAGIC ||
        sys_get_le16(image + 4) != DP_STORE_VERSION || sys_get_le32(image + 8)) {
        return -EPROTO;
    }
    size_t offset = DP_STORE_IMAGE_HEADER;
    for (size_t i = 0; i < sys_get_le16(image + 6); ++i) {
        if (offset + 6 > length || !sys_get_le16(image + offset) ||
            !sys_get_le16(image + offset + 2) ||
            !valid_value(image[offset + 4], image[offset + 5]) ||
            offset + 6 + 2U * image[offset + 5] > length) {
            return -EPROTO;
        }
        for (size_t at = DP_STORE_IMAGE_HEADER; at < offset; at += 6 + 2U * image[at + 5]) {
            if (sys_get_le16(image + at) == sys_get_le16(image + offset) &&
                sys_get_le16(image + at + 2) == sys_get_le16(image + offset + 2)) {
                return -EEXIST;
            }
        }
        offset += 6 + 2U * image[offset + 5];
    }
    return offset == length ? 0 : -EPROTO;
}

static int apply_image(struct nvs_fs *fs, const uint8_t *image, size_t length)
{
    int err = dp_store_validate_image(image, length);
    if (err) {
        return err;
    }
    uint8_t record[VALUE_MAX];
    uint32_t sequence = sys_get_le32(image + 12);
    for (size_t at = DP_STORE_IMAGE_HEADER; at < length; at += 6 + 2U * image[at + 5]) {
        dp_key_t key = {sys_get_le16(image + at), sys_get_le16(image + at + 2)};
        uint16_t id;
        bool recovered = false;
        err = find_slot(fs, key, &id, record, &recovered);
        if (err < 0 && err != -ENOENT) {
            return err;
        }
        sys_put_le32(VALUE_MAGIC, record);
        sys_put_le16(DP_STORE_VERSION, record + 4);
        sys_put_le16(key.device_id, record + 6);
        sys_put_le16(key.signal_id, record + 8);
        record[10] = image[at + 4];
        record[11] = image[at + 5];
        sys_put_le32(sequence, record + 12);
        memcpy(record + DP_STORE_VALUE_HEADER, image + at + 6, 2U * record[11]);
        ssize_t rc = nvs_write(fs, id, record, DP_STORE_VALUE_HEADER + 2U * record[11]);
        if (rc < 0) {
            return rc;
        }
    }
    uint8_t meta[12] = {0};
    sys_put_le32(META_MAGIC, meta);
    sys_put_le16(DP_STORE_VERSION, meta + 4);
    sys_put_le32(sequence, meta + 8);
    ssize_t rc = nvs_write(fs, DP_STORE_META_ID, meta, sizeof(meta));
    if (rc < 0) {
        return rc;
    }
    return nvs_delete(fs, DP_STORE_COMMIT_ID);
}

int dp_store_commit(struct nvs_fs *fs, const uint8_t *image, size_t length)
{
    int err = dp_store_validate_image(image, length);
    if (err) {
        return err;
    }
    uint8_t probe;
    ssize_t rc = nvs_read(fs, DP_STORE_COMMIT_ID, &probe, sizeof(probe));
    if (rc != -ENOENT) {
        return rc < 0 ? (int)rc : -EBUSY;
    }
    /* 数值未变化时，重复刷新不应磨损 Flash。 */
    if (!sys_get_le16(image + 6)) {
        uint8_t meta[12];
        rc = nvs_read(fs, DP_STORE_META_ID, meta, sizeof(meta));
        if (rc == sizeof(meta) && sys_get_le32(meta) == META_MAGIC &&
            sys_get_le16(meta + 4) == DP_STORE_VERSION && !sys_get_le16(meta + 6) &&
            sys_get_le32(meta + 8) == sys_get_le32(image + 12)) {
            return 0;
        }
        if (rc < 0 && rc != -ENOENT) {
            return rc;
        }
    }
    rc = nvs_write(fs, DP_STORE_COMMIT_ID, image, length);
    return rc < 0 ? (int)rc : apply_image(fs, image, length);
}

int dp_store_open(struct nvs_fs *fs,
    uint8_t *scratch,
    size_t capacity,
    uint32_t *sequence,
    bool *present,
    bool *recovered)
{
    ssize_t len = nvs_read(fs, DP_STORE_COMMIT_ID, scratch, capacity);
    if (len != -ENOENT) {
        if (len < 0 || len > capacity) {
            return len < 0 ? (int)len : -EMSGSIZE;
        }
        int err = apply_image(fs, scratch, len);
        if (err) {
            return err;
        }
        *recovered = true;
    }
    uint8_t meta[12];
    len = nvs_read(fs, DP_STORE_META_ID, meta, sizeof(meta));
    if (len == -ENOENT) {
        *present = false;
        *sequence = 0;
        return 0;
    }
    if (len < 0) {
        return len;
    }
    if (len != sizeof(meta) || sys_get_le32(meta) != META_MAGIC ||
        sys_get_le16(meta + 4) != DP_STORE_VERSION || sys_get_le16(meta + 6)) {
        return -EPROTO;
    }
    *present = true;
    *sequence = sys_get_le32(meta + 8);
    return 0;
}
