#ifndef DP_PARAMETER_STORE_INTERNAL_H
#define DP_PARAMETER_STORE_INTERNAL_H

#include <dataprocess/points.h>
#include <zephyr/kvss/nvs.h>

/* ID 2 为南向意图；普通参数不使用点表下标作为存储 ID。 */
#define DP_STORE_META_ID 3U
#define DP_STORE_COMMIT_ID 4U
#define DP_STORE_IMAGE_MAGIC 0x50554449U
#define DP_STORE_VERSION 2U
#define DP_STORE_IMAGE_HEADER 16U
#define DP_STORE_VALUE_HEADER 16U
#define DP_STORE_MAX_WORDS 72U

typedef struct {
    uint8_t type, width;
    uint32_t sequence;
    uint16_t words[DP_STORE_MAX_WORDS];
} dp_stored_value_t;

/* 调用方串行化。先完成有效提交日志，再读取参数；格式错误交由调用方重新初始化分区。 */
int dp_store_open(struct nvs_fs *fs,
    uint8_t *scratch,
    size_t capacity,
    uint32_t *sequence,
    bool *present,
    bool *recovered);
int dp_store_read(struct nvs_fs *fs,
    uint16_t device_id,
    uint16_t signal_id,
    dp_stored_value_t *value,
    bool *recovered);
/* image 为自描述的键/类型/长度/值列表，仅存本次变化和新建的参数。
 * 提交日志成功后即决定整组新值；写参数中断时下次打开幂等完成，不发送硬件操作。
 */
int dp_store_validate_image(const uint8_t *image, size_t length);
int dp_store_commit(struct nvs_fs *fs, const uint8_t *image, size_t length);

#endif
