/* Bounded SGZ/IAP3 container reader for the active MV-IO ARM upgrade path.
 * Format reference: io-interface-board Iapv3 (SUNGROW, 1997-2022),
 * QuickLZ 1.5 level 3, no streaming history. No legacy allocator/RTOS code.
 */
#include "iap/idu_sgz.h"
#include <quicklz/quicklz.h>
#include "legacy_aes_key.h"
#include <errno.h>
#include <string.h>
#include <tinycrypt/aes.h>
#include <tinycrypt/sha256.h>
#include <tinycrypt/ecc_dsa.h>

static uint32_t le(const uint8_t *p, size_t n)
{
    uint32_t v = 0;
    for (size_t i = 0; i < n; ++i) { v |= (uint32_t)p[i] << (8 * i); }
    return v;
}

uint32_t idu_sgz_crc32(uint32_t crc, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) { crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1))); }
    }
    return ~crc;
}

int idu_sgz_decrypt(uint8_t *data, size_t len, uint32_t offset)
{
    if (!data || !len || len % 16 || offset % 16) { return -EINVAL; }
    struct tc_aes_key_sched_struct key;
    if (!tc_aes128_set_decrypt_key(&key, idu_legacy_aes_key)) { return -EIO; }
    for (size_t i = offset ? 0 : 16; i < len; i += 16) {
        if (!tc_aes_decrypt(data + i, data + i, &key)) { return -EIO; }
    }
    return 0;
}

static int check_crc(idu_image_read_fn read, void *ctx, uint32_t offset,
                     uint32_t length, uint32_t expected, uint8_t buf[2048])
{
    uint32_t crc = 0;
    while (length) {
        size_t n = length < 2048 ? length : 2048;
        int err = read(ctx, offset, buf, n);
        if (err) { return err; }
        crc = idu_sgz_crc32(crc, buf, n); offset += n; length -= n;
    }
    return crc == expected ? 0 : -EBADMSG;
}

int idu_sgz_unpack(idu_image_read_fn read, void *in, uint32_t received,
                   idu_image_write_fn write, void *out, struct idu_sgz_workspace *ws,
                   uint32_t *image_size)
{
    uint8_t hdr[132];
    if (!read || !write || !ws || !image_size || received < sizeof(hdr) ||
        received > IDU_SGZ_MAX_SIZE || received % 16) { return -EINVAL; }
    int err = read(in, 0, hdr, sizeof(hdr));
    if (err) { return err; }
    uint32_t length = le(hdr + 20, 4), head = le(hdr + 24, 4), nums = le(hdr + 28, 4);
    if (memcmp(hdr, "SungrowIAP3.0", 12) || length < sizeof(hdr) || length > received ||
        received - length >= 16 || head < sizeof(hdr) || head > length) { return -EBADMSG; }
    /* The fitted MV-IO only upgrades its ARM host (UPGRADE_DOWN=0). */
    if (nums != 1 || le(hdr + 32, 4) != 2) { return -ENOTSUP; }
    uint32_t size = le(hdr + 40, 4), crc = le(hdr + 44, 4);
    uint32_t packed = le(hdr + 120, 4), start = le(hdr + 128, 4);
    if (!size || size > IDU_IMAGE_MAX_SIZE || start < head || start > length ||
        !packed || packed > length - start || start + packed != length) { return -EFBIG; }
    err = check_crc(read, in, 20, length - 20, le(hdr + 16, 4), ws->plain);
    if (!err) { err = check_crc(read, in, start, packed, le(hdr + 124, 4), ws->plain); }
    if (err) { return err; }
    uint32_t produced = 0, actual_crc = 0;
    while (packed) {
        uint8_t count[2];
        if (packed < 2) { return -EBADMSG; }
        err = read(in, start, count, 2);
        if (err) { return err; }
        uint32_t n = le(count, 2); start += 2; packed -= 2;
        if (n < 3 || n > sizeof(ws->compressed) || n > packed) { return -EBADMSG; }
        err = read(in, start, ws->compressed, n);
        if (err) { return err; }
        int decoded = qlz_decode_level3(ws->compressed, n, ws->plain, sizeof(ws->plain));
        if (decoded < 0) { return decoded; }
        if ((uint32_t)decoded > size - produced) { return -EFBIG; }
        actual_crc = idu_sgz_crc32(actual_crc, ws->plain, decoded);
        err = write(out, ws->plain, decoded);
        if (err) { return err; }
        produced += decoded; start += n; packed -= n;
    }
    if (produced != size || actual_crc != crc) { return -EBADMSG; }
    *image_size = size;
    return 0;
}

