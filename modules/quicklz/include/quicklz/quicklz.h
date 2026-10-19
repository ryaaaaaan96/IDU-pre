#ifndef QUICKLZ_H
#define QUICKLZ_H

#include <stddef.h>
#include <stdint.h>

/* Decode one complete QuickLZ 1.5 level-3 block, with no streaming history.
 * Both compressed and stored blocks are supported. Buffers must not overlap;
 * cap must fit in int. No allocation or retained state is used.
 * Returns the decoded byte count, or -EBADMSG for malformed input or an
 * undersized destination. On failure, destination contents are unspecified.
 */
int qlz_decode_level3(const uint8_t *src, size_t len, uint8_t *dst, size_t cap);

#endif
