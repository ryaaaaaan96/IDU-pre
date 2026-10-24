#include <dataprocess/points.h>
#include <zephyr/ztest.h>
#include <errno.h>

static bool fail_allocation, fail_after_allocation, reader_failed;
static size_t requested;
static unsigned allocations, releases;
static unsigned fail_nth;

void *__real_k_malloc(size_t size);
void __real_k_free(void *ptr);
void *__wrap_k_malloc(size_t size)
{
    ++allocations;
    requested = size;
    if (fail_nth && --fail_nth == 0) { return NULL; }
    if (fail_allocation) { return NULL; }
    void *ptr = __real_k_malloc(size);
    if (ptr && fail_after_allocation) { reader_failed = true; }
    return ptr;
}
void __wrap_k_free(void *ptr)
{
    if (ptr) { ++releases; }
    __real_k_free(ptr);
}

static const struct dp_point_cfg points[] = {
    {.key = {10, 100}, .source = DP_SOURCE_LOCAL, .type = DP_U16,
     .width = 1, .maximum = UINT16_MAX, .initial.words = {11}},
    {.key = {11, 100}, .source = DP_SOURCE_LOCAL, .type = DP_U16,
     .width = 1, .maximum = UINT16_MAX, .initial.words = {22}},
};
static int get_point(const struct dp_equip_cfg *cfg, size_t index, struct dp_point_cfg *out)
{
    ARG_UNUSED(cfg);
    if (reader_failed) { return -EIO; }
    *out = points[index];
    return 0;
}
static const struct dp_equip_cfg config = {
    .point_count = ARRAY_SIZE(points), .point_get = get_point,
};

ZTEST(point_storage, test_allocation_failure_cleanup_and_retry)
{
    fail_allocation = true;
    zassert_equal(dp_init_config(&config), -ENOMEM);
    zassert_is_null(dp_config());
    zassert_equal(requested, 4); /* 实际存储两个 U16 值，不按最大容量分配。 */
    zassert_equal(releases, 0);

    fail_allocation = false;
    fail_after_allocation = true;
    zassert_equal(dp_init_config(&config), -EINVAL);
    zassert_is_null(dp_config());
    zassert_equal(releases, 1);

    fail_after_allocation = reader_failed = false;
    fail_nth = 2; /* 测点值分配成功后，任务批次分配失败。 */
    zassert_equal(dp_init_config(&config), -ENOMEM);
    zassert_is_null(dp_config());
    zassert_equal(releases, 2);
    zassert_ok(dp_init_config(&config));
    struct dp_point_state first, second;
    zassert_ok(dp_read((struct dp_key){10, 100}, &first));
    zassert_ok(dp_read((struct dp_key){11, 100}, &second));
    zassert_equal(first.value.words[0], 11);
    zassert_equal(second.value.words[0], 22);
    unsigned before = allocations;
    zassert_equal(dp_init_config(&config), -EALREADY);
    zassert_equal(allocations, before);
}
ZTEST_SUITE(point_storage, NULL, NULL, NULL, NULL, NULL);
