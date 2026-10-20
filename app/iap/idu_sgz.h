#ifndef IDU_SGZ_H
#define IDU_SGZ_H
#include <stddef.h>
#include <stdint.h>

#define IDU_SGZ_MAX_SIZE 0xf0000u
#define IDU_IMAGE_MAX_SIZE 0x6e000u
/* Reader/writer callbacks must complete the entire operation or return errno. */
typedef int (*idu_image_read_fn)(void *ctx, uint32_t off, void *buf, size_t len);
typedef int (*idu_image_write_fn)(void *ctx, const void *buf, size_t len);
struct idu_sgz_workspace { uint8_t compressed[2304], plain[2048]; };
uint32_t idu_sgz_crc32(uint32_t crc, const void *buf, size_t len);
int idu_sgz_decrypt(uint8_t *data, size_t len, uint32_t file_offset);
int idu_sgz_unpack(idu_image_read_fn read, void *in, uint32_t received,
                   idu_image_write_fn write, void *out, struct idu_sgz_workspace *ws,
                   uint32_t *image_size);
/* Verifies this board's vectors, MCUboot SHA256, key hash and ECDSA-P256 TLVs.
 * Returns the signed image's SHA256 for the persistent upgrade journal. */
int idu_image_verify(idu_image_read_fn read, void *ctx, uint32_t available,
                      const uint8_t public_key[64], const uint8_t key_hash[32],
                      uint8_t image_hash[32]);
#endif
