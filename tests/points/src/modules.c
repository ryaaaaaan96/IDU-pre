#include "idu_equip.h"
#include <idu/process.h>
#include <idu/collect.h>
#include <idu/forward.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>
#include <string.h>

static struct idu_device_cfg devices[2];
static struct idu_collect_cfg collect_blocks[CONFIG_IDU_POINTS_MAX_POLLS];
static uint16_t bus_value = 100;
static int bus_fault;
static unsigned callback_count, timer_count, reboot_count;
static bool slow_handler;
extern int north_client;
static uint16_t callback_port;
static struct idu_key input = {1, 11200}, output = {1, 11201}, dynamic = {1, 11202};
static struct idu_key remote2 = {20, 1200};

void idu_process_reboot(void) { ++reboot_count; }
unsigned module_reboot_count(void) { return reboot_count; }

static int linked(struct idu_process_ctx *ctx, struct idu_key key)
{
    struct idu_value candidate, old;
    ++callback_count; callback_port = ctx->request->port_id;
    if (ctx->request->operation == IDU_OP_INIT) {
        candidate = (struct idu_value){.words = {7}};
        return idu_txn_emit(ctx, output, &candidate, IDU_OP_NO_COLLECT | IDU_OP_NO_STORE);
    }
    if (ctx->request->operation == IDU_OP_TIMER) {
        ++timer_count;
        candidate = (struct idu_value){.words = {timer_count}};
        return idu_txn_emit(ctx, output, &candidate, IDU_OP_NO_COLLECT | IDU_OP_NO_STORE);
    }
    if (ctx->request->operation == IDU_OP_READ) {
        candidate = (struct idu_value){.words = {1234}};
        return idu_txn_emit(ctx, key, &candidate, IDU_OP_NO_COLLECT | IDU_OP_NO_STORE);
    }
    if (slow_handler) { k_sleep(K_MSEC(70)); }
    zassert_true(idu_txn_contains(ctx, input));
    zassert_false(idu_txn_contains(ctx, output));
    zassert_ok(idu_txn_get(ctx, input, false, &candidate));
    zassert_ok(idu_txn_get(ctx, input, true, &old));
    candidate.words[0] *= 2;
    int err = idu_txn_emit(ctx, output, &candidate, IDU_OP_NO_COLLECT | IDU_OP_NO_STORE);
    candidate.words[0] = candidate.words[0] / 2 + 10;
    return err ? err : idu_txn_emit(ctx, (struct idu_key){1, 31100}, &candidate,
                                   IDU_OP_NO_COLLECT | IDU_OP_NO_STORE);
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

void module_add_points(struct idu_equip_cfg *cfg, struct idu_point_cfg *points,
                        struct idu_transmit_cfg *maps)
{
    memcpy(collect_blocks,cfg->collect,cfg->collect_count*sizeof(*collect_blocks));
    collect_blocks[0].init_only=true;
    cfg->collect=collect_blocks;
    devices[0] = cfg->devices[0];
    devices[1] = (struct idu_device_cfg){.dev_id = 20, .unit_id = 1, .bus_id = 1};
    cfg->devices = devices; cfg->device_count = 2;
    points[cfg->point_count++] = (struct idu_point_cfg){.key = input,
        .source = IDU_SOURCE_LOCAL, .type = IDU_U16, .width = 1, .maximum = 1000, .handler = linked};
    points[cfg->point_count++] = (struct idu_point_cfg){.key = output,
        .source = IDU_SOURCE_LOCAL, .type = IDU_U16, .width = 1, .maximum = 2000};
    points[cfg->point_count++] = (struct idu_point_cfg){.key = dynamic,
        .source = IDU_SOURCE_LOCAL, .type = IDU_U16, .width = 1, .maximum = 2000,
        .handler = linked, .read_callback = true};
    points[cfg->point_count++] = (struct idu_point_cfg){.key = remote2,
        .source = IDU_SOURCE_PARAMETER, .type = IDU_U16, .width = 1, .maximum = 1000,
        .param_flags = IDU_PARAM_WRITE | IDU_PARAM_PERSIST | IDU_PARAM_FORWARD | IDU_PARAM_RESTORE,
        .initial.words = {100}, .write_address = 40000};
    for (unsigned order=0;order<4;++order) {
        maps[cfg->transmit_count++] = (struct idu_transmit_cfg){.function=4,.address=11300+2*order,
            .count=2,.key={1,11001},.order=order};
        maps[cfg->transmit_count++] = (struct idu_transmit_cfg){.function=3,.address=31600+2*order,
            .count=2,.key={IDU_DEV_REMOTE_IO,IDU_SIG_REMOTE_OFFSET},.order=order};
    }
    maps[cfg->transmit_count++] = (struct idu_transmit_cfg){.function = 4, .address = 11202,
        .count = 1, .key = dynamic};
}

void module_bus_init(const struct idu_equip_cfg *cfg)
{
    struct idu_bus_cfg bus = {.id = 1, .iface = -1, .simulated = false, .transfer = second_bus};
    zassert_ok(idu_collect_init_bus(cfg, &bus));
    for (size_t i = 0; i < cfg->collect_count; ++i) { zassert_ok(idu_collect_enable_poll(i, false)); }
    zassert_ok(idu_collect_start());
}

static uint16_t value(struct idu_key key)
{
    struct idu_point_state state; zassert_ok(idu_points_read(key, &state)); return state.value.words[0];
}

ZTEST(points, test_modules_process_linked_outputs_and_read_init)
{
    callback_count = 0;
    struct idu_process_request req = {.operation = IDU_OP_WRITE, .id = 123, .port_id = 9, .count = 1,
        .items[0].point = {.key = input, .width = 1, .value.words = {20}}};
    struct idu_process_result result;
    zassert_ok(idu_process_execute(&req, &result));
    zassert_equal(result.id, 123); zassert_equal(result.count, 3);
    zassert_equal(callback_count, 1); zassert_equal(callback_port, 9);
    zassert_equal(value(output), 40); zassert_equal(value((struct idu_key){1,31100}), 30);
    zassert_ok(idu_params_flush()); zassert_ok(idu_params_reload());
    zassert_equal(value((struct idu_key){1,31100}), 10); /* linked NO_STORE never leaked */
    struct idu_point_state state;
    zassert_ok(idu_process_read(dynamic, 12, &state));
    zassert_equal(state.value.words[0], 1234); zassert_equal(callback_port, 12);
    zassert_ok(idu_process_init_device(1)); zassert_equal(value(output), 7);
    req.items[0].point.value.words[0] = 2000;
    zassert_equal(idu_process_execute(&req, &result), -ERANGE);
    zassert_equal(value(output), 7); /* no output committed after failed validation */
}

static K_SEM_DEFINE(done_sem, 0, 8);
static struct idu_process_result completion;
static void completed(const struct idu_process_result *result, void *user)
{
    zassert_equal(user, &completion); completion = *result; k_sem_give(&done_sem);
}

ZTEST(points, test_modules_async_timer_and_save_reboot)
{
    struct idu_process_request req = {.operation = IDU_OP_WRITE, .id = 321, .count = 1,
        .items[0].point = {.key = input, .width = 1, .value.words = {8}}};
    zassert_ok(idu_process_submit(&req, completed, &completion));
    req.items[0].point.value.words[0] = 999; /* queue owns a value copy */
    zassert_ok(k_sem_take(&done_sem, K_SECONDS(1)));
    zassert_ok(completion.error); zassert_equal(completion.id, 321); zassert_equal(value(output), 16);
    timer_count = 0;
    int slot = idu_process_timer_start(input, 5, 0); zassert_true(slot >= 0);
    k_sleep(K_MSEC(50)); zassert_equal(timer_count, 1);
    k_sleep(K_MSEC(30)); zassert_equal(timer_count, 1);
    slot = idu_process_timer_start(input, 5, 10); zassert_true(slot >= 0);
    k_sleep(K_MSEC(60)); zassert_true(timer_count >= 3);
    zassert_ok(idu_process_timer_cancel(slot));
    unsigned before = timer_count; k_sleep(K_MSEC(40)); zassert_equal(timer_count, before);
    before = reboot_count; zassert_ok(idu_process_save_reboot(100));
    k_sleep(K_MSEC(150)); zassert_equal(reboot_count, before+1);
}

ZTEST(points, test_modules_parameter_operation_flags_and_deferred_store)
{
    struct idu_point_update update = {.key = {1,31100}, .width = 1, .value.words = {55}};
    zassert_ok(idu_params_apply(&update, 1, IDU_OP_NO_STORE));
    zassert_equal(value(update.key), 55);
    struct idu_point_update another = {.key = {1, IDU_SIG_SAMPLE_PERIOD}, .width = 1, .value.words = {600}};
    zassert_ok(idu_params_set_values(&another, 1));
    zassert_ok(idu_params_reload()); zassert_equal(value(update.key), 10);
    update.value.words[0] = 66;
    zassert_ok(idu_params_apply(&update, 1, IDU_OP_DEFER));
    zassert_equal(value(update.key), 66);
    zassert_ok(idu_params_reload()); zassert_equal(value(update.key), 10);
    zassert_ok(idu_params_apply(&update, 1, IDU_OP_DEFER));
    k_sleep(K_MSEC(1100)); zassert_ok(idu_params_reload()); zassert_equal(value(update.key), 66);
    update = (struct idu_point_update){.key = remote2, .width = 1, .value.words = {222}};
    unsigned before = bus_value;
    zassert_ok(idu_params_apply(&update, 1, IDU_OP_NO_COLLECT | IDU_OP_NO_STORE));
    zassert_equal(bus_value, before); zassert_equal(value(remote2), 222);
    update.value.words[0] = 333;
    zassert_ok(idu_params_apply(&update, 1, IDU_OP_NO_DM));
    zassert_equal(bus_value, 333); zassert_equal(value(remote2), 222);
    zassert_ok(idu_params_reload()); zassert_equal(value(remote2), 100);
}

ZTEST(points, test_modules_multibus_modes_state_and_initialization)
{
    struct idu_point_update update = {.key = remote2, .width = 1, .value.words = {222}};
    zassert_ok(idu_params_set_values(&update, 1)); zassert_equal(bus_value, 222);
    zassert_ok(idu_collect_set_mode(1, IDU_PORT_SLEEP));
    uint16_t word; zassert_equal(idu_collect_read(20, 3, 40000, &word, 1), -EBUSY);
    zassert_ok(idu_collect_read(IDU_DEV_REMOTE_IO, 4, 8152, &word, 1)); /* other bus unaffected */
    zassert_ok(idu_collect_set_mode(1, IDU_PORT_POLL));
    bus_fault = -ETIMEDOUT;
    zassert_equal(idu_collect_read(20, 3, 40000, &word, 1), -ETIMEDOUT);
    struct idu_device_status status; zassert_ok(idu_collect_device_status(20, &status));
    zassert_equal(status.state, IDU_DEVICE_OFFLINE);
    bus_fault = 2; zassert_equal(idu_collect_read(20, 3, 40000, &word, 1), 2);
    zassert_ok(idu_collect_device_status(20, &status)); zassert_equal(status.state, IDU_DEVICE_EXCEPTION);
    bus_fault = 0; bus_value = 44;
    zassert_ok(idu_collect_initialize_device(20)); zassert_equal(bus_value, 222);
    zassert_ok(idu_virtual_set_register(4,8152,0x27));
    zassert_ok(idu_collect_initialize_device(IDU_DEV_REMOTE_IO));
    zassert_equal(value((struct idu_key){IDU_DEV_REMOTE_IO,IDU_SIG_DI}),0x27);
    zassert_ok(idu_collect_device_status(20, &status)); zassert_equal(status.state, IDU_DEVICE_ONLINE);
}

static uint32_t order[4]; static unsigned completed_count;
static K_SEM_DEFINE(bus_done, 0, 8);
static void bus_completed(const struct idu_collect_request *reply, int error, void *user)
{
    ARG_UNUSED(user); zassert_ok(error); order[completed_count++] = reply->id; k_sem_give(&bus_done);
}
ZTEST(points, test_modules_priority_and_segment_passthrough)
{
    zassert_ok(idu_collect_set_mode(1, IDU_PORT_SLEEP)); completed_count = 0;
    struct idu_collect_request request = {.dev_id = 20, .id = 1, .priority = 0, .mode=IDU_PORT_PASSTHROUGH,
                                         .adu = {.fc = 3, .length = 4}};
    sys_put_be16(40000, request.adu.data); sys_put_be16(1, request.adu.data+2);
    zassert_ok(idu_collect_submit(&request, bus_completed, NULL));
    request.id = 2; request.priority = 3; zassert_ok(idu_collect_submit(&request, bus_completed, NULL));
    zassert_ok(idu_collect_set_mode(1, IDU_PORT_PASSTHROUGH));
    zassert_ok(k_sem_take(&bus_done, K_SECONDS(1))); zassert_ok(k_sem_take(&bus_done, K_SECONDS(1)));
    zassert_equal(order[0], 2); zassert_equal(order[1], 1);
    struct idu_forward_segment segment = {.begin=50000,.end=50003,.target_dev=20,.target_begin=40000};
    struct idu_forward_routes routes = {.segments=&segment,.count=1};
    struct modbus_adu rx = {.unit_id=16,.fc=3,.length=4}, tx;
    sys_put_be16(50000,rx.data); sys_put_be16(1,rx.data+2);
    zassert_ok(idu_forward_dispatch(2,&rx,&tx,&routes));
    zassert_equal(tx.unit_id,16); zassert_equal(sys_get_be16(tx.data+1),bus_value);
    sys_put_be16(50003,rx.data); sys_put_be16(2,rx.data+2);
    zassert_equal(idu_forward_dispatch(2,&rx,&tx,&routes),-ERANGE);
    zassert_ok(idu_collect_set_mode(1, IDU_PORT_POLL));
}

ZTEST(points, test_modules_all_four_byte_orders)
{
    const uint16_t words[] = {0x3344,0x1122};
    const uint16_t expected[][2] = {{0x3344,0x1122},{0x1122,0x3344},{0x4433,0x2211},{0x2211,0x4433}};
    for (int order = 0; order < 4; ++order) {
        uint16_t wire[2];
        for (int i=0;i<2;++i) { wire[i]=idu_order_word(words,2,i,order); zassert_equal(wire[i],expected[order][i]); }
        for (int i=0;i<2;++i) { zassert_equal(idu_order_word(wire,2,i,order),words[i]); }
    }
}

ZTEST(points, test_modules_sample_callbacks_keep_provenance)
{
    struct idu_point_batch batch={.source=IDU_SOURCE_LOCAL,.flags=IDU_QUALITY_SIMULATED,.count=1,
        .points[0]={.key=input,.width=1,.value.words={21}}};
    zassert_ok(idu_points_publish(&batch,K_MSEC(20)));
    k_sleep(K_MSEC(30));
    struct idu_point_state state; zassert_ok(idu_points_read(output,&state));
    zassert_equal(state.value.words[0],42);
    zassert_equal(state.quality,IDU_QUALITY_SIMULATED|IDU_QUALITY_GOOD);
    batch.points[0].value.words[0]=2000;
    zassert_ok(idu_points_publish(&batch,K_MSEC(20))); k_sleep(K_MSEC(30));
    zassert_equal(value(output),42);
    zassert_ok(idu_points_read(input,&state));
    zassert_equal(state.quality,IDU_QUALITY_SIMULATED|IDU_QUALITY_BAD_VALUE);
}

ZTEST(points, test_modules_busy_timer_retries_and_partial_results)
{
    timer_count=0; slow_handler=true;
    int timer=idu_process_timer_start(input,5,0); zassert_true(timer>=0);
    struct idu_process_request req={.operation=IDU_OP_WRITE,.count=1,
        .items[0].point={.key=input,.width=1,.value.words={10}}};
    struct idu_process_result result;
    zassert_ok(idu_process_execute(&req,&result)); slow_handler=false;
    k_sleep(K_MSEC(50)); zassert_equal(timer_count,1);
    req.count=2;
    req.items[0].point=(struct idu_point_update){.key={1,31100},.width=1,.value.words={33}};
    req.items[1].point=(struct idu_point_update){.key=remote2,.width=1,.value.words={333}};
    bus_fault=-ETIMEDOUT;
    zassert_equal(idu_process_execute(&req,&result),-ETIMEDOUT); bus_fault=0;
    zassert_equal(result.count,2); zassert_ok(result.points[0].error);
    zassert_equal(result.points[1].error,-ETIMEDOUT);
    zassert_equal(value((struct idu_key){1,31100}),33); zassert_equal(value(remote2),100);
}

ZTEST(points, test_modules_four_orders_over_modbus)
{
    struct idu_point_update update={.key={1,11001},.width=2,.value.words={0x3344,0x1122}};
    zassert_ok(idu_points_commit(&update,1));
    const uint16_t expected[][2]={{0x3344,0x1122},{0x1122,0x3344},{0x4433,0x2211},{0x2211,0x4433}};
    for (int order=0;order<4;++order) {
        uint16_t wire[2];
        zassert_ok(modbus_read_input_regs(north_client,16,11300+2*order,wire,2));
        zassert_mem_equal(wire,expected[order],sizeof(wire));
        uint16_t canonical[2]={0x1234,0};
        for (int i=0;i<2;++i) { wire[i]=idu_order_word(canonical,2,i,order); }
        zassert_ok(modbus_write_holding_regs(north_client,16,31600+2*order,wire,2));
        zassert_equal(value((struct idu_key){IDU_DEV_REMOTE_IO,IDU_SIG_REMOTE_OFFSET}),0x1234);
    }
}

static unsigned cancelled;
static void on_cancel(const struct idu_collect_request *request, int error, void *user)
{
    ARG_UNUSED(request); ARG_UNUSED(user); zassert_equal(error,-ECANCELED); ++cancelled;
}
ZTEST(points, test_modules_mode_ownership_and_cancel)
{
    zassert_ok(idu_collect_set_mode(1,IDU_PORT_UPDATE));
    uint16_t word; zassert_equal(idu_collect_read(20,3,40000,&word,1),-EBUSY);
    struct idu_collect_request req={.dev_id=20,.adu={.fc=3,.length=4}};
    sys_put_be16(40000,req.adu.data); sys_put_be16(1,req.adu.data+2);
    cancelled=0;
    zassert_ok(idu_collect_submit(&req,on_cancel,NULL));
    k_sleep(K_MSEC(20)); zassert_equal(cancelled,0);
    zassert_ok(idu_collect_cancel(1)); zassert_equal(cancelled,1);
    zassert_ok(idu_collect_cancel(1)); zassert_equal(cancelled,1);
    zassert_ok(idu_collect_transfer_mode(20,IDU_PORT_UPDATE,&req.adu));
    zassert_ok(idu_collect_set_mode(1,IDU_PORT_POLL));
}
