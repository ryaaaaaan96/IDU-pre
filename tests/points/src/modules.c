#include "idu_equip.h"
#include <dataprocess/process.h>
#include <collect/collect.h>
#include <transmit/transmit.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>
#include <string.h>

static struct dp_device_cfg devices[2];
static struct co_cfg collect_blocks[CONFIG_DP_POINTS_MAX_POLLS];
static uint16_t bus_value = 100;
static int bus_fault;
static unsigned callback_count, timer_count, reboot_count;
static bool slow_handler;
extern int north_client;
static uint16_t callback_port;
static struct dp_key input = {1, 11200}, output = {1, 11201}, dynamic = {1, 11202};
static struct dp_key remote2 = {20, 1200};

void dp_process_reboot(void) { ++reboot_count; }
unsigned module_reboot_count(void) { return reboot_count; }

static int linked(struct dp_process_ctx *ctx, struct dp_key key)
{
    struct dp_value candidate, old;
    ++callback_count; callback_port = ctx->request->port_id;
    if (ctx->request->operation == DP_OP_INIT) {
        candidate = (struct dp_value){.words = {7}};
        return dp_txn_emit(ctx, output, &candidate, DP_OP_NO_COLLECT | DP_OP_NO_STORE);
    }
    if (ctx->request->operation == DP_OP_TIMER) {
        ++timer_count;
        candidate = (struct dp_value){.words = {timer_count}};
        return dp_txn_emit(ctx, output, &candidate, DP_OP_NO_COLLECT | DP_OP_NO_STORE);
    }
    if (ctx->request->operation == DP_OP_READ) {
        candidate = (struct dp_value){.words = {1234}};
        return dp_txn_emit(ctx, key, &candidate, DP_OP_NO_COLLECT | DP_OP_NO_STORE);
    }
    if (slow_handler) { k_sleep(K_MSEC(70)); }
    zassert_true(dp_txn_contains(ctx, input));
    zassert_false(dp_txn_contains(ctx, output));
    zassert_ok(dp_txn_get(ctx, input, false, &candidate));
    zassert_ok(dp_txn_get(ctx, input, true, &old));
    candidate.words[0] *= 2;
    int err = dp_txn_emit(ctx, output, &candidate, DP_OP_NO_COLLECT | DP_OP_NO_STORE);
    candidate.words[0] = candidate.words[0] / 2 + 10;
    return err ? err : dp_txn_emit(ctx, (struct dp_key){1, 31100}, &candidate,
                                   DP_OP_NO_COLLECT | DP_OP_NO_STORE);
}

static int second_bus(void *user, struct modbus_adu *adu)
{
    ARG_UNUSED(user);
    if (bus_fault) { return bus_fault; }
    if (adu->fc == 3 && adu->length == 4 && sys_get_be16(adu->data + 2) == 1) {
        adu->data[0] = 2; sys_put_be16(bus_value, adu->data + 1); adu->length = 3; return 0;
    }
    if (adu->fc == 6 && adu->length == 4) {
        bus_value = sys_get_be16(adu->data + 2); return 0;
    }
    return -ENOTSUP;
}

