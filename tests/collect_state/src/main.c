#include "../../fixtures/dp_tasks.h"
#include "../../fixtures/co_tasks.h"
#include "../../fixtures/tr_tasks.h"
#include "../../fixtures/point_connections.h"
#include <transmit/transmit.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>

static const struct dp_point_cfg points[] = {
    {.key = {10, 1}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1, .maximum = UINT16_MAX},
    {.key = {10, 2}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1, .maximum = UINT16_MAX},
    {.key = {10, 3}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1, .maximum = UINT16_MAX},
    {.key = {10, 4}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1, .maximum = UINT16_MAX},
    {.key = {10, 5}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1, .maximum = UINT16_MAX},
    {.key = {11, 3}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1, .maximum = UINT16_MAX},
    {.key = {10, 6}, .source = DP_SOURCE_MODBUS, .type = DP_WORDS, .width = 16},
    {.key = {10, 7}, .source = DP_SOURCE_MODBUS, .type = DP_WORDS, .width = 16},
    {.key = {10, 8}, .source = DP_SOURCE_MODBUS, .type = DP_U16, .width = 1, .maximum = UINT16_MAX},
};
static const struct dp_device_cfg devices[] = {
    {.dev_id = 10, .unit_id = 1, .bus_id = 0},
    {.dev_id = 11, .unit_id = 2, .bus_id = 0},
};
static const struct co_field first[] = {{.sig_id = 1, .width = 1}};
static const struct co_field second[] = {{.sig_id = 2, .width = 1}};
static const struct co_field third[] = {{.sig_id = 3, .width = 1}};
static const struct co_field fourth[] = {{.sig_id = 4, .width = 1}};
static const struct co_field fifth[] = {{.sig_id = 5, .width = 1}};
static const struct co_field sixth[] = {{.sig_id = 6, .width = 16}};
static const struct co_field seventh[] = {{.sig_id = 7, .width = 16}};
static const struct co_field eighth[] = {{.sig_id = 8, .width = 1}};
static const struct co_cfg polls[] = {
    {.dev_id = 10, .function = 4, .address = 10, .count = 1, .init_only = true,
     .fields = first, .field_count = 1},
    {.dev_id = 10, .function = 4, .address = 11, .count = 1, .init_only = true,
     .fields = second, .field_count = 1},
    {.dev_id = 10, .function = 4, .address = 100, .count = 1, .period_ms = 20,
     .fields = third, .field_count = 1},
    {.dev_id = 10, .function = 4, .address = 101, .count = 1, .period_ms = 20,
     .fields = fourth, .field_count = 1},
    {.dev_id = 10, .function = 4, .address = 103, .count = 1, .period_ms = 20,
     .fields = fifth, .field_count = 1},
    {.dev_id = 11, .function = 4, .address = 100, .count = 1, .period_ms = 20,
     .fields = third, .field_count = 1},
    {.dev_id = 10, .function = 4, .address = 200, .count = 16, .init_only = true,
     .fields = sixth, .field_count = 1},
    {.dev_id = 10, .function = 4, .address = 216, .count = 16, .init_only = true,
     .fields = seventh, .field_count = 1},
    {.dev_id = 10, .function = 4, .address = 232, .count = 1, .init_only = true,
     .fields = eighth, .field_count = 1},
};
static const struct dp_transmit_cfg maps[] = {
    {.function = 4, .address = 1000, .count = 1, .key = {10, 3}},
    {.function = 4, .address = 1001, .count = 1, .key = {10, 4}},
};
static const struct dp_equip_cfg equipment = {
    .points = points, .point_count = ARRAY_SIZE(points),
    .devices = devices, .device_count = ARRAY_SIZE(devices),
    .collect = polls, .collect_count = ARRAY_SIZE(polls),
    .transmit = maps, .transmit_count = ARRAY_SIZE(maps),
};
struct request_event { uint16_t dev, address, count; };
K_MSGQ_DEFINE(requests, sizeof(struct request_event), 32, 4);

static int transfer(void *user, uint16_t dev, struct modbus_adu *adu)
{
    ARG_UNUSED(user);
    uint16_t address = sys_get_be16(adu->data);
    uint16_t count = sys_get_be16(adu->data + 2);
    struct request_event event = {.dev = dev, .address = address, .count = count};
    zassert_ok(k_msgq_put(&requests, &event, K_NO_WAIT));
    adu->data[0] = count * 2;
    adu->length = 1 + count * 2;
    for (size_t i = 0; i < count; ++i) { sys_put_be16((address + i) * 10, adu->data + 1 + i * 2); }
    return 0;
}

static void *setup(void)
{
    zassert_ok(dp_init_config(&equipment));
    fixture_start_dp();
    fixture_connect_points();
    const struct co_bus_cfg bus = {.id = 0, .iface = -1, .transfer = transfer};
    zassert_ok(co_init_bus(&equipment, &bus));
    return NULL;
}

ZTEST(collect_state, test_a_initialization_combines_contiguous_registers)
{
    zassert_ok(co_initialize_device(10));
    struct request_event event;
    zassert_ok(k_msgq_get(&requests, &event, K_NO_WAIT));
    zassert_equal(event.dev, 10);
    zassert_equal(event.address, 10);
    zassert_equal(event.count, 2);
    zassert_ok(k_msgq_get(&requests, &event, K_NO_WAIT));
    zassert_equal(event.address, 200);
    zassert_equal(event.count, 32);
    zassert_ok(k_msgq_get(&requests, &event, K_NO_WAIT));
    zassert_equal(event.address, 232);
    zassert_equal(event.count, 1);
    zassert_not_equal(k_msgq_get(&requests, &event, K_NO_WAIT), 0);
    struct dp_point_state state;
    zassert_ok(dp_read((struct dp_key){10, 2}, &state));
    zassert_equal(state.quality, DP_QUALITY_GOOD);
    zassert_equal(state.value.words[0], 110);
    zassert_ok(dp_read((struct dp_key){10, 7}, &state));
    zassert_equal(state.value.words[0], 2160);
    zassert_equal(state.value.words[15], 2310);
}

ZTEST(collect_state, test_b_periodic_groups_preserve_gaps_and_device_identity)
{
    zassert_ok(co_start());
    fixture_start_co();
    struct request_event event;
    bool combined = false, gap = false, other = false;
    for (unsigned i = 0; i < 3; ++i) {
        zassert_ok(k_msgq_get(&requests, &event, K_MSEC(50)));
        if (event.dev == 10 && event.address == 100) {
            zassert_equal(event.count, 2);
            combined = true;
        } else if (event.dev == 10 && event.address == 103) {
            zassert_equal(event.count, 1);
            gap = true;
        } else if (event.dev == 11 && event.address == 100) {
            zassert_equal(event.count, 1);
            other = true;
        } else { zassert_unreachable("Unexpected merged request"); }
    }
    zassert_true(combined && gap && other);
    for (size_t i = 0; i < ARRAY_SIZE(polls); ++i) { zassert_ok(co_enable_poll(i, false)); }
    struct dp_point_state state;
    for (unsigned i = 0; i < 50; ++i) {
        zassert_ok(dp_read((struct dp_key){10, 4}, &state));
        if (state.quality == DP_QUALITY_GOOD) { break; }
        k_msleep(1);
    }
    zassert_equal(state.quality, DP_QUALITY_GOOD);
    zassert_equal(state.value.words[0], 1010);
}

ZTEST_SUITE(collect_state, NULL, setup, NULL, NULL, NULL);
