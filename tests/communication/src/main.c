#include <collect/modbus.h>
#include <collect/port.h>
#include <transmit/modbus.h>
#include <transmit/modbus_adapter.h>
#include <modbus_io.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <errno.h>
#include <string.h>

/* uart-emul 枚举所有子节点；Modbus 节点仅生成接口，测试补充占位 device。 */
DEVICE_DT_DEFINE(DT_NODELABEL(modbus_test), NULL, NULL, NULL, NULL, POST_KERNEL, 90, NULL);
static int serial, client, server, forward;
static const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(uart_test));
static const struct gpio_dt_spec de = GPIO_DT_SPEC_GET(DT_NODELABEL(modbus_test), de_gpios);
static atomic_t stall_tx, fail_tx, hold_handler, capture, completed, tx_error;
static K_SEM_DEFINE(blocked, 0, 1);
static K_SEM_DEFINE(unblock, 0, 1);
static K_SEM_DEFINE(reconfigured, 0, 1);
static uint8_t serial_reply[CONFIG_MODBUS_BUFFER_SIZE], captured[CONFIG_MODBUS_BUFFER_SIZE];
static size_t serial_size, captured_size;
static struct vport_source captured_source;
static uint16_t request_crc;
static int configure_error;
static int read_register(uint16_t address, uint16_t *value)
{
    if (atomic_get(&hold_handler)) { k_sem_give(&blocked); k_sem_take(&unblock, K_FOREVER); }
    *value = address ^ 0x1234;
    return 0;
}
static struct modbus_user_callbacks callbacks = {.input_reg_rd = read_register};
static int echo(int iface, const struct modbus_adu *rx, struct modbus_adu *tx, void *user)
{
    ARG_UNUSED(user);
    if (rx->fc != 65) { return -ENOENT; }
    if (iface == forward) { request_crc = rx->crc; }
    *tx = *rx; return 0;
}
static int send(void *user, uint16_t port, const struct vport_source *destination,
                 const struct vport_frame *frame, k_timeout_t timeout)
{
    ARG_UNUSED(user);
    if (atomic_get(&fail_tx)) { return -EIO; }
    if (port == 1) {
        zassert_equal(destination->link, 11);
        int err = mb_serial_send(serial, frame->data, frame->size, timeout);
        atomic_set(&tx_error, err); return err;
    }
    if (port == 2) { return tr_port_input(3, destination, frame); }
    if (atomic_get(&capture)) {
        captured_source = *destination; captured_size = frame->size;
        memcpy(captured, frame->data, frame->size); return 0;
    }
    return co_port_input(2, destination, frame);
}
static void finished(void *user, uint16_t port, const struct vport_source *source)
{
    ARG_UNUSED(user); ARG_UNUSED(source);
    if (port == 1) { mb_serial_release(serial); }
    atomic_inc(&completed);
}
static const struct vport_io forward_io = {.send = send, .complete = finished,
    .mtu = CONFIG_MODBUS_BUFFER_SIZE, .destination = {.link = 11}};
static const struct vport_io client_io = {.send = send, .complete = finished,
    .mtu = CONFIG_MODBUS_BUFFER_SIZE, .destination = {.link = 22}};
