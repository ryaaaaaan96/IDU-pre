#include "../../fixtures/co_tasks.h"
#include "../../fixtures/dp_tasks.h"
#include "../../fixtures/point_connections.h"
#include <collect/collect.h>
#include <collect/port.h>
#include <dataprocess/process.h>
#include <errno.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

struct transfer_event {
    uint16_t address;
    int64_t time;
};
struct completion_event {
    uint32_t id;
    int error;
};
K_MSGQ_DEFINE(transfers, sizeof(struct transfer_event), 32, 4);
K_MSGQ_DEFINE(completions, sizeof(struct completion_event), 16, 4);
K_SEM_DEFINE(init_entered, 0, 1);
K_SEM_DEFINE(init_release, 0, 1);
K_SEM_DEFINE(init_finished, 0, 1);
K_THREAD_STACK_DEFINE(init_stack, 4096);
static struct k_thread init_thread;
static atomic_t hold_init, init_error, init_callbacks;
static int init_result;
static k_tid_t fail_allocation_owner;
void *__real_k_malloc(size_t size);
void *__wrap_k_malloc(size_t size)
{
    if (fail_allocation_owner == k_current_get()) {
        fail_allocation_owner = NULL;
        return NULL;
    }
    return __real_k_malloc(size);
}

static int initialized(struct dp_process_ctx *ctx, struct dp_key key)
{
    ARG_UNUSED(key);
    if (ctx->request->operation == DP_OP_INIT) { atomic_inc(&init_callbacks); }
    return 0;
}

static const struct dp_point_cfg points[] = {
    {.key = {1, 1}, .source = DP_SOURCE_MODBUS, .type = DP_U16,
     .width = 1, .maximum = UINT16_MAX},
    {.key = {1, 2}, .source = DP_SOURCE_MODBUS, .type = DP_U16,
     .width = 1, .maximum = UINT16_MAX, .handler = initialized},
};
static const struct dp_device_cfg devices[] = {
    {.dev_id = 1, .unit_id = 1, .bus_id = 0},
};
static const struct co_field periodic_field[] = {{.sig_id = 1, .width = 1}};
static const struct co_field init_field[] = {{.sig_id = 2, .width = 1}};
static const struct co_cfg polls[] = {
    {.dev_id = 1, .function = 4, .address = 100, .count = 1,
     .period_ms = 80, .fields = periodic_field, .field_count = 1},
    {.dev_id = 1, .function = 4, .address = 300, .count = 1,
     .init_only = true, .fields = init_field, .field_count = 1},
};
static const struct dp_equip_cfg equipment = {
    .points = points, .point_count = ARRAY_SIZE(points),
    .devices = devices, .device_count = ARRAY_SIZE(devices),
    .collect = polls, .collect_count = ARRAY_SIZE(polls),
};

static int transfer(void *user, uint16_t dev_id, struct modbus_adu *adu,
                     k_timeout_t timeout)
{
    ARG_UNUSED(timeout);
    ARG_UNUSED(dev_id);
    ARG_UNUSED(user);
    struct transfer_event event = {.address = sys_get_be16(adu->data),
                                    .time = k_uptime_get()};
    zassert_ok(k_msgq_put(&transfers, &event, K_NO_WAIT));
    if (adu->fc == 4) {
        adu->data[0] = 2;
        sys_put_be16(event.address, adu->data + 1);
        adu->length = 3;
    }
    return 0;
}

static int initialize(void *user, uint16_t dev_id)
{
    ARG_UNUSED(user);
    k_sem_give(&init_entered);
    if (atomic_get(&hold_init)) {
        zassert_ok(k_sem_take(&init_release, K_SECONDS(1)));
    }
    int err = atomic_get(&init_error);
    uint16_t word;
    return err ? err : co_read(dev_id, 4, 200, &word, 1);
}

static void completed(const struct co_request *request, int error, void *user)
{
    zassert_equal(user, &equipment);
    struct completion_event event = {.id = request->id, .error = error};
    zassert_ok(k_msgq_put(&completions, &event, K_NO_WAIT));
}

static int submit(uint32_t id, enum co_port_mode mode, uint8_t priority)
{
    struct co_request request = {
        .id = id, .dev_id = 1, .priority = priority, .mode = mode,
        .adu = {.fc = 6, .length = 4},
    };
    sys_put_be16(id, request.adu.data);
    sys_put_be16(123, request.adu.data + 2);
    return co_submit(&request, completed, (void *)&equipment);
}

static void expect_completion(uint32_t id, int error)
{
    struct completion_event event;
    zassert_ok(k_msgq_get(&completions, &event, K_MSEC(50)));
    zassert_equal(event.id, id);
    zassert_equal(event.error, error);
}