void module_add_points(struct dp_equip_cfg *cfg, struct dp_point_cfg *points,
                        struct dp_transmit_cfg *maps)
{
    memcpy(collect_blocks,cfg->collect,cfg->collect_count*sizeof(*collect_blocks));
    collect_blocks[0].init_only=true;
    cfg->collect=collect_blocks;
    devices[0] = cfg->devices[0];
    devices[1] = (struct dp_device_cfg){.dev_id = 20, .unit_id = 1, .bus_id = 1};
    cfg->devices = devices; cfg->device_count = 2;
    points[cfg->point_count++] = (struct dp_point_cfg){.key = input,
        .source = DP_SOURCE_LOCAL, .type = DP_U16, .width = 1, .maximum = 1000, .handler = linked};
    points[cfg->point_count++] = (struct dp_point_cfg){.key = output,
        .source = DP_SOURCE_LOCAL, .type = DP_U16, .width = 1, .maximum = 2000};
    points[cfg->point_count++] = (struct dp_point_cfg){.key = dynamic,
        .source = DP_SOURCE_LOCAL, .type = DP_U16, .width = 1, .maximum = 2000,
        .handler = linked, .read_callback = true};
    points[cfg->point_count++] = (struct dp_point_cfg){.key = remote2,
        .source = DP_SOURCE_PARAMETER, .type = DP_U16, .width = 1, .maximum = 1000,
        .param_flags = DP_PARAM_WRITE | DP_PARAM_PERSIST | DP_PARAM_FORWARD | DP_PARAM_RESTORE,
        .initial.words = {100}, .write_address = 40000};
    for (unsigned order=0;order<4;++order) {
        maps[cfg->transmit_count++] = (struct dp_transmit_cfg){.function=4,.address=11300+2*order,
            .count=2,.key={1,11001},.order=order};
        maps[cfg->transmit_count++] = (struct dp_transmit_cfg){.function=3,.address=31600+2*order,
            .count=2,.key={IDU_DEV_REMOTE_IO,IDU_SIG_REMOTE_OFFSET},.order=order};
    }
    maps[cfg->transmit_count++] = (struct dp_transmit_cfg){.function = 4, .address = 11202,
        .count = 1, .key = dynamic};
}

void module_bus_init(const struct dp_equip_cfg *cfg)
{
    struct co_bus_cfg bus = {.id = 1, .iface = -1, .simulated = false, .transfer = second_bus};
    zassert_ok(co_init_bus(cfg, &bus));
    for (size_t i = 0; i < cfg->collect_count; ++i) { zassert_ok(co_enable_poll(i, false)); }
    zassert_ok(co_start());
}

static uint16_t value(struct dp_key key)
{
    struct dp_point_state state; zassert_ok(dp_read(key, &state)); return state.value.words[0];
}

ZTEST(points, test_modules_process_linked_outputs_and_read_init)
{
    callback_count = 0;
    struct dp_process_request req = {.operation = DP_OP_WRITE, .id = 123, .port_id = 9, .count = 1,
        .items[0].point = {.key = input, .width = 1, .value.words = {20}}};
    struct dp_process_result result;
    zassert_ok(dp_process_execute(&req, &result));
    zassert_equal(result.id, 123); zassert_equal(result.count, 3);
    zassert_equal(callback_count, 1); zassert_equal(callback_port, 9);
    zassert_equal(value(output), 40); zassert_equal(value((struct dp_key){1,31100}), 30);
    zassert_ok(dp_params_flush()); zassert_ok(dp_params_reload());
    zassert_equal(value((struct dp_key){1,31100}), 10); /* linked NO_STORE never leaked */
    struct dp_point_state state;
    zassert_ok(dp_process_read(dynamic, 12, &state));
    zassert_equal(state.value.words[0], 1234); zassert_equal(callback_port, 12);
    zassert_ok(dp_process_init_device(1)); zassert_equal(value(output), 7);
    req.items[0].point.value.words[0] = 2000;
    zassert_equal(dp_process_execute(&req, &result), -ERANGE);
    zassert_equal(value(output), 7); /* no output committed after failed validation */
}

static K_SEM_DEFINE(done_sem, 0, 8);
static struct dp_process_result completion;
static void completed(const struct dp_process_result *result, void *user)
{
    zassert_equal(user, &completion); completion = *result; k_sem_give(&done_sem);
}

