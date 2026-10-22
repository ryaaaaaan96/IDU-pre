#include "../../fixtures/tr_tasks.h"
#include <transmit/modbus.h>
#include <transmit/modbus_adapter.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>
#include <string.h>
BUILD_ASSERT(!IS_ENABLED(CONFIG_COLLECT));
BUILD_ASSERT(!IS_ENABLED(CONFIG_MODBUS_CLIENT));
static K_SEM_DEFINE(done, 0, 1);
static uint8_t response[16];
static size_t response_size;
static struct vport_source response_source;
static int send(void *user, uint16_t port, const struct vport_source *source,
                 const struct vport_frame *frame, k_timeout_t timeout)
{
    ARG_UNUSED(user); ARG_UNUSED(timeout);
    zassert_equal(port, 9); zassert_true(frame->size <= sizeof(response));
    response_size = frame->size; memcpy(response, frame->data, frame->size);
    response_source = *source;
    return 0;
}
static void complete(void *user, uint16_t port, const struct vport_source *source)
{
    ARG_UNUSED(user); ARG_UNUSED(port); ARG_UNUSED(source); k_sem_give(&done);
}
static int read_word(uint16_t address, uint16_t *value)
{ *value = address + 100; return 0; }
static struct modbus_user_callbacks callbacks = {.input_reg_rd = read_word};
static const struct vport_io io = {.send = send, .complete = complete, .mtu = CONFIG_MODBUS_BUFFER_SIZE};
ZTEST(transmit_port, test_server_without_collect_or_client)
{
    zassert_ok(tr_modbus_port_bind(9, "RAW_0", &io));
    int iface = modbus_iface_get_by_name("RAW_0");
    struct modbus_iface_param param = {.mode = MODBUS_MODE_RAW,
        .server = {.unit_id = 6, .user_cb = &callbacks}};
    fixture_bind_tr(iface);
    zassert_ok(tr_modbus_init_server(iface, param, NULL, true));
    uint8_t request[] = {6, 4, 0, 7, 0, 1, 0, 0};
    sys_put_le16(crc16_ansi(request, 6), request + 6);
    const struct vport_frame frame = {.data = request, .size = sizeof(request)};
    const struct vport_source source = {.link = 20, .peer = 99};
    zassert_ok(tr_port_input(9, &source, &frame));
    zassert_ok(k_sem_take(&done, K_MSEC(100)));
    zassert_equal(response_size, 7); zassert_equal(sys_get_be16(response + 3), 107);
    zassert_equal(response_source.link, 20); zassert_equal(response_source.peer, 99);
    zassert_equal(crc16_ansi(response, 5), sys_get_le16(response + 5));
}
ZTEST_SUITE(transmit_port, NULL, NULL, NULL, NULL, NULL);
