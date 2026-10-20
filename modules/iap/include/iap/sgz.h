#ifndef IAP_SGZ_H
#define IAP_SGZ_H

#include <stddef.h>
#include <stdint.h>

/* Callbacks complete the entire operation or return a negative errno. */
typedef int (*iap_image_read_fn)(void *ctx, uint32_t off, void *buf, size_t len);
typedef int (*iap_image_write_fn)(void *ctx, const void *buf, size_t len);
struct iap_sgz_workspace { uint8_t compressed[2304], plain[2048]; };

/* Cortex-M, 512-byte MCUboot header, SHA256 and ECDSA-P256. max_size includes
 * header and TLVs, excludes reserved swap/trailer space. sram_end admits a
 * stack pointer at the exclusive RAM upper bound. Keys are public material. */
struct iap_image_policy {
    uint32_t max_size;
    uint32_t load_address;
    uint32_t sram_start;
    uint32_t sram_end;
    const uint8_t *public_key; /* 64-byte uncompressed P-256 X || Y */
    const uint8_t *key_hash;   /* 32-byte SHA256 of the DER public key */
};

uint32_t iap_sgz_crc32(uint32_t crc, const void *buf, size_t len);
int iap_sgz_decrypt(uint8_t *data, size_t len, uint32_t file_offset,
                    const uint8_t aes_key[16]);
/* received is bounded by the caller's input storage; max_image_size bounds
 * decompressed output. No heap allocation or Zephyr runtime is required. */
int iap_sgz_unpack(iap_image_read_fn read, void *in, uint32_t received,
                   uint32_t max_image_size, iap_image_write_fn write, void *out,
                   struct iap_sgz_workspace *ws, uint32_t *image_size);
int iap_image_verify(iap_image_read_fn read, void *ctx, uint32_t available,
                     const struct iap_image_policy *policy, uint8_t image_hash[32]);

#endif