ZTEST(points, test_modules_async_timer_and_save_reboot)
{
    struct dp_process_request req = {.operation = DP_OP_WRITE, .id = 321, .count = 1,
        .items[0].point = {.key = input, .width = 1, .value.words = {8}}};
    zassert_ok(dp_process_submit(&req, completed, &completion));
    req.items[0].point.value.words[0] = 999; /* queue owns a value copy */
    zassert_ok(k_sem_take(&done_sem, K_SECONDS(1)));
    zassert_ok(completion.error); zassert_equal(completion.id, 321); zassert_equal(value(output), 16);
    timer_count = 0;
    int slot = dp_process_timer_start(input, 5, 0); zassert_true(slot >= 0);
    k_sleep(K_MSEC(50)); zassert_equal(timer_count, 1);
    k_sleep(K_MSEC(30)); zassert_equal(timer_count, 1);
    slot = dp_process_timer_start(input, 5, 10); zassert_true(slot >= 0);
    k_sleep(K_MSEC(60)); zassert_true(timer_count >= 3);
    zassert_ok(dp_process_timer_cancel(slot));
    unsigned before = timer_count; k_sleep(K_MSEC(40)); zassert_equal(timer_count, before);
    before = reboot_count; zassert_ok(dp_process_save_reboot(100));
    k_sleep(K_MSEC(150)); zassert_equal(reboot_count, before+1);
}

ZTEST(points, test_modules_parameter_operation_flags_and_deferred_store)
{
    struct dp_point_update update = {.key = {1,31100}, .width = 1, .value.words = {55}};
    zassert_ok(dp_params_apply(&update, 1, DP_OP_NO_STORE));
    zassert_equal(value(update.key), 55);
    struct dp_point_update another = {.key = {1, IDU_SIG_SAMPLE_PERIOD}, .width = 1, .value.words = {600}};
    zassert_ok(dp_params_set_values(&another, 1));
    zassert_ok(dp_params_reload()); zassert_equal(value(update.key), 10);
    update.value.words[0] = 66;
    zassert_ok(dp_params_apply(&update, 1, DP_OP_DEFER));
    zassert_equal(value(update.key), 66);
    zassert_ok(dp_params_reload()); zassert_equal(value(update.key), 10);
    zassert_ok(dp_params_apply(&update, 1, DP_OP_DEFER));
    k_sleep(K_MSEC(1100)); zassert_ok(dp_params_reload()); zassert_equal(value(update.key), 66);
    update = (struct dp_point_update){.key = remote2, .width = 1, .value.words = {222}};
    unsigned before = bus_value;
    zassert_ok(dp_params_apply(&update, 1, DP_OP_NO_COLLECT | DP_OP_NO_STORE));
    zassert_equal(bus_value, before); zassert_equal(value(remote2), 222);
    update.value.words[0] = 333;
    zassert_ok(dp_params_apply(&update, 1, DP_OP_NO_DM));
    zassert_equal(bus_value, 333); zassert_equal(value(remote2), 222);
    zassert_ok(dp_params_reload()); zassert_equal(value(remote2), 100);
}

ZTEST(points, test_modules_multibus_modes_state_and_initialization)
{
    struct dp_point_update update = {.key = remote2, .width = 1, .value.words = {222}};
    zassert_ok(dp_params_set_values(&update, 1)); zassert_equal(bus_value, 222);
    zassert_ok(co_set_mode(1, CO_PORT_SLEEP));
    uint16_t word; zassert_equal(co_read(20, 3, 40000, &word, 1), -EBUSY);
    zassert_ok(co_read(IDU_DEV_REMOTE_IO, 4, 8152, &word, 1)); /* other bus unaffected */
    zassert_ok(co_set_mode(1, CO_PORT_POLL));
    bus_fault = -ETIMEDOUT;
    zassert_equal(co_read(20, 3, 40000, &word, 1), -ETIMEDOUT);
    struct co_device_status status; zassert_ok(co_device_status(20, &status));
    zassert_equal(status.state, CO_DEVICE_OFFLINE);
    bus_fault = 2; zassert_equal(co_read(20, 3, 40000, &word, 1), 2);
    zassert_ok(co_device_status(20, &status)); zassert_equal(status.state, CO_DEVICE_EXCEPTION);
    bus_fault = 0; bus_value = 44;
    zassert_ok(co_initialize_device(20)); zassert_equal(bus_value, 222);
    zassert_ok(idu_virtual_set_register(4,8152,0x27));
    zassert_ok(co_initialize_device(IDU_DEV_REMOTE_IO));
    zassert_equal(value((struct dp_key){IDU_DEV_REMOTE_IO,IDU_SIG_DI}),0x27);
    zassert_ok(co_device_status(20, &status)); zassert_equal(status.state, CO_DEVICE_ONLINE);
}

