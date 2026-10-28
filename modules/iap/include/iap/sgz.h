/**
 * @file
 * @brief 升级容器解码和镜像校验接口。
 */
#ifndef IAP_SGZ_H
#define IAP_SGZ_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief 回调须完成整个操作，否则返回负 errno。
 */
typedef int (*iap_image_read_fn)(void *ctx, uint32_t off, void *buf, size_t len);
typedef int (*iap_image_write_fn)(void *ctx, const void *buf, size_t len);

struct iap_sgz_workspace {
    uint8_t compressed[2304], plain[2048];
};

/**
 * @brief Cortex-M、512 字节 MCUboot 头、SHA256 和 ECDSA-P256。
 *
 * max_size 包含头和 TLV，不含预留交换及尾部空间。 sram_end 允许栈指针恰好位于 RAM
 * 上界。密钥为公钥材料。
 */
struct iap_image_policy {
    uint32_t max_size;
    uint32_t load_address;
    uint32_t sram_start;
    uint32_t sram_end;
    const uint8_t *public_key; /**< 64 字节非压缩 P-256 公钥 X || Y。 */
    const uint8_t *key_hash;   /**< DER 公钥的 32 字节 SHA256。 */
};

uint32_t iap_sgz_crc32(uint32_t crc, const void *buf, size_t len);
int iap_sgz_decrypt(uint8_t *data, size_t len, uint32_t file_offset, const uint8_t aes_key[16]);
/**
 * @brief received 受调用方输入空间限制， max_image_size 限制解压输出； 不要求堆分配或 Zephyr
 * 运行环境。
 */
int iap_sgz_unpack(iap_image_read_fn read,
    void *in,
    uint32_t received,
    uint32_t max_image_size,
    iap_image_write_fn write,
    void *out,
    struct iap_sgz_workspace *ws,
    uint32_t *image_size);
int iap_image_verify(iap_image_read_fn read,
    void *ctx,
    uint32_t available,
    const struct iap_image_policy *policy,
    uint8_t image_hash[32]);

#endif
