#include "idu_equipment.h"
#include "cfg/devices/device_config.h"
#include "cfg/tables/signal_ids.h"
#include "communication/idu_protocol.h"
#include "communication/ports/idu_acquisition.h"
#include "communication/ports/idu_virtual.h"
#include "hardware/idu_pre.h"
#include "process/engineering.h"
#include "legacy_registers.h"
#include <collect/collect.h>
#include <dataprocess/params.h>
#include <dataprocess/points.h>
#include <transmit/modbus_adapter.h>
#include <transmit/transmit.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

/* Only to verify that bypassing the publication API is rejected. */
ZBUS_CHAN_DECLARE(dp_samples_chan);

static const struct dp_key remote_di = {IDU_DEV_REMOTE_IO, IDU_SIG_DI};
static const struct dp_key local_di = {IDU_DEV_LOCAL, IDU_SIG_DI};
static struct dp_equip_cfg test_cfg;
static struct dp_point_cfg test_points[CONFIG_DP_POINTS_MAX_POINTS];
static struct dp_transmit_cfg test_transmit[80];
static struct idu_snapshot local_sample;
int north_client;
static int north_server;
static int south_client;
static struct modbus_adu north_reply;

static struct dp_point_state state_of(struct dp_key key)
{
    struct dp_point_state state;
    zassert_ok(dp_read(key, &state));
    return state;
}

static void wait_after(struct dp_key key, uint32_t generation)
{
    for (int i = 0; i < 200; ++i) {
        if (state_of(key).generation > generation) {
            return;
        }
        k_msleep(1);
    }
    zassert_unreachable("processor did not consume batch");
}

static void poll_block(size_t block, int expected)
{
    struct dp_key key = {
        test_cfg.collect[block].dev_id, test_cfg.collect[block].fields[0].sig_id,
    };
    uint32_t old = state_of(key).generation;
    zassert_equal(co_poll(block), expected);
    wait_after(key, old);
}

static int north_request(int iface, const struct modbus_adu *adu, void *data)
{
    ARG_UNUSED(iface);
    ARG_UNUSED(data);
    return modbus_raw_submit_rx(north_server, adu);
}

static int north_response(int iface, const struct modbus_adu *adu, void *data)
{
    ARG_UNUSED(iface);
    ARG_UNUSED(data);
    north_reply = *adu;
    return modbus_raw_submit_rx(north_client, adu);
}

/* The build-local adapter also accepts standard one-byte RAW exceptions. */
static void assert_exception(int result, uint8_t function, uint8_t code)
{
    zassert_equal(result, code);
    zassert_equal(north_reply.fc, function | 0x80);
    zassert_equal(north_reply.length, 1);
    zassert_equal(north_reply.data[0], code);
}

static int reject_42(const struct dp_point_cfg *cfg, struct dp_value *value,
                     const struct dp_value *old)
{
    ARG_UNUSED(cfg);
    ARG_UNUSED(old);
    return value->words[0] == 42 ? -ERANGE : 0;
}

void module_add_points(struct dp_equip_cfg *, struct dp_point_cfg *, struct dp_transmit_cfg *);
void module_bus_init(const struct dp_equip_cfg *);