static uint32_t order[4]; static unsigned completed_count;
static K_SEM_DEFINE(bus_done, 0, 8);
static void bus_completed(const struct co_request *reply, int error, void *user)
{
    ARG_UNUSED(user); zassert_ok(error); order[completed_count++] = reply->id; k_sem_give(&bus_done);
}
ZTEST(points, test_modules_priority_and_segment_passthrough)
{
    zassert_ok(co_set_mode(1, CO_PORT_SLEEP)); completed_count = 0;
    struct co_request request = {.dev_id = 20, .id = 1, .priority = 0, .mode=CO_PORT_PASSTHROUGH,
                                         .adu = {.fc = 3, .length = 4}};
    sys_put_be16(40000, request.adu.data); sys_put_be16(1, request.adu.data+2);
    zassert_ok(co_submit(&request, bus_completed, NULL));
    request.id = 2; request.priority = 3; zassert_ok(co_submit(&request, bus_completed, NULL));
    zassert_ok(co_set_mode(1, CO_PORT_PASSTHROUGH));
    zassert_ok(k_sem_take(&bus_done, K_SECONDS(1))); zassert_ok(k_sem_take(&bus_done, K_SECONDS(1)));
    zassert_equal(order[0], 2); zassert_equal(order[1], 1);
    struct tr_segment segment = {.begin=50000,.end=50003,.target_dev=20,.target_begin=40000};
    struct tr_routes routes = {.segments=&segment,.count=1};
    struct modbus_adu rx = {.unit_id=16,.fc=3,.length=4}, tx;
    sys_put_be16(50000,rx.data); sys_put_be16(1,rx.data+2);
    zassert_ok(tr_dispatch(2,&rx,&tx,&routes));
    zassert_equal(tx.unit_id,16); zassert_equal(sys_get_be16(tx.data+1),bus_value);
    sys_put_be16(50003,rx.data); sys_put_be16(2,rx.data+2);
    zassert_equal(tr_dispatch(2,&rx,&tx,&routes),-ERANGE);
    zassert_ok(co_set_mode(1, CO_PORT_POLL));
}

ZTEST(points, test_modules_all_four_byte_orders)
{
    const uint16_t words[] = {0x3344,0x1122};
    const uint16_t expected[][2] = {{0x3344,0x1122},{0x1122,0x3344},{0x4433,0x2211},{0x2211,0x4433}};
    for (int order = 0; order < 4; ++order) {
        uint16_t wire[2];
        for (int i=0;i<2;++i) { wire[i]=dp_order_word(words,2,i,order); zassert_equal(wire[i],expected[order][i]); }
        for (int i=0;i<2;++i) { zassert_equal(dp_order_word(wire,2,i,order),words[i]); }
    }
}

ZTEST(points, test_modules_sample_callbacks_keep_provenance)
{
    struct dp_point_batch batch={.source=DP_SOURCE_LOCAL,.flags=DP_QUALITY_SIMULATED,.count=1,
        .points[0]={.key=input,.width=1,.value.words={21}}};
    zassert_ok(dp_publish(&batch,K_MSEC(20)));
    k_sleep(K_MSEC(30));
    struct dp_point_state state; zassert_ok(dp_read(output,&state));
    zassert_equal(state.value.words[0],42);
    zassert_equal(state.quality,DP_QUALITY_SIMULATED|DP_QUALITY_GOOD);
    batch.points[0].value.words[0]=2000;
    zassert_ok(dp_publish(&batch,K_MSEC(20))); k_sleep(K_MSEC(30));
    zassert_equal(value(output),42);
    zassert_ok(dp_read(input,&state));
    zassert_equal(state.quality,DP_QUALITY_SIMULATED|DP_QUALITY_BAD_VALUE);
}

