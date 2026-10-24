/* 有界 QuickLZ 1.5 级别 3 解码器，不保留流式历史。 */
#include <quicklz/quicklz.h>
#include <errno.h>
#include <string.h>

static uint32_t le(const uint8_t *p, size_t n)
{
    uint32_t v = 0;
    for (size_t i = 0; i < n; ++i) { v |= (uint32_t)p[i] << (8 * i); }
    return v;
}

int qlz_decode_level3(const uint8_t *src, size_t len, uint8_t *dst, size_t cap)
{
    if (!src || !dst || len < 3 || (src[0] & 0xfc) != 0x4c) { return -EBADMSG; }
    size_t field = (src[0] & 2) ? 4 : 1, header = 1 + 2 * field;
    if (len < header) { return -EBADMSG; }
    size_t size = le(src + 1 + field, field);
    if (le(src + 1, field) != len || !size || size > cap) { return -EBADMSG; }
    if (!(src[0] & 1)) {
        if (len - header != size) { return -EBADMSG; }
        memcpy(dst, src + header, size); return (int)size;
    }
    size_t pos = header, produced = 0;
    uint32_t control = 1;
    while (produced < size) {
        if (control == 1) {
            if (len - pos < 4) { return -EBADMSG; }
            control = le(src + pos, 4); pos += 4;
            if (!(control & 0x80000000u)) { return -EBADMSG; }
        }
        if (!(control & 1)) {
            if (pos == len) { return -EBADMSG; }
            dst[produced++] = src[pos++]; control >>= 1; continue;
        }
        if (pos == len) { return -EBADMSG; }
        uint8_t first = src[pos];
        size_t bytes = !(first & 3) ? 1 : !(first & 2) || !(first & 1) ? 2 :
                       (first & 127) != 3 ? 3 : 4;
        if (len - pos < bytes) { return -EBADMSG; }
        uint32_t token = le(src + pos, bytes), distance, count;
        if (!(first & 3)) { distance = token >> 2; count = 3; }
        else if (!(first & 2)) { distance = token >> 2; count = 3; }
        else if (!(first & 1)) { distance = token >> 6; count = ((token >> 2) & 15) + 3; }
        else if ((first & 127) != 3) {
            distance = token >> 7; count = ((token >> 2) & 31) + 2;
        } else { distance = token >> 15; count = ((token >> 7) & 255) + 3; }
        if (distance < 3 || distance > produced || size - produced < 4 ||
            count > size - produced - 4) { return -EBADMSG; }
        for (size_t i = 0; i < count; ++i) {
            dst[produced] = dst[produced - distance]; ++produced;
        }
        pos += bytes; control >>= 1;
    }
    /* QLZ 会将很短的压缩流填充至九个载荷字节。 */
    if (pos != len && !(len == header + 9 && pos < len)) { return -EBADMSG; }
    return (int)size;
}