static void *setup(void)
{
    const struct dp_equip_cfg *app_cfg = idu_equipment_config();
    test_cfg = *app_cfg;
    memcpy(test_points, app_cfg->points, app_cfg->point_count * sizeof(test_points[0]));
    test_cfg.points = test_points;
    test_points[test_cfg.point_count++] = (struct dp_point_cfg){
        .key = {1, 11000}, .source = DP_SOURCE_LOCAL, .type = DP_S16,
        .width = 1, .minimum = -1000, .maximum = 1000,
    };
    test_points[test_cfg.point_count++] = (struct dp_point_cfg){
        .key = {1, 11001}, .source = DP_SOURCE_LOCAL, .type = DP_S32,
        .width = 2, .minimum = INT32_MIN, .maximum = INT32_MAX,
    };
    test_points[test_cfg.point_count++] = (struct dp_point_cfg){
        .key = {1, 31100}, .source = DP_SOURCE_PARAMETER, .type = DP_U16,
        .width = 1, .maximum = 1000, .initial.words = {10},
        .param_flags = DP_PARAM_WRITE | DP_PARAM_PERSIST | DP_PARAM_RESTORE,
        .prepare_write = reject_42,
    };
    test_points[test_cfg.point_count++] = (struct dp_point_cfg){
        .key = {1, 31101}, .source = DP_SOURCE_PARAMETER, .type = DP_U16,
        .width = 1, .maximum = 1000, .initial.words = {7},
        .param_flags = DP_PARAM_PERSIST, /* Readable, immutable, not restorable. */
    };
    memcpy(test_transmit, app_cfg->transmit,
           app_cfg->transmit_count * sizeof(test_transmit[0]));
    test_cfg.transmit = test_transmit;
    test_transmit[test_cfg.transmit_count++] = (struct dp_transmit_cfg){
        .function = 4, .address = 11000, .count = 1, .key = {1, 11000},
    };
    test_transmit[test_cfg.transmit_count++] = (struct dp_transmit_cfg){
        .function = 4, .address = 11001, .count = 2, .key = {1, 11001},
        .order = DP_HIGH_WORD_FIRST,
    };
    module_add_points(&test_cfg, test_points, test_transmit);
    zassert_ok(dp_init_config(&test_cfg));
    zassert_equal(state_of(remote_di).quality, DP_QUALITY_UNINITIALIZED);
    uint16_t initial;
    zassert_equal(tr_read_register(4, 10000, &initial), -EIO);
    zassert_ok(tr_read_register(4, 8149, &initial));
    south_client = idu_acquisition_port_init();
    zassert_true(south_client >= 0);
    zassert_ok(co_init(&test_cfg, south_client, true));
    module_bus_init(&test_cfg);
    zassert_ok(dp_params_init(&test_cfg, DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings))));
    north_server = modbus_iface_get_by_name("RAW_2");
    north_client = modbus_iface_get_by_name("RAW_3");
    struct modbus_iface_param server = {
        .mode = MODBUS_MODE_RAW,
        .server = {.unit_id = 16, .user_cb = idu_parameter_callbacks()},
        .rawcb = {.raw_tx_cb = north_response},
    };
    struct modbus_iface_param client = {
        .mode = MODBUS_MODE_RAW, .rx_timeout = 2000000,
        .rawcb = {.raw_tx_cb = north_request},
    };
    zassert_ok(tr_attach(north_server, NULL));
    zassert_ok(tr_modbus_init_server(north_server, server, idu_parameter_write, true));
    zassert_ok(modbus_init_client(north_client, client));
    return NULL;
}

void parameter_before(void);

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    parameter_before();
    idu_virtual_set_online(true);
    zassert_ok(idu_virtual_set_register(4, 8152, 0x0005));
    zassert_ok(idu_virtual_set_register(4, 8150, 0x0101));
    zassert_ok(idu_virtual_set_register(4, 8151, 0x0100));
    zassert_ok(idu_virtual_set_register(3, 30459, 1));
    for (int i = 0; i < 8; ++i) {
        zassert_ok(idu_virtual_set_register(4, 8163 + i, (i + 1) * 100));
    }
    for (size_t i = 0; i < test_cfg.collect_count; ++i) {
        poll_block(i, 0);
    }
    local_sample = (struct idu_snapshot){.di = 0xa55a, .address = 16, .valid = true};
    for (unsigned i = 0; i < IDU_ANALOG_COUNT; ++i) {
        local_sample.raw[i] = i == 7 ? 4095 : i * 256;
        local_sample.current[i] = idu_current_from_raw(local_sample.raw[i]);
    }
    uint32_t old = state_of(local_di).generation;
    zassert_ok(idu_snapshot_publish(&local_sample));
    wait_after(local_di, old);
}

