#include <modbus_support/server.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>
#include <string.h>

typedef struct {
    mb_server_t *server;
    k_tid_t thread;
    unsigned calls;
    int exception;
} raw_test_context_t;

static int raw_access(void *context, const mb_request_t *request)
{
    raw_test_context_t *test = context;
    zassert_equal(k_current_get(), test->thread);
    ++test->calls;
    zassert_equal(mb_server_close(test->server), -EBUSY);
    struct modbus_adu nested = {.unit_id = 3, .fc = 3, .length = 4};
    int iface = modbus_iface_get_by_name("RAW_0");
    zassert_equal(modbus_raw_submit_rx(iface, &nested), -EBUSY);
    struct modbus_adu unused;
    bool respond;
    zassert_equal(
        mb_server_process(test->server, &nested, &unused, &respond, raw_access, context), -EBUSY);
    if (test->exception) {
        return test->exception;
    }
    if (!request->writing) {
        for (size_t i = 0; i < request->count; ++i) {
            if (request->area == MB_DATA_AREA_COIL || request->area == MB_DATA_AREA_DISCRETE_INPUT) {
                request->data[i / 8] |= BIT(i % 8);
            } else {
                sys_put_be16(42, request->data + i * 2);
            }
        }
    }
    return 0;
}

ZTEST(modbus_raw_server, test_official_raw_adapter_and_protocol_validation)
{
    raw_test_context_t test = {.thread = k_current_get()};
    zassert_ok(mb_server_open(3, &test.server));
    mb_server_t *extra = NULL;
    zassert_equal(mb_server_open(4, &extra), -ENOSPC);
    zassert_is_null(extra);
    struct modbus_adu request = {.unit_id = 3, .fc = 3, .length = 4};
    sys_put_be16(45000, request.data); /* 禁用 FP 后高地址仍必须按整数处理。 */
    sys_put_be16(125, request.data + 2);
    zassert_equal(modbus_raw_submit_rx(modbus_iface_get_by_name("RAW_0"), &request), -EACCES);
    struct modbus_adu response;
    bool respond;
    zassert_ok(mb_server_process(test.server, &request, &response, &respond, raw_access, &test));
    zassert_true(respond);
    zassert_equal(test.calls, 1);
    zassert_equal(response.length, 251);
    zassert_equal(response.data[0], 250);
    zassert_equal(sys_get_be16(response.data + 249), 42);
    request.fc = 1;
    sys_put_be16(0, request.data);
    sys_put_be16(2000, request.data + 2);
    zassert_ok(mb_server_process(test.server, &request, &response, &respond, raw_access, &test));
    zassert_true(respond);
    zassert_equal(response.length, 251);
    zassert_equal(response.data[0], 250);
    zassert_equal(response.data[250], 0xff);
    request.fc = 3;
    /* 不足的固定请求和非法线圈值在官方校验阶段结束，不进入后端。 */
    request.length = 3;
    zassert_ok(mb_server_process(test.server, &request, &response, &respond, raw_access, &test));
    zassert_true(respond);
    zassert_equal(response.fc, 0x83);
    zassert_equal(response.data[0], MODBUS_EXC_ILLEGAL_DATA_VAL);
    request.fc = 5;
    request.length = 4;
    sys_put_be16(0x1234, request.data + 2);
    zassert_ok(mb_server_process(test.server, &request, &response, &respond, raw_access, &test));
    zassert_equal(response.data[0], MODBUS_EXC_ILLEGAL_DATA_VAL);
    zassert_equal(test.calls, 2);
    request.fc = 16;
    request.length = 12; /* 三寄存器后多出一个字节，旧整除检查会错误接受。 */
    sys_put_be16(10, request.data);
    sys_put_be16(3, request.data + 2);
    request.data[4] = 7;
    zassert_ok(mb_server_process(test.server, &request, &response, &respond, raw_access, &test));
    zassert_equal(response.data[0], MODBUS_EXC_ILLEGAL_DATA_VAL);
    zassert_equal(test.calls, 2);
    request.fc = 15;
    request.length = 252;
    sys_put_be16(0, request.data);
    sys_put_be16(1969, request.data + 2);
    request.data[4] = 247;
    zassert_ok(mb_server_process(test.server, &request, &response, &respond, raw_access, &test));
    zassert_equal(response.data[0], MODBUS_EXC_ILLEGAL_DATA_VAL);
    zassert_equal(test.calls, 2);
    request.fc = 3;
    request.length = 4;
    sys_put_be16(65535, request.data);
    sys_put_be16(2, request.data + 2);
    zassert_ok(mb_server_process(test.server, &request, &response, &respond, raw_access, &test));
    zassert_equal(response.data[0], MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(test.calls, 2);
    request.fc = 99;
    request.length = 0;
    zassert_ok(mb_server_process(test.server, &request, &response, &respond, raw_access, &test));
    zassert_true(respond);
    zassert_equal(response.data[0], MODBUS_EXC_ILLEGAL_FC);
    request.fc = 6;
    request.length = 4;
    sys_put_be16(10, request.data);
    test.exception = MODBUS_EXC_SERVER_DEVICE_FAILURE;
    zassert_ok(mb_server_process(test.server, &request, &response, &respond, raw_access, &test));
    zassert_equal(response.data[0], MODBUS_EXC_SERVER_DEVICE_FAILURE);
    zassert_equal(test.calls, 3);
    zassert_ok(mb_server_close(test.server));
    zassert_ok(mb_server_open(3, &extra)); /* 释放后可用于启动失败重试。 */
    zassert_ok(mb_server_close(extra));
}

static K_SEM_DEFINE(unowned_done, 0, 1);
static k_tid_t unowned_caller;
static struct modbus_adu unowned_response;

static int unowned_read(uint16_t address, uint16_t *value)
{
    zassert_equal(address, 7);
    zassert_not_equal(k_current_get(), unowned_caller);
    k_tid_t system_queue = k_work_queue_thread_get(&k_sys_work_q);
    if (IS_ENABLED(CONFIG_MODBUS_WORKQUEUE)) {
        zassert_not_equal(k_current_get(), system_queue);
        zassert_equal(strcmp(k_thread_name_get(k_current_get()), "modbus_work_q"), 0);
    } else {
        zassert_equal(k_current_get(), system_queue);
    }
    *value = 42;
    return 0;
}

static int unowned_send(int iface, const struct modbus_adu *response, void *context)
{
    ARG_UNUSED(iface);
    ARG_UNUSED(context);
    unowned_response = *response;
    k_sem_give(&unowned_done);
    return 0;
}

ZTEST(modbus_raw_server, test_unowned_raw_interface_keeps_official_workqueue)
{
    static struct modbus_user_callbacks callbacks = {.holding_reg_rd = unowned_read};
    unowned_caller = k_current_get();
    int iface = modbus_iface_get_by_name("RAW_0");
    zassert_true(iface >= 0);
    struct modbus_iface_param param = {.mode = MODBUS_MODE_RAW,
        .server = {.unit_id = 3, .user_cb = &callbacks},
        .rawcb = {.raw_tx_cb = unowned_send}};
    zassert_ok(modbus_init_server(iface, param));
    const struct modbus_adu request = {.unit_id = 3, .fc = 3, .length = 4, .data = {0, 7, 0, 1}};
    zassert_ok(modbus_raw_submit_rx(iface, &request));
    zassert_ok(k_sem_take(&unowned_done, K_SECONDS(1)));
    zassert_equal(unowned_response.data[0], 2);
    zassert_equal(sys_get_be16(unowned_response.data + 1), 42);
    zassert_ok(modbus_disable(iface));
}

#ifdef CONFIG_MODBUS_CLIENT
static unsigned client_reply_case;

static int client_reply(int iface, const struct modbus_adu *request, void *context)
{
    ARG_UNUSED(context);
    struct modbus_adu reply = {
        .unit_id = request->unit_id, .fc = request->fc, .length = 3, .data = {2, 0x12, 0x34}};
    if (client_reply_case == 1) {
        reply.length = 0;
    } else if (client_reply_case == 2) {
        reply.fc |= BIT(7);
        reply.length = 1;
        reply.data[0] = MODBUS_EXC_ILLEGAL_DATA_ADDR;
    }
    return modbus_raw_submit_rx(iface, &reply);
}

ZTEST(modbus_raw_server, test_unowned_raw_client_keeps_exception_length_validation)
{
    int iface = modbus_iface_get_by_name("RAW_0");
    struct modbus_iface_param param = {
        .mode = MODBUS_MODE_RAW, .rx_timeout = 100000, .rawcb = {.raw_tx_cb = client_reply}};
    zassert_ok(modbus_init_client(iface, param));
    for (client_reply_case = 0; client_reply_case < 3; ++client_reply_case) {
        uint16_t value = 0xcafe;
        int err = modbus_read_holding_regs(iface, 3, 7, &value, 1);
        if (client_reply_case == 0) {
            zassert_ok(err);
            zassert_equal(value, 0x1234);
        } else {
            zassert_equal(err, client_reply_case == 1 ? -EMSGSIZE : MODBUS_EXC_ILLEGAL_DATA_ADDR);
            zassert_equal(value, 0xcafe);
        }
    }
    zassert_ok(modbus_disable(iface));
}
#endif

ZTEST_SUITE(modbus_raw_server, NULL, NULL, NULL, NULL, NULL);
