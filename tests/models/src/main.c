#include "mvio_model.h"
#include <collect/collect.h>
#include <transmit/transmit.h>
#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

IDU_MVIO_COLLECT_FIELDS(peer);
static const struct dp_point_cfg points[] = {
    IDU_MVIO_POINTS(10, 2000, 15000),
    IDU_MVIO_POINTS(20, 2000, 15000),
};
static const struct dp_device_cfg devices[] = {
    {.dev_id = 10, .bus_id = 0, .unit_id = 1},
    {.dev_id = 20, .bus_id = 1, .unit_id = 1},
};
static const struct co_cfg collect[] = {
    IDU_MVIO_COLLECT(peer, 10, 500, 5000),
    IDU_MVIO_COLLECT(peer, 20, 750, 6000),
};
static const struct dp_transmit_cfg transmit[] = {
    IDU_MVIO_TRANSMIT(10, 10000, 30460),
    IDU_MVIO_TRANSMIT(20, 10100, 30462),
};
static const struct dp_equip_cfg cfg = {
    .points = points, .point_count = ARRAY_SIZE(points),
    .devices = devices, .device_count = ARRAY_SIZE(devices),
    .collect = collect, .collect_count = ARRAY_SIZE(collect),
    .transmit = transmit, .transmit_count = ARRAY_SIZE(transmit),
};

struct peer_state {
    uint16_t value;
    int error;
};
static struct peer_state peers[] = {{.value = 17}, {.value = 29}};

static int transfer(void *user, struct modbus_adu *adu)
{
    struct peer_state *peer_state = user;
    zassert_equal(adu->unit_id, 1);
    zassert_equal(adu->length, 4);
    uint16_t address = sys_get_be16(adu->data);
    uint16_t count = sys_get_be16(adu->data + 2);
    if (peer_state->error) { return peer_state->error; }
    zassert_true(count <= 8);
    if (address == 30459) { zassert_equal(adu->fc, 3); }
    else { zassert_equal(adu->fc, 4); }
    adu->length = 1 + count * 2;
    adu->data[0] = count * 2;
    for (size_t i = 0; i < count; ++i) {
        sys_put_be16(peer_state->value + i, adu->data + 1 + 2 * i);
    }
    return 0;
}

static void *setup(void)
{
    zassert_ok(dp_init(&cfg));
    for (size_t i = 0; i < ARRAY_SIZE(peers); ++i) {
        struct co_bus_cfg bus = {
            .id = i, .iface = -1, .simulated = i == 0,
            .transfer = transfer, .user = &peers[i],
        };
        zassert_ok(co_init_bus(&cfg, &bus));
    }
    return NULL;
}

static void poll_and_wait(size_t block, int expected)
{
    struct dp_key key = {collect[block].dev_id, collect[block].fields[0].sig_id};
    struct dp_point_state state;
    zassert_ok(dp_read(key, &state));
    uint32_t old = state.generation;
    zassert_equal(co_poll(block), expected);
    for (size_t attempt = 0; attempt < 100; ++attempt) {
        zassert_ok(dp_read(key, &state));
        if (state.generation != old) { return; }
        k_msleep(1);
    }
    zassert_unreachable("model collection was not consumed");
}

ZTEST(models, test_instances_share_fields_and_keep_independent_windows)
{
    zassert_equal(cfg.point_count, 22);
    zassert_equal(cfg.collect_count, 8);
    zassert_equal(cfg.transmit_count, 44);
    zassert_equal(collect[0].fields, collect[4].fields);
    zassert_equal(collect[1].fields, collect[5].fields);
    poll_and_wait(0, 0);
    poll_and_wait(4, 0);
    uint16_t first, second;
    zassert_ok(tr_read(0, 4, 10000, &first, 1));
    zassert_ok(tr_read(0, 4, 10100, &second, 1));
    zassert_equal(first, 17);
    zassert_equal(second, 29);
    zassert_ok(tr_read(0, 4, 10009, &first, 1));
    zassert_ok(tr_read(0, 4, 10109, &second, 1));
    zassert_equal(first, DP_QUALITY_GOOD | DP_QUALITY_SIMULATED);
    zassert_equal(second, DP_QUALITY_GOOD);
}

ZTEST(models, test_collect_process_transmit_widths_and_addresses)
{
    poll_and_wait(1, 0);
    poll_and_wait(2, 0);
    poll_and_wait(3, 0);
    uint16_t words[8];
    zassert_ok(tr_read(0, 4, 10001, words, 8));
    for (size_t i = 0; i < 8; ++i) { zassert_equal(words[i], 17 + i); }
    zassert_ok(tr_read(0, 4, 10010, words, 2));
    zassert_equal(words[0], 17);
    zassert_equal(words[1], 18);
    zassert_ok(tr_read(0, 3, 30460, words, 1));
    zassert_equal(words[0], 17);
    zassert_equal(tr_read(0, 4, 10012, words, 1), -ENOENT);
    zassert_equal(tr_read(0, 4, 10023, words, 2), -ENOENT);
}

ZTEST(models, test_transport_failure_does_not_poison_other_instance)
{
    poll_and_wait(0, 0);
    poll_and_wait(4, 0);
    peers[0].error = -ETIMEDOUT;
    poll_and_wait(0, -ETIMEDOUT);
    peers[0].error = 0;
    uint16_t value;
    zassert_equal(tr_read(0, 4, 10000, &value, 1), -ENOTSUP);
    zassert_ok(tr_read(0, 4, 10009, &value, 1));
    zassert_equal(value, DP_QUALITY_COMM_ERROR | DP_QUALITY_SIMULATED);
    zassert_ok(tr_read(0, 4, 10100, &value, 1));
    zassert_equal(value, 29);
    poll_and_wait(0, 0);
    zassert_ok(tr_read(0, 4, 10000, &value, 1));
    zassert_equal(value, 17);
}

ZTEST(models, test_instance_conflicts_are_rejected_before_start)
{
    struct dp_equip_cfg invalid = cfg;
    struct dp_device_cfg changed_devices[ARRAY_SIZE(devices)];
    memcpy(changed_devices, devices, sizeof(devices));
    invalid.devices = changed_devices;
    changed_devices[1].dev_id = 10;
    zassert_equal(dp_validate(&invalid), -EEXIST);
    changed_devices[1] = devices[1];
    changed_devices[1].bus_id = 0;
    zassert_equal(dp_validate(&invalid), -EEXIST);

    struct dp_transmit_cfg changed_maps[ARRAY_SIZE(transmit)];
    memcpy(changed_maps, transmit, sizeof(transmit));
    invalid = cfg;
    invalid.transmit = changed_maps;
    changed_maps[22].address = 10000;
    zassert_equal(dp_validate(&invalid), -EEXIST);
    changed_maps[22] = transmit[22];
    changed_maps[22].key.dev_id = 30;
    zassert_equal(dp_validate(&invalid), -EINVAL);
}

ZTEST_SUITE(models, NULL, setup, NULL, NULL, NULL);