ZTEST(points, test_legacy_registers_preserved)
{
    const uint16_t windows[][2] = {
        {2503, 2532}, {2568, 2571}, {2581, 2595}, {4805, 4814},
        {4989, 5014}, {5486, 5487}, {8149, 8199},
    };
    uint16_t wire[51];
    for (size_t i = 0; i < ARRAY_SIZE(windows); ++i) {
        uint16_t count = windows[i][1] - windows[i][0] + 1;
        zassert_ok(modbus_read_input_regs(north_client, 16, windows[i][0], wire, count));
        for (uint16_t n = 0; n < count; ++n) {
            uint16_t expected;
            zassert_ok(idu_input_register_read(&local_sample, windows[i][0] + n, &expected));
            /* 4814 now exposes the saved address, independently of the DIP. */
            if (windows[i][0] + n == 4814) { expected = 1; }
            zassert_equal(wire[n], expected, "address %u", windows[i][0] + n);
        }
    }
    zassert_ok(modbus_read_holding_regs(north_client, 16, 30459, wire, 1));
    zassert_equal(wire[0], 1);
}

ZTEST(points, test_end_to_end_remote_forwarding)
{
    uint16_t wire[13];
    zassert_ok(modbus_read_input_regs(north_client, 16, 10000, wire, ARRAY_SIZE(wire)));
    zassert_equal(wire[0], 5);
    for (unsigned i = 0; i < 8; ++i) {
        zassert_equal(wire[i + 1], (i + 1) * 100);
    }
    zassert_equal(wire[9], DP_QUALITY_SIMULATED | DP_QUALITY_GOOD);
    zassert_equal(wire[10], 0x0101);
    zassert_equal(wire[11], 0x0100);
    zassert_equal(wire[12], 1);
    zassert_ok(modbus_read_holding_regs(north_client, 16, 30460, wire, 1));
    zassert_equal(wire[0], 1);
    zassert_equal(state_of(local_di).value.words[0], 0xa55a);
    zassert_equal(state_of(local_di).quality, DP_QUALITY_GOOD);
}