static struct transfer_event expect_transfer(uint16_t address, k_timeout_t timeout)
{
    struct transfer_event event;
    zassert_ok(k_msgq_get(&transfers, &event, timeout));
    zassert_equal(event.address, address);
    return event;
}

static void set_mode(enum co_port_mode mode)
{
    int err = -EBUSY;
    for (unsigned i = 0; i < 20 && err == -EBUSY; ++i) {
        err = co_set_mode(0, mode);
        if (err == -EBUSY) { k_sleep(K_MSEC(1)); }
    }
    zassert_ok(err);
}

static void *setup(void)
{
    const struct co_bus_cfg bus = {
        .id = 0, .iface = -1, .transfer = transfer, .initialize = initialize,
    };
    zassert_ok(dp_init_config(&equipment)); fixture_start_dp();
    if (!IS_ENABLED(CONFIG_CO_MODBUS_ADAPTER)) {
        zassert_false(IS_ENABLED(CONFIG_MODBUS_CLIENT));
        zassert_false(IS_ENABLED(CONFIG_TRANSMIT));
        const struct co_port_config port = {.iface_name = "RAW_0"};
        zassert_equal(co_port_open(&port), -ENOTSUP);
        zassert_equal(co_init(&equipment, 0, false), -ENOTSUP);
    }
    /* 表中引用稍后登记但超出配置池范围的总线时，必须拒绝，
     * 即使当前登记总线 ID 有效。
     */
    struct dp_device_cfg invalid_device = devices[0];
    invalid_device.bus_id = CONFIG_CO_MAX_BUSES;
    struct dp_equip_cfg invalid = equipment;
    invalid.devices = &invalid_device;
    zassert_ok(dp_validate(&invalid)); /* 采集器容量不归 DP 管理。 */
    zassert_equal(co_init_bus(&invalid, &bus), -EINVAL);
    struct co_bus_cfg invalid_bus = bus;
    invalid_bus.id = CONFIG_CO_MAX_BUSES;
    zassert_equal(co_init_bus(&equipment, &invalid_bus), -EINVAL);
    /* 两种拒绝都不能预留任务或安装无效设备。 */
    fixture_connect_points();
    zassert_ok(co_init_bus(&equipment, &bus));
    zassert_ok(co_enable_poll(0, false));
    set_mode(CO_PORT_SLEEP);
    zassert_ok(co_polling_init());
    set_mode(CO_PORT_UPDATE);
    zassert_ok(submit(999, CO_PORT_UPDATE, 0));
    struct completion_event delayed;
    zassert_equal(k_msgq_get(&completions, &delayed, K_MSEC(5)), -EAGAIN,
                  "CO must wait for the application-owned worker");
    fixture_start_co();
    expect_completion(999, 0);
    expect_transfer(999, K_NO_WAIT);
    set_mode(CO_PORT_SLEEP);
    return NULL;
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    zassert_ok(co_enable_poll(0, false));
    set_mode(CO_PORT_SLEEP);
    zassert_ok(co_cancel(0));
    k_msgq_purge(&completions);
    k_msgq_purge(&transfers);
    atomic_clear(&hold_init);
    atomic_clear(&init_error);
    atomic_clear(&init_callbacks);
    k_sem_reset(&init_entered);
    k_sem_reset(&init_release);
    k_sem_reset(&init_finished);
}

ZTEST(collector, test_queue_capacity_priority_and_cancel)
{
    zassert_equal(CONFIG_CO_QUEUE_DEPTH, 3);
    fail_allocation_owner = k_current_get();
    zassert_equal(submit(9, CO_PORT_UPDATE, 0), -ENOMEM);
    zassert_is_null(fail_allocation_owner);
    zassert_ok(submit(10, CO_PORT_UPDATE, 0));
    zassert_ok(submit(11, CO_PORT_UPDATE, 3));
    zassert_ok(submit(12, CO_PORT_UPDATE, 1));
    zassert_equal(submit(13, CO_PORT_UPDATE, 0), -ENOSPC);
    k_sleep(K_MSEC(20));
    zassert_equal(k_msgq_num_used_get(&transfers), 0);
    set_mode(CO_PORT_UPDATE);
    expect_completion(11, 0);
    expect_completion(12, 0);
    expect_completion(10, 0);
    expect_transfer(11, K_NO_WAIT);
    expect_transfer(12, K_NO_WAIT);
    expect_transfer(10, K_NO_WAIT);
    set_mode(CO_PORT_SLEEP);
    zassert_ok(submit(14, CO_PORT_POLL, 0));
    zassert_ok(submit(15, CO_PORT_UPDATE, 0));
    zassert_ok(co_cancel(0));
    expect_completion(14, -ECANCELED);
    expect_completion(15, -ECANCELED);
    zassert_ok(co_cancel(0));
    zassert_equal(k_msgq_num_used_get(&completions), 0);
    zassert_equal(k_msgq_num_used_get(&transfers), 0);
}

