/* 通用 DP 批量质量与转发测试夹具，不参与产品采样。 */
#ifndef TEST_LOCAL_SAMPLE_H
#define TEST_LOCAL_SAMPLE_H
#include <dataprocess/points.h>
#include <string.h>
#define SAMPLE_ANALOG_COUNT 8
struct test_local_sample {
    uint16_t di;
    uint16_t raw[SAMPLE_ANALOG_COUNT];
    uint8_t address;
    bool valid;
};
/* 仅为通用 DP 测试构造输入值，独立于产品采样实现。 */
static uint16_t test_current_value(uint16_t raw)
{
    uint64_t value = (uint64_t)raw * 3300 * 50 * 100 / (4096 * 240);
    return value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}
static int test_publish_sample(const struct test_local_sample *snapshot)
{
    static struct dp_point_batch storage;
    struct dp_point_batch *batch = &storage;
    const size_t count = 2 + SAMPLE_ANALOG_COUNT;
    memset(batch, 0, sizeof(*batch));
    batch->source = DP_SOURCE_LOCAL;
    batch->count = count;
    struct dp_point_update *values = batch->points;
    values[0] = (struct dp_point_update){
        .key = {DEVICE_LOCAL, SIGNAL_ADDRESS}, .width = 1,
        .value.words = {snapshot->address}, .error = snapshot->valid ? 0 : -EIO,
    };
    values[1] = (struct dp_point_update){
        .key = {DEVICE_LOCAL, SIGNAL_DI}, .width = 1,
        .value.words = {snapshot->di}, .error = snapshot->valid ? 0 : -EIO,
    };
    for (unsigned i = 0; i < SAMPLE_ANALOG_COUNT; ++i) {
        values[i + 2] = (struct dp_point_update){
            .key = {DEVICE_LOCAL, SIGNAL_CURRENT_1 + i}, .width = 1,
            .value.words = {test_current_value(snapshot->raw[i])},
            .error = !snapshot->valid ? -EIO : snapshot->raw[i] > 4095 ? -ERANGE : 0,
        };
    }
    return dp_publish(batch, K_MSEC(20));
}
#endif