ZTEST(points, test_timeout_invalidates_and_recovers)
{
    idu_virtual_set_online(false);
    poll_block(0, -ETIMEDOUT);
    struct dp_point_state state = state_of(remote_di);
    zassert_equal(state.quality, DP_QUALITY_SIMULATED | DP_QUALITY_COMM_ERROR);
    zassert_equal(state.error, -ETIMEDOUT);
    uint16_t value;
    assert_exception(modbus_read_input_regs(north_client, 16, 10000, &value, 1),
                     4, MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_ok(modbus_read_input_regs(north_client, 16, 10009, &value, 1));
    zassert_equal(value, state.quality);
    idu_virtual_set_online(true);
    zassert_ok(idu_virtual_set_register(4, 8152, 0x55aa));
    poll_block(0, 0);
    zassert_ok(modbus_read_input_regs(north_client, 16, 10000, &value, 1));
    zassert_equal(value, 0x55aa);
}

ZTEST(points, test_stale_data_is_not_reported_as_valid)
{
    k_msleep(2001);
    zassert_equal(state_of(remote_di).quality, DP_QUALITY_SIMULATED | DP_QUALITY_STALE);
    uint16_t value;
    zassert_equal(tr_read_register(4, 10000, &value), -EIO);
    zassert_ok(tr_read_register(4, 8149, &value)); /* Constant never expires. */
    poll_block(0, 0);
    zassert_ok(tr_read_register(4, 10000, &value));
}

ZTEST(points, test_process_callback_and_range_rejection)
{
    struct dp_key key = {IDU_DEV_LOCAL, IDU_SIG_CURRENT_1};
    uint32_t old = state_of(key).generation;
    local_sample.raw[0] = 4096;
    zassert_ok(idu_snapshot_publish(&local_sample));
    wait_after(key, old);
    zassert_equal(state_of(key).quality, DP_QUALITY_BAD_VALUE);
    uint16_t value;
    zassert_equal(tr_read_register(4, 8163, &value), -EIO);
    old = state_of(key).generation;
    local_sample.raw[0] = 2048;
    zassert_ok(idu_snapshot_publish(&local_sample));
    wait_after(key, old);
    zassert_ok(tr_read_register(4, 8163, &value));
    zassert_equal(value, 34375);
    /* A syntactically successful Modbus reply may still violate point range. */
    zassert_ok(idu_virtual_set_register(3, 30459, 0));
    poll_block(3, 0);
    key = (struct dp_key){IDU_DEV_REMOTE_IO, IDU_SIG_ADDRESS};
    zassert_equal(state_of(key).quality, DP_QUALITY_SIMULATED | DP_QUALITY_BAD_VALUE);
}

ZTEST(points, test_signed_values_and_word_order)
{
    struct dp_point_batch batch = {
        .source = DP_SOURCE_LOCAL, .count = 2,
        .points = {
            {.key = {1, 11000}, .width = 1, .value.words = {(uint16_t)-123}},
            {.key = {1, 11001}, .width = 2, .value.words = {0x3344, 0x8122}},
        },
    };
    uint32_t old = state_of(batch.points[0].key).generation;
    zassert_ok(dp_publish(&batch, K_MSEC(20)));
    wait_after(batch.points[0].key, old);
    uint16_t wire[3];
    zassert_ok(modbus_read_input_regs(north_client, 16, 11000, wire, 3));
    zassert_equal(wire[0], (uint16_t)-123);
    zassert_equal(wire[1], 0x8122);
    zassert_equal(wire[2], 0x3344);
    zassert_ok(idu_virtual_set_register(4, 8150, 0x3344));
    zassert_ok(idu_virtual_set_register(4, 8151, 0x1122));
    poll_block(2, 0);
    zassert_ok(modbus_read_input_regs(north_client, 16, 10010, wire, 2));
    zassert_equal(wire[0], 0x3344);
    zassert_equal(wire[1], 0x1122);
}

ZTEST(points, test_invalid_batch_cannot_partially_update_store)
{
    struct dp_point_batch batch = {
        .source = DP_SOURCE_LOCAL, .count = 2,
        .points = {
            {.key = {IDU_DEV_LOCAL, IDU_SIG_DI}, .width = 1, .value.words = {0}},
            {.key = {99, 123}, .width = 1},
        },
    };
    struct dp_point_state old = state_of(local_di);
    zassert_equal(dp_publish(&batch, K_NO_WAIT), -ENOENT);
    /* Even direct use of the exposed zbus channel is validated. */
    zassert_true(zbus_chan_pub(&dp_samples_chan, &batch, K_NO_WAIT) < 0);
    batch.count = 1;
    batch.source = DP_SOURCE_MODBUS;
    zassert_equal(dp_publish(&batch, K_NO_WAIT), -EINVAL);
    batch.source = DP_SOURCE_LOCAL;
    batch.points[0].key.sig_id = 8149; /* Cannot overwrite constant identity. */
    zassert_equal(dp_publish(&batch, K_NO_WAIT), -EINVAL);
    k_msleep(2);
    zassert_equal(state_of(local_di).generation, old.generation);
    zassert_equal(state_of(local_di).value.words[0], old.value.words[0]);
}

ZTEST(points, test_burst_messages_are_processed_individually)
{
    struct dp_point_batch batch = {
        .source = DP_SOURCE_LOCAL, .count = 1,
        .points = {{.key = {IDU_DEV_LOCAL, IDU_SIG_DI}, .width = 1}},
    };
    uint32_t old = state_of(local_di).generation;
    for (unsigned i = 0; i < 20; ++i) {
        batch.points[0].value.words[0] = i;
        zassert_ok(dp_publish(&batch, K_MSEC(100)));
    }
    for (unsigned i = 0; i < 200 && state_of(local_di).generation < old + 20; ++i) {
        k_msleep(1);
    }
    struct dp_point_state state = state_of(local_di);
    zassert_equal(state.generation, old + 20);
    zassert_equal(state.value.words[0], 19);
}

ZTEST(points, test_unknown_register_and_writes_rejected)
{
    uint16_t value = 0;
    assert_exception(modbus_read_input_regs(north_client, 16, 10013, &value, 1),
                     4, MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_ok(modbus_write_holding_reg(north_client, 16, 30459, 2));
    assert_exception(modbus_write_holding_regs(north_client, 16, 30459, &value, 1),
                     16, MODBUS_EXC_ILLEGAL_DATA_VAL);
    zassert_equal(modbus_read_input_regs(north_client, 17, 10000, &value, 1), -ETIMEDOUT);
    /* Valid RAW exception now reaches the client as a protocol exception. */
    zassert_equal(modbus_read_input_regs(south_client, 1, 99, &value, 1),
                  MODBUS_EXC_ILLEGAL_DATA_ADDR);
}

ZTEST(points, test_queue_full_returns_error_and_recovers)
{
    struct dp_point_batch batch = {
        .source = DP_SOURCE_LOCAL, .count = 1,
        .points = {{.key = {IDU_DEV_LOCAL, IDU_SIG_DI}, .width = 1}},
    };
    uint32_t old = state_of(local_di).generation;
    int results[CONFIG_DP_POINTS_QUEUE_DEPTH + 1];
    k_sched_lock();
    for (size_t i = 0; i < ARRAY_SIZE(results); ++i) {
        batch.points[0].value.words[0] = i;
        results[i] = dp_publish(&batch, K_NO_WAIT);
    }
    k_sched_unlock();
    for (int i = 0; i < CONFIG_DP_POINTS_QUEUE_DEPTH; ++i) {
        zassert_ok(results[i]);
    }
    zassert_true(results[CONFIG_DP_POINTS_QUEUE_DEPTH] < 0);
    for (int i = 0; i < 200 && state_of(local_di).generation <
         old + CONFIG_DP_POINTS_QUEUE_DEPTH; ++i) {
        k_msleep(1);
    }
    zassert_equal(state_of(local_di).generation, old + CONFIG_DP_POINTS_QUEUE_DEPTH);
    zassert_equal(state_of(local_di).value.words[0], CONFIG_DP_POINTS_QUEUE_DEPTH - 1);
    old = state_of(local_di).generation;
    zassert_ok(dp_publish(&batch, K_MSEC(20)));
    wait_after(local_di, old);
}

ZTEST(points, test_positive_modbus_exception_invalidates_sample)
{
    struct dp_point_batch batch = {
        .source = DP_SOURCE_MODBUS, .flags = DP_QUALITY_SIMULATED, .count = 1,
        .points = {{.key = {IDU_DEV_REMOTE_IO, IDU_SIG_DI}, .width = 1,
                    .error = MODBUS_EXC_ILLEGAL_DATA_ADDR}},
    };
    uint32_t old = state_of(remote_di).generation;
    zassert_ok(dp_publish(&batch, K_MSEC(20)));
    wait_after(remote_di, old);
    zassert_equal(state_of(remote_di).quality, DP_QUALITY_SIMULATED | DP_QUALITY_COMM_ERROR);
    zassert_equal(state_of(remote_di).error, MODBUS_EXC_ILLEGAL_DATA_ADDR);
}

ZTEST(points, test_configuration_validation)
{
    struct dp_equip_cfg cfg = *idu_equipment_config();
    zassert_ok(dp_validate(&cfg));
    struct dp_point_cfg points[2] = {cfg.points[0], cfg.points[0]};
    struct dp_equip_cfg small = {.points = points, .point_count = 2};
    zassert_equal(dp_validate(&small), -EEXIST);
    small.point_count = 1;
    points[0].width = 2;
    zassert_equal(dp_validate(&small), -EINVAL);
    points[0] = cfg.points[0];
    struct dp_transmit_cfg maps[2] = {cfg.transmit[0], cfg.transmit[0]};
    small.transmit = maps;
    small.transmit_count = 2;
    zassert_equal(dp_validate(&small), -EEXIST);
    small.transmit_count = 1;
    maps[0].key.sig_id = 9999;
    zassert_equal(dp_validate(&small), -EINVAL);
    maps[0] = (struct dp_transmit_cfg){
        .function = 4, .address = 65535, .count = 2, .kind = DP_MAP_ZERO,
    };
    zassert_equal(dp_validate(&small), -EINVAL);
    struct co_cfg collect = cfg.collect[0];
    cfg.collect = &collect;
    cfg.collect_count = 1;
    collect.count = CO_MAX_REGS + 1;
    zassert_equal(dp_validate(&cfg), -EINVAL);
    collect.count = 1;
    collect.dev_id = 999;
    zassert_equal(dp_validate(&cfg), -EINVAL);
    zassert_equal(dp_init_config(idu_equipment_config()), -EALREADY);
}

ZTEST_SUITE(points, NULL, setup, before, NULL, NULL);

static K_SEM_DEFINE(config_done,0,1);
static int config_error;
static bool fail_config_init;
int __real_modbus_init_server(int iface, struct modbus_iface_param param);
int __wrap_modbus_init_server(int iface, struct modbus_iface_param param)
{
    if (fail_config_init && param.server.unit_id==19) {
        fail_config_init=false; return -EIO;
    }
    return __real_modbus_init_server(iface,param);
}
static void reconfigured(int iface, int error, void *user)
{
    ARG_UNUSED(user); zassert_equal(iface,north_server);
    config_error=error; k_sem_give(&config_done);
}
static int configure_segment(int iface, const struct modbus_adu *rx, struct modbus_adu *tx, void *user)
{
    ARG_UNUSED(user);
    struct modbus_iface_param param={.mode=MODBUS_MODE_RAW,
        .server={.unit_id=sys_get_be16(rx->data+2),.user_cb=idu_parameter_callbacks()},
        .rawcb={.raw_tx_cb=north_response}};
    int err=tr_modbus_reconfigure(iface,param,reconfigured,NULL);
    if (!err) { *tx=*rx; }
    return err;
}
ZTEST(points,test_modules_dynamic_port_configuration_and_routes)
{
    static const struct tr_segment segments[]={
        {.begin=52000,.end=52000,.function=6,.handler=configure_segment},
        {.begin=50000,.end=50003,.target_dev=20,.target_begin=40000},
    };
    static const struct tr_routes routes={.segments=segments,.count=ARRAY_SIZE(segments)};
    zassert_ok(tr_attach(north_server,&routes));
    uint16_t word;
    /* A backend initialization failure must restore the last working config,
     * including when a second failed change follows the first one. */
    struct modbus_iface_param invalid={.mode=MODBUS_MODE_RAW,
        .server={.unit_id=19,.user_cb=idu_parameter_callbacks()},
        .rawcb={.raw_tx_cb=north_response}};
    for (int i=0;i<2;++i) {
        fail_config_init=true;
        zassert_ok(tr_modbus_reconfigure(north_server,invalid,reconfigured,NULL));
        zassert_ok(k_sem_take(&config_done,K_SECONDS(1)));
        zassert_true(config_error<0 && config_error!=-ENOTRECOVERABLE);
        zassert_ok(modbus_read_holding_regs(north_client,16,31000,&word,1));
    }
    zassert_ok(modbus_read_holding_regs(north_client,16,50000,&word,1)); zassert_equal(word,100);
    zassert_ok(modbus_write_holding_reg(north_client,16,50000,123));
    zassert_ok(modbus_read_holding_regs(north_client,16,50000,&word,1)); zassert_equal(word,123);
    zassert_equal(modbus_write_holding_reg(north_client,16,52000,0),MODBUS_EXC_ILLEGAL_DATA_VAL);
    zassert_ok(modbus_write_holding_reg(north_client,16,52000,17));
    zassert_equal(north_reply.unit_id,16); zassert_equal(north_reply.fc,6);
    zassert_ok(k_sem_take(&config_done,K_SECONDS(1))); zassert_ok(config_error);
    zassert_ok(modbus_read_holding_regs(north_client,17,31000,&word,1));
    zassert_ok(modbus_write_holding_reg(north_client,17,52000,16));
    zassert_ok(k_sem_take(&config_done,K_SECONDS(1))); zassert_ok(config_error);
    zassert_ok(modbus_read_holding_regs(north_client,16,31000,&word,1));
    zassert_ok(tr_attach(north_server,NULL));
}