ZTEST(collector, test_aging_and_matching_modes)
{
    zassert_ok(submit(20, CO_PORT_UPDATE, 0));
    k_sleep(K_MSEC(1100));
    zassert_ok(submit(21, CO_PORT_UPDATE, 1));
    zassert_ok(submit(22, CO_PORT_INIT, 3));
    set_mode(CO_PORT_UPDATE);
    expect_completion(20, 0);
    expect_completion(21, 0);
    k_sleep(K_MSEC(20));
    zassert_equal(k_msgq_num_used_get(&completions), 0);
    set_mode(CO_PORT_INIT);
    expect_completion(22, 0);
    /* 此模式没有期限，新请求必须唤醒阻塞的任务。 */
    k_sleep(K_MSEC(20));
    zassert_ok(submit(23, CO_PORT_INIT, 0));
    expect_completion(23, 0);
}

ZTEST(collector, test_poll_deadline_disable_and_reenable)
{
    struct transfer_event event;
    zassert_ok(co_enable_poll(0, true));
    zassert_not_equal(k_msgq_get(&transfers, &event, K_MSEC(20)), 0);
    set_mode(CO_PORT_POLL);
    struct transfer_event first = expect_transfer(100, K_MSEC(50));
    zassert_not_equal(k_msgq_get(&transfers, &event, K_MSEC(20)), 0);
    struct transfer_event second = expect_transfer(100, K_MSEC(100));
    zassert_true(second.time - first.time >= 80);
    zassert_ok(co_enable_poll(0, false));
    zassert_not_equal(k_msgq_get(&transfers, &event, K_MSEC(120)), 0);
    zassert_ok(co_enable_poll(0, true));
    expect_transfer(100, K_MSEC(50));
    set_mode(CO_PORT_SLEEP);
    zassert_not_equal(k_msgq_get(&transfers, &event, K_MSEC(120)), 0);
}

static void initialize_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    init_result = co_initialize_device(1);
    k_sem_give(&init_finished);
}

static void start_initialization(int error)
{
    atomic_set(&hold_init, 1);
    atomic_set(&init_error, error);
    k_thread_create(&init_thread, init_stack, K_THREAD_STACK_SIZEOF(init_stack),
                    initialize_thread, NULL, NULL, NULL, 6, 0, K_NO_WAIT);
    zassert_ok(k_sem_take(&init_entered, K_MSEC(50)));
}

static void finish_initialization(int expected)
{
    k_sem_give(&init_release);
    zassert_ok(k_sem_take(&init_finished, K_MSEC(100)));
    zassert_ok(k_thread_join(&init_thread, K_MSEC(100)));
    zassert_equal(init_result, expected);
}

ZTEST(collector, test_initialization_owns_bus_and_wakes_restored_mode)
{
    set_mode(CO_PORT_UPDATE);
    start_initialization(0);
    zassert_ok(submit(30, CO_PORT_INIT, 3));
    zassert_ok(submit(31, CO_PORT_UPDATE, 0));
    k_sleep(K_MSEC(30));
    zassert_equal(k_msgq_num_used_get(&transfers), 0);
    zassert_equal(co_set_mode(0, CO_PORT_SLEEP), -EBUSY);
    finish_initialization(0);
    expect_completion(31, 0);
    expect_transfer(200, K_NO_WAIT); /* 持有初始化所有权的 INIT 上下文允许读取。 */
    expect_transfer(300, K_NO_WAIT); /* 仅用于初始化的表条目。 */
    expect_transfer(31, K_NO_WAIT);
    zassert_equal(atomic_get(&init_callbacks), 1); /* 不依赖 NVS。 */
    enum co_port_mode mode;
    zassert_ok(co_get_mode(0, &mode));
    zassert_equal(mode, CO_PORT_UPDATE);
    zassert_ok(co_cancel(0));
    expect_completion(30, -ECANCELED);
}

ZTEST(collector, test_failed_initialization_releases_bus)
{
    set_mode(CO_PORT_UPDATE);
    start_initialization(-EIO);
    zassert_ok(submit(40, CO_PORT_UPDATE, 0));
    finish_initialization(-EIO);
    expect_completion(40, 0);
    expect_transfer(40, K_NO_WAIT);
    zassert_equal(atomic_get(&init_callbacks), 0);
}

ZTEST_SUITE(collector, NULL, setup, before, NULL, NULL);