static int signature_decode(const uint8_t *der, size_t len, uint8_t signature[64])
{
    if (len < 8 || len > 72 || der[0] != 0x30 || der[1] != len - 2) { return -EBADMSG; }
    size_t pos = 2;
    memset(signature, 0, 64);
    for (int i = 0; i < 2; ++i) {
        if (len - pos < 3 || der[pos++] != 2) { return -EBADMSG; }
        size_t n = der[pos++];
        if (!n || n > 33 || n > len - pos || (der[pos] & 0x80)) { return -EBADMSG; }
        if (n > 1 && !der[pos]) {
            if (!(der[pos + 1] & 0x80)) { return -EBADMSG; }
            ++pos; --n;
        }
        if (n > 32) { return -EBADMSG; }
        memcpy(signature + i * 32 + 32 - n, der + pos, n); pos += n;
    }
    return pos == len ? 0 : -EBADMSG;
}

int idu_image_verify(idu_image_read_fn read, void *ctx, uint32_t available,
                      const uint8_t public_key[64], const uint8_t key_hash[32],
                      uint8_t image_hash[32])
{
    uint8_t buf[256], signature[64], expected[32];
    if (!read || !public_key || !key_hash || !image_hash || available < 544 ||
        available > IDU_IMAGE_MAX_SIZE) { return -EINVAL; }
    int err = read(ctx, 0, buf, 32);
    if (err) { return err; }
    uint32_t size = le(buf + 12, 4), protected = le(buf + 10, 2);
    if (le(buf, 4) != 0x96f3b83d || le(buf + 4, 4) || le(buf + 8, 2) != 512 ||
        le(buf + 16, 4) || protected || size < 8 || size > available - 516) { return -ENOEXEC; }
    uint32_t end = 512 + size;
    err = read(ctx, 512, buf, 8);
    if (err) { return err; }
    uint32_t sp = le(buf, 4), pc = le(buf + 4, 4);
    if (sp <= 0x20000000 || sp > 0x20020000 || sp % 8 || !(pc & 1) ||
        (pc & ~1u) < 0x08010200 || (pc & ~1u) >= 0x08010000 + end) { return -ENOEXEC; }
    struct tc_sha256_state_struct hash;
    tc_sha256_init(&hash);
    for (uint32_t off = 0; off < end;) {
        size_t n = end - off < sizeof(buf) ? end - off : sizeof(buf);
        err = read(ctx, off, buf, n);
        if (err) { return err; }
        tc_sha256_update(&hash, buf, n); off += n;
    }
    tc_sha256_final(image_hash, &hash);
    err = read(ctx, end, buf, 4);
    if (err) { return err; }
    uint32_t tlv_size = le(buf + 2, 2);
    if (le(buf, 2) != 0x6907 || tlv_size < 4 || tlv_size > available - end) { return -EBADMSG; }
    unsigned found = 0;
    for (uint32_t pos = end + 4; pos < end + tlv_size;) {
        if (end + tlv_size - pos < 4) { return -EBADMSG; }
        err = read(ctx, pos, buf, 4);
        if (err) { return err; }
        uint32_t type = le(buf, 2), n = le(buf + 2, 2); pos += 4;
        if (n > end + tlv_size - pos || n > sizeof(buf)) { return -EBADMSG; }
        err = read(ctx, pos, buf, n);
        if (err) { return err; }
        if (type == 0x10 && n == 32 && !(found & 1)) {
            memcpy(expected, buf, 32); found |= 1;
        } else if (type == 1 && n == 32 && !(found & 2)) {
            if (memcmp(buf, key_hash, 32)) { return -EACCES; }
            found |= 2;
        } else if (type == 0x22 && !(found & 4)) {
            err = signature_decode(buf, n, signature);
            if (err) { return err; }
            found |= 4;
        } else { return -ENOTSUP; }
        pos += n;
    }
    if (found != 7 || memcmp(image_hash, expected, 32) ||
        !uECC_verify(public_key, image_hash, 32, signature, uECC_secp256r1())) { return -EACCES; }
    return 0;
}