ZTEST(points, test_modules_busy_timer_retries_and_partial_results)
{
    timer_count=0; slow_handler=true;
    int timer=dp_process_timer_start(input,5,0); zassert_true(timer>=0);
    struct dp_process_request req={.operation=DP_OP_WRITE,.count=1,
        .items[0].point={.key=input,.width=1,.value.words={10}}};
    struct dp_process_result result;
    zassert_ok(dp_process_execute(&req,&result)); slow_handler=false;
    k_sleep(K_MSEC(50)); zassert_equal(timer_count,1);
    req.count=2;
    req.items[0].point=(struct dp_point_update){.key={1,31100},.width=1,.value.words={33}};
    req.items[1].point=(struct dp_point_update){.key=remote2,.width=1,.value.words={333}};
    bus_fault=-ETIMEDOUT;
    zassert_equal(dp_process_execute(&req,&result),-ETIMEDOUT); bus_fault=0;
    zassert_equal(result.count,2); zassert_ok(result.points[0].error);
    zassert_equal(result.points[1].error,-ETIMEDOUT);
    zassert_equal(value((struct dp_key){1,31100}),33); zassert_equal(value(remote2),100);
}

ZTEST(points, test_modules_four_orders_over_modbus)
{
    struct dp_point_update update={.key={1,11001},.width=2,.value.words={0x3344,0x1122}};
    zassert_ok(dp_commit(&update,1));
    const uint16_t expected[][2]={{0x3344,0x1122},{0x1122,0x3344},{0x4433,0x2211},{0x2211,0x4433}};
    for (int order=0;order<4;++order) {
        uint16_t wire[2];
        zassert_ok(modbus_read_input_regs(north_client,16,11300+2*order,wire,2));
        zassert_mem_equal(wire,expected[order],sizeof(wire));
        uint16_t canonical[2]={0x1234,0};
        for (int i=0;i<2;++i) { wire[i]=dp_order_word(canonical,2,i,order); }
        zassert_ok(modbus_write_holding_regs(north_client,16,31600+2*order,wire,2));
        zassert_equal(value((struct dp_key){IDU_DEV_REMOTE_IO,IDU_SIG_REMOTE_OFFSET}),0x1234);
    }
}

static unsigned cancelled;
static void on_cancel(const struct co_request *request, int error, void *user)
{
    ARG_UNUSED(request); ARG_UNUSED(user); zassert_equal(error,-ECANCELED); ++cancelled;
}
ZTEST(points, test_modules_mode_ownership_and_cancel)
{
    zassert_ok(co_set_mode(1,CO_PORT_UPDATE));
    uint16_t word; zassert_equal(co_read(20,3,40000,&word,1),-EBUSY);
    struct co_request req={.dev_id=20,.adu={.fc=3,.length=4}};
    sys_put_be16(40000,req.adu.data); sys_put_be16(1,req.adu.data+2);
    cancelled=0;
    zassert_ok(co_submit(&req,on_cancel,NULL));
    k_sleep(K_MSEC(20)); zassert_equal(cancelled,0);
    zassert_ok(co_cancel(1)); zassert_equal(cancelled,1);
    zassert_ok(co_cancel(1)); zassert_equal(cancelled,1);
    zassert_ok(co_transfer_mode(20,CO_PORT_UPDATE,&req.adu));
    zassert_ok(co_set_mode(1,CO_PORT_POLL));
}