static int serial_receive(void *user, const uint8_t *data, size_t size)
{
    ARG_UNUSED(user);
    const struct vport_source source = {.link = 11};
    const struct vport_frame frame = {.data = data, .size = size};
    return tr_port_input(1, &source, &frame);
}
static void uart_transmitted(const struct device *dev, size_t size, void *user)
{
    ARG_UNUSED(size); ARG_UNUSED(user);
    if (atomic_get(&stall_tx)) { uart_irq_tx_disable(dev); return; }
    serial_size += uart_emul_get_tx_data(dev, serial_reply + serial_size,
                                        sizeof(serial_reply) - serial_size);
}
static void configured(int iface, int error, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user); configure_error = error;
    k_sem_give(&reconfigured);
}
static void wait_complete(int before)
{
    for (int i = 0; i < 250 && atomic_get(&completed) == before; ++i) { k_msleep(1); }
    zassert_true(atomic_get(&completed) > before); k_msleep(1);
}
static void serial_request(uint8_t unit, bool corrupt)
{
    uint8_t data[] = {unit, 4, 0, 7, 0, 1, 0, 0};
    sys_put_le16(crc16_ansi(data, 6) ^ corrupt, data + 6);
    serial_size = 0;
    zassert_equal(uart_emul_put_rx_data(uart, data, sizeof(data)), sizeof(data));
}
static void check_serial_reply(void)
{
    uint8_t expected[] = {16, 4, 2, 0x12, 0x33, 0, 0};
    sys_put_le16(crc16_ansi(expected, 5), expected + 5);
    zassert_equal(serial_size, sizeof(expected));
    zassert_mem_equal(serial_reply, expected, sizeof(expected));
    zassert_equal(gpio_emul_output_get(de.port, de.pin), 0);
}
static void *setup(void)
{
    client = modbus_iface_get_by_name("RAW_0"); server = modbus_iface_get_by_name("RAW_1");
    forward = modbus_iface_get_by_name("RAW_2");
    zassert_equal(co_modbus_port_bind(2, DEVICE_DT_NAME(DT_NODELABEL(modbus_test)), &client_io), -EINVAL);
    zassert_ok(co_modbus_port_bind(2, "RAW_0", &client_io));
    zassert_equal(tr_modbus_port_bind(2, "RAW_1", &client_io), -EEXIST);
    zassert_ok(tr_modbus_port_bind(3, "RAW_1", &client_io));
    zassert_ok(tr_modbus_port_bind(1, "RAW_2", &forward_io));
    uart_emul_callback_tx_data_ready_set(uart, uart_transmitted, NULL);
    struct modbus_serial_param serial_param = {.baud = 115200, .parity = UART_CFG_PARITY_NONE,
                                              .stop_bits = UART_CFG_STOP_BITS_1};
    serial = mb_serial_open(DEVICE_DT_NAME(DT_NODELABEL(modbus_test)), serial_param, serial_receive, NULL);
    zassert_true(serial >= 0);
    struct modbus_iface_param param = {.mode = MODBUS_MODE_RAW,
        .server = {.unit_id = 16, .user_cb = &callbacks}};
    zassert_ok(tr_modbus_init_server(forward, param, NULL, true));
    zassert_ok(tr_modbus_set_router(forward, echo, NULL));
    param.server.unit_id = 1;
    zassert_ok(tr_modbus_init_server(server, param, NULL, true));
    zassert_ok(tr_modbus_set_router(server, echo, NULL));
    static const struct co_port_config config = {.iface_name = "RAW_0",
        .param = {.mode = MODBUS_MODE_RAW, .rx_timeout = 100000}};
    zassert_equal(co_port_open(&config), client);
    return NULL;
}
ZTEST(communication, test_serial_roundtrip_and_crc_rejection)
{
    serial_request(16, true); k_msleep(20); zassert_equal(serial_size, 0);
    int before = atomic_get(&completed); serial_request(16, false);
    wait_complete(before); check_serial_reply();
}
ZTEST(communication, test_serial_full_adu_capacity)
{
    uint8_t request[CONFIG_MODBUS_BUFFER_SIZE]; request[0] = 16; request[1] = 65;
    for (size_t i = 2; i < sizeof(request) - 2; ++i) { request[i] = i; }
    sys_put_le16(crc16_ansi(request, sizeof(request) - 2), request + sizeof(request) - 2);
    serial_size = 0; int before = atomic_get(&completed);
    zassert_equal(uart_emul_put_rx_data(uart, request, sizeof(request)), sizeof(request));
    wait_complete(before);
    zassert_equal(serial_size, sizeof(request)); zassert_mem_equal(serial_reply, request, sizeof(request));
    zassert_equal(request_crc, sys_get_le16(request + sizeof(request) - 2));
}
ZTEST(communication, test_raw_client_server_and_send_error)
{
    uint16_t word;
    zassert_ok(modbus_read_input_regs(client, 1, 7, &word, 1)); zassert_equal(word, 0x1233);
    atomic_set(&fail_tx, 1);
    zassert_equal(modbus_read_input_regs(client, 1, 7, &word, 1), -EIO);
    atomic_clear(&fail_tx);
    zassert_ok(modbus_read_input_regs(client, 1, 8, &word, 1)); zassert_equal(word, 0x123c);
}
ZTEST(communication, test_source_preserved_and_busy_request_not_overwritten)
{
    uint8_t data[] = {1, 4, 0, 7, 0, 1, 0, 0};
    sys_put_le16(crc16_ansi(data, 6), data + 6);
    struct vport_frame frame = {.data = data, .size = sizeof(data)};
    const struct vport_source source = {.link = 31, .peer = 0x123456789abcdefULL};
    atomic_set(&capture, 1); atomic_set(&hold_handler, 1);
    int before = atomic_get(&completed);
    zassert_ok(tr_port_input(3, &source, &frame));
    zassert_ok(k_sem_take(&blocked, K_MSEC(100)));
    data[3] = 8; sys_put_le16(crc16_ansi(data, 6), data + 6);
    zassert_equal(tr_port_input(3, &source, &frame), -EBUSY);
    zassert_equal(co_port_input(3, &source, &frame), -ENOENT);
    k_sem_give(&unblock); wait_complete(before);
    zassert_equal(captured_source.link, source.link); zassert_equal(captured_source.peer, source.peer);
    zassert_equal(captured_size, 7); zassert_equal(sys_get_be16(captured + 3), 0x1233);
    atomic_clear(&capture); atomic_clear(&hold_handler);
}
ZTEST(communication, test_serial_timeout_and_silent_request_recover)
{
    atomic_set(&stall_tx, 1); int before = atomic_get(&completed);
    serial_request(16, false); wait_complete(before);
    zassert_equal(atomic_get(&tx_error), -ETIMEDOUT);
    zassert_equal(gpio_emul_output_get(de.port, de.pin), 0);
    atomic_clear(&stall_tx); uart_emul_flush_tx_data(uart);
    before = atomic_get(&completed); serial_request(17, false); wait_complete(before);
    zassert_equal(serial_size, 0);
    before = atomic_get(&completed); serial_request(16, false); wait_complete(before); check_serial_reply();
}
ZTEST(communication, test_reconfigure_and_recovery_keep_port_binding)
{
    struct modbus_serial_param serial_param = {.baud = 9600, .parity = UART_CFG_PARITY_NONE,
                                              .stop_bits = UART_CFG_STOP_BITS_1};
    zassert_ok(mb_serial_configure(serial, serial_param));
    struct uart_config current; zassert_ok(uart_config_get(uart, &current));
    zassert_equal(current.baudrate, 9600);
    struct modbus_iface_param param = {.mode = MODBUS_MODE_RAW,
        .server = {.unit_id = 17, .user_cb = &callbacks}};
    zassert_ok(tr_modbus_reconfigure(forward, param, configured, NULL));
    zassert_ok(k_sem_take(&reconfigured, K_MSEC(200))); zassert_ok(configure_error);
    int before = atomic_get(&completed); serial_request(17, false); wait_complete(before);
    zassert_equal(serial_reply[0], 17);
    zassert_ok(tr_modbus_recover(forward, configured, NULL));
    zassert_ok(k_sem_take(&reconfigured, K_MSEC(200))); zassert_ok(configure_error);
    param.server.unit_id = 16;
    zassert_ok(tr_modbus_reconfigure(forward, param, configured, NULL));
    zassert_ok(k_sem_take(&reconfigured, K_MSEC(200))); zassert_ok(configure_error);
    serial_param.baud = 115200; zassert_ok(mb_serial_configure(serial, serial_param));
}
ZTEST(communication, test_port_registration_and_malformed_input)
{
    zassert_ok(co_modbus_port_bind(2, "RAW_0", &client_io));
    zassert_equal(co_modbus_port_bind(2, "RAW_1", &client_io), -EEXIST);
    uint8_t data[] = {1, 4, 0, 0};
    const struct vport_frame frame = {.data = data, .size = sizeof(data)};
    const struct vport_source source = {.link = 22};
    zassert_equal(tr_port_input(3, &source, &frame), -EBADMSG);
}
ZTEST(communication, test_overlong_frame_with_valid_prefix_is_rejected)
{
    uint8_t data[CONFIG_MODBUS_BUFFER_SIZE + 1];
    data[0] = 16; data[1] = 65;
    memset(data + 2, 0x53, sizeof(data) - 2);
    sys_put_le16(crc16_ansi(data, CONFIG_MODBUS_BUFFER_SIZE - 2), data + CONFIG_MODBUS_BUFFER_SIZE - 2);
    serial_size = 0;
    zassert_equal(uart_emul_put_rx_data(uart, data, sizeof(data)), sizeof(data));
    k_msleep(30); zassert_equal(serial_size, 0);
    int before = atomic_get(&completed);
    serial_request(16, false); wait_complete(before); check_serial_reply();
}
ZTEST(communication, test_serial_settings_defer_until_request_finishes)
{
    atomic_set(&hold_handler, 1);
    int before = atomic_get(&completed); serial_request(16, false);
    zassert_ok(k_sem_take(&blocked, K_MSEC(100)));
    const struct modbus_serial_param param = {.baud = 9600, .parity = UART_CFG_PARITY_NONE,
                                             .stop_bits = UART_CFG_STOP_BITS_1};
    zassert_equal(mb_serial_configure(serial, param), -EBUSY);
    k_sem_give(&unblock); wait_complete(before); check_serial_reply();
    atomic_clear(&hold_handler);
}
ZTEST_SUITE(communication, NULL, setup, NULL, NULL, NULL);
