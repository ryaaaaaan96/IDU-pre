#include "../../fixtures/dp_tasks.h"
#include <dataprocess/points.h>
#include <transmit/transmit.h>
#include <zephyr/ztest.h>
#include <errno.h>

BUILD_ASSERT(!IS_ENABLED(CONFIG_DP_PROCESS));
BUILD_ASSERT(!IS_ENABLED(CONFIG_DP_PARAMS));
BUILD_ASSERT(!IS_ENABLED(CONFIG_TR_MODBUS_ADAPTER));
BUILD_ASSERT(!IS_ENABLED(CONFIG_FLASH));

static int scale(const struct dp_point_cfg *cfg, struct dp_value *value,
                 const struct dp_value *old)
{
    ARG_UNUSED(cfg);
    ARG_UNUSED(old);
    value->words[0] *= 2;
    return 0;
}

static int unused_handler(struct dp_process_ctx *ctx, struct dp_key key)
{
    ARG_UNUSED(ctx);
    ARG_UNUSED(key);
    return 0;
}

static const struct dp_point_cfg point = {
    .key = {1, 1}, .source = DP_SOURCE_LOCAL, .type = DP_U16,
    .width = 1, .maximum = 1000, .process = scale,
};
static const struct dp_transmit_cfg map = {
    .function = 4, .address = 1000, .count = 1, .key = {1, 1},
};
static const struct dp_equip_cfg equipment = {
    .points = &point, .point_count = 1, .transmit = &map, .transmit_count = 1,
};

static void *setup(void)
{
    zassert_ok(dp_init_config(&equipment)); fixture_start_dp();
    return NULL;
}

static void publish(int error)
{
    const struct dp_point_update value = {
        .key = {1, 1}, .width = 1, .value.words = {7}, .error = error,
    };
    uint32_t ticket;
    zassert_ok(dp_submit_sig_values(&value, 1, K_MSEC(20), &ticket));
    zassert_ok(dp_wait_processed(ticket, K_SECONDS(1)));
}

ZTEST(pipeline_basic, test_disabled_business_handlers_rejected)
{
    struct dp_point_cfg configured = point;
    struct dp_equip_cfg cfg = equipment;
    cfg.points = &configured;
    configured.handler = unused_handler;
    zassert_equal(dp_validate(&cfg), -ENOTSUP);
    configured.handler = NULL;
    configured.read_callback = true;
    zassert_equal(dp_validate(&cfg), -ENOTSUP);
}

ZTEST(pipeline_basic, test_pure_transform_and_modbus_callbacks)
{
    publish(0);
    uint16_t value;
    zassert_ok(tr_callbacks()->input_reg_rd(1000, &value));
    zassert_equal(value, 14);
}

ZTEST(pipeline_basic, test_transport_error_invalidates_value)
{
    publish(0);
    publish(-ETIMEDOUT);
    struct dp_point_state state;
    zassert_ok(dp_read(point.key, &state));
    zassert_equal(state.value.words[0], 14);
    zassert_equal(state.quality, DP_QUALITY_COMM_ERROR);
    uint16_t value;
    zassert_equal(tr_callbacks()->input_reg_rd(1000, &value), -EIO);
}

ZTEST_SUITE(pipeline_basic, NULL, setup, NULL, NULL, NULL);
