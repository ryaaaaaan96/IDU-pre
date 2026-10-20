#include <transmit/modbus_transport.h>
#include <transmit/modbus_adapter.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

/* uart-emul 枚举所有子节点；Modbus 节点通常只生成接口，不生成 device。
 * 此测试占位仅用于 emulator 枚举，实际协议仍由 Zephyr Modbus 执行。
 */
DEVICE_DT_DEFINE(DT_NODELABEL(modbus_test), NULL, NULL, NULL, NULL,
                  POST_KERNEL, 90, NULL);

static int serial, client, server;
static const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(uart_test));
static const struct gpio_dt_spec de = GPIO_DT_SPEC_GET(DT_NODELABEL(modbus_test), de_gpios);
static const struct tr_modbus_transport endpoints[] = {
    {.iface_name = DEVICE_DT_NAME(DT_NODELABEL(modbus_test)), .port = 10, .link = 1},
    {.iface_name = "RAW_0", .port = 20, .link = 2},
    {.iface_name = "RAW_1", .port = 30, .link = 3},
};
static K_SEM_DEFINE(blocked, 0, 1);
static K_SEM_DEFINE(unblock, 0, 1);
static K_SEM_DEFINE(reply_ready, 0, 1);
static K_SEM_DEFINE(reconfigured, 0, 1);
static atomic_t stall_tx, fail_tx, capture_raw, hold_handler, reads;
static struct modbus_adu raw_reply;
static uint8_t serial_reply[CONFIG_MODBUS_BUFFER_SIZE];
static size_t serial_size;
static uint16_t serial_request_crc;
static struct k_spinlock serial_lock;
static int configure_error;

static void blocked_receive(const struct vport_source *source,
                             const struct vport_frame *frame, void *user)
{
    ARG_UNUSED(source); ARG_UNUSED(frame); ARG_UNUSED(user);
    k_sem_give(&blocked);
    k_sem_take(&unblock, K_MSEC(500)); /* 故障注入：模拟 RX worker 暂时无法运行。 */
}

static int sink_send(void *context, uint64_t peer,
                      const struct vport_frame *frame, k_timeout_t timeout)
{
    ARG_UNUSED(context); ARG_UNUSED(peer); ARG_UNUSED(frame); ARG_UNUSED(timeout);
    return 0;
}
static const struct vport_backend_ops sink = {.send = sink_send};
static const struct vport_link_config links[] = {
    {.id = 1, .type = VPORT_DATAGRAM, .mtu = TR_MODBUS_TRANSPORT_MTU,
     .ops = &tr_modbus_transport_backend, .context = (void *)&endpoints[0]},
    {.id = 2, .type = VPORT_DATAGRAM, .mtu = TR_MODBUS_TRANSPORT_MTU,
     .ops = &tr_modbus_transport_backend, .context = (void *)&endpoints[1]},
    {.id = 3, .type = VPORT_DATAGRAM, .mtu = TR_MODBUS_TRANSPORT_MTU,
     .ops = &tr_modbus_transport_backend, .context = (void *)&endpoints[2]},
    {.id = 4, .type = VPORT_DATAGRAM, .mtu = 1, .ops = &sink},
};
static const struct vport_port_config ports[] = {
    {.id = 10, .type = VPORT_DATAGRAM, .receive = tr_modbus_transport_receive,
     .user = (void *)&endpoints[0]},
    {.id = 20, .type = VPORT_DATAGRAM, .receive = tr_modbus_transport_receive,
     .user = (void *)&endpoints[1]},
    {.id = 30, .type = VPORT_DATAGRAM, .receive = tr_modbus_transport_receive,
     .user = (void *)&endpoints[2]},
    {.id = 40, .type = VPORT_DATAGRAM, .receive = blocked_receive},
};
static const struct vport_route routes[] = {
    {.port = 10, .link = 1, .directions = VPORT_RX | VPORT_TX},
    {.port = 20, .link = 2, .directions = VPORT_RX | VPORT_TX},
    {.port = 30, .link = 3, .directions = VPORT_RX | VPORT_TX},
    {.port = 40, .link = 4, .directions = VPORT_RX},
};
static const struct vport_config config = {
    .links = links, .link_count = ARRAY_SIZE(links),
    .ports = ports, .port_count = ARRAY_SIZE(ports),
    .routes = routes, .route_count = ARRAY_SIZE(routes),
};

static const struct tr_modbus_network network = {
    .endpoints = endpoints, .endpoint_count = ARRAY_SIZE(endpoints), .ports = &config,
};

static int read_register(uint16_t address, uint16_t *value)
{
    atomic_inc(&reads);
    if (atomic_get(&hold_handler)) {
        k_sem_give(&blocked);
        k_sem_take(&unblock, K_MSEC(500));
    }
    *value = address ^ 0x1234;
    return 0;
}
static struct modbus_user_callbacks callbacks = {.input_reg_rd = read_register};

static int raw_request(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    if (atomic_cas(&fail_tx, 1, 0)) { return -EIO; }
    return modbus_raw_submit_rx(server, adu);
}
static int raw_response(int iface, const struct modbus_adu *adu, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    if (atomic_get(&capture_raw)) {
        raw_reply = *adu;
        k_sem_give(&reply_ready);
        return 0;
    }
    return modbus_raw_submit_rx(client, adu);
}
static int echo(int iface, const struct modbus_adu *request,
                 struct modbus_adu *response, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    if (request->fc != 65) { return -ENOENT; }
    if (iface == serial) { serial_request_crc = request->crc; }
    *response = *request;
    return 0;
}

static void uart_transmitted(const struct device *dev, size_t size, void *user)
{
    ARG_UNUSED(size); ARG_UNUSED(user);
    if (atomic_get(&stall_tx)) {
        uart_irq_tx_disable(dev); /* 模拟 UART 不再报告 TX complete。 */
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&serial_lock);
    serial_size += uart_emul_get_tx_data(dev, serial_reply + serial_size,
                                         sizeof(serial_reply) - serial_size);
    k_spin_unlock(&serial_lock, key);
}

static void configured(int iface, int error, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    configure_error = error;
    k_sem_give(&reconfigured);
}
static struct modbus_iface_param serial_param(uint8_t unit, uint32_t baud)
{
    return (struct modbus_iface_param){
        .mode = MODBUS_MODE_RTU, .server = {.unit_id = unit, .user_cb = &callbacks},
        .serial = {.baud = baud, .parity = UART_CFG_PARITY_NONE,
                    .stop_bits = UART_CFG_STOP_BITS_1},
    };
}
static struct vport_stats stats(unsigned link)
{
    struct vport_stats result;
    zassert_ok(vport_get_stats(link, &result));
    return result;
}
static void wait_tx(unsigned link, uint32_t completed)
{
    for (int i = 0; i < 200 && stats(link).tx_completed < completed; ++i) { k_msleep(1); }
    zassert_true(stats(link).tx_completed >= completed);
}
static void serial_request(uint8_t unit, bool corrupt)
{
    uint8_t request[] = {unit, 4, 0, 7, 0, 1, 0, 0};
    sys_put_le16(crc16_ansi(request, 6) ^ corrupt, request + 6);
    k_spinlock_key_t key = k_spin_lock(&serial_lock);
    serial_size = 0;
    k_spin_unlock(&serial_lock, key);
    zassert_equal(uart_emul_put_rx_data(uart, request, sizeof(request)), sizeof(request));
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
    zassert_ok(tr_modbus_network_start(&network));
    serial = modbus_iface_get_by_name(endpoints[0].iface_name);
    client = modbus_iface_get_by_name("RAW_0");
    server = modbus_iface_get_by_name("RAW_1");
    uart_emul_callback_tx_data_ready_set(uart, uart_transmitted, NULL);
    zassert_ok(tr_modbus_set_router(serial, echo, NULL));
    zassert_ok(tr_modbus_init_server(serial, serial_param(16, 115200), NULL, true));
    zassert_ok(tr_modbus_set_router(server, echo, NULL));
    struct modbus_iface_param server_param = {
        .mode = MODBUS_MODE_RAW, .server = {.unit_id = 1, .user_cb = &callbacks},
        .rawcb = {.raw_tx_cb = raw_response},
    };
    zassert_ok(tr_modbus_init_server(server, server_param, NULL, true));
    struct modbus_iface_param client_param = {
        .mode = MODBUS_MODE_RAW, .rx_timeout = 100000, .rawcb = {.raw_tx_cb = raw_request},
    };
    zassert_ok(modbus_init_client(client, client_param));
    return NULL;
}

ZTEST(communication, test_serial_roundtrip_and_crc_rejection)
{
    struct vport_stats before = stats(1);
    serial_request(16, true);
    k_msleep(20);
    zassert_equal(serial_size, 0);
    zassert_equal(stats(1).rx_delivered, before.rx_delivered);
    serial_request(16, false);
    wait_tx(1, before.tx_completed + 1);
    check_serial_reply();
    zassert_equal(stats(1).rx_delivered, before.rx_delivered + 1);
}

ZTEST(communication, test_serial_full_adu_capacity)
{
    uint8_t request[CONFIG_MODBUS_BUFFER_SIZE];
    request[0] = 16;
    request[1] = 65;
    for (size_t i = 2; i < sizeof(request) - 2; ++i) { request[i] = i; }
    sys_put_le16(crc16_ansi(request, sizeof(request) - 2), request + sizeof(request) - 2);
    struct vport_stats before = stats(1);
    k_spinlock_key_t key = k_spin_lock(&serial_lock);
    serial_size = 0;
    k_spin_unlock(&serial_lock, key);
    zassert_equal(uart_emul_put_rx_data(uart, request, sizeof(request)), sizeof(request));
    wait_tx(1, before.tx_completed + 1);
    zassert_equal(serial_size, sizeof(request));
    zassert_mem_equal(serial_reply, request, sizeof(request));
    zassert_equal(serial_request_crc, sys_get_le16(request + sizeof(request) - 2));
    zassert_equal(gpio_emul_output_get(de.port, de.pin), 0);
}

ZTEST(communication, test_raw_client_server_use_both_routes)
{
    struct vport_stats c = stats(2), s = stats(3);
    uint16_t word = 0;
    zassert_ok(modbus_read_input_regs(client, 1, 7, &word, 1));
    zassert_equal(word, 0x1233);
    zassert_equal(stats(2).tx_completed, c.tx_completed + 1);
    zassert_equal(stats(2).rx_delivered, c.rx_delivered + 1);
    zassert_equal(stats(3).tx_completed, s.tx_completed + 1);
    zassert_equal(stats(3).rx_delivered, s.rx_delivered + 1);
}

ZTEST(communication, test_backend_error_is_reported_to_client)
{
    struct vport_stats before = stats(2);
    atomic_set(&fail_tx, 1);
    uint16_t word;
    zassert_equal(modbus_read_input_regs(client, 1, 7, &word, 1), -EIO);
    zassert_equal(stats(2).tx_errors, before.tx_errors + 1);
    zassert_ok(modbus_read_input_regs(client, 1, 7, &word, 1));
}

ZTEST(communication, test_raw_full_payload_and_no_overwrite)
{
    atomic_set(&capture_raw, 1);
    struct modbus_adu request = {.unit_id = 1, .fc = 65, .trans_id = 0xabcd};
    request.length = sizeof(request.data);
    for (size_t i = 0; i < request.length; ++i) { request.data[i] = i; }
    zassert_ok(modbus_raw_submit_rx(server, &request));
    zassert_ok(k_sem_take(&reply_ready, K_MSEC(100)));
    zassert_equal(raw_reply.length, request.length);
    zassert_equal(raw_reply.trans_id, request.trans_id);
    zassert_mem_equal(raw_reply.data, request.data, request.length);
    k_msleep(1);

    request = (struct modbus_adu){.unit_id = 1, .fc = 4, .length = 4,
                                 .data = {0, 7, 0, 1}};
    atomic_set(&hold_handler, 1);
    zassert_ok(modbus_raw_submit_rx(server, &request));
    zassert_ok(k_sem_take(&blocked, K_MSEC(100)));
    request.data[1] = 8;
    zassert_equal(modbus_raw_submit_rx(server, &request), -EBUSY);
    atomic_clear(&hold_handler);
    k_sem_give(&unblock);
    zassert_ok(k_sem_take(&reply_ready, K_MSEC(100)));
    zassert_equal(sys_get_be16(raw_reply.data + 1), 0x1233);
    k_msleep(1);
    atomic_clear(&capture_raw);
}

ZTEST(communication, test_timed_out_handoff_drops_stale_queued_frame)
{
    struct vport_frame block = {0};
    zassert_ok(vport_rx_submit(4, 0, &block));
    zassert_ok(k_sem_take(&blocked, K_MSEC(100)));
    uint16_t word;
    int count = atomic_get(&reads);
    zassert_equal(modbus_read_input_regs(client, 1, 7, &word, 1), -ETIMEDOUT);
    zassert_equal(atomic_get(&reads), count);
    k_sem_give(&unblock);
    k_msleep(2);
    zassert_ok(modbus_read_input_regs(client, 1, 8, &word, 1));
    zassert_equal(word, 0x123c);
}

ZTEST(communication, test_serial_timeout_releases_de_and_restores_receive)
{
    struct vport_stats before = stats(1);
    atomic_set(&stall_tx, 1);
    serial_request(16, false);
    for (int i = 0; i < 150 && stats(1).tx_errors == before.tx_errors; ++i) { k_msleep(1); }
    zassert_equal(stats(1).tx_errors, before.tx_errors + 1);
    zassert_equal(gpio_emul_output_get(de.port, de.pin), 0);
    atomic_clear(&stall_tx);
    uart_emul_flush_tx_data(uart);
    serial_request(16, false);
    wait_tx(1, before.tx_completed + 1);
    check_serial_reply();
}

ZTEST(communication, test_serial_reconfigure_and_recovery_keep_routes)
{
    zassert_ok(tr_modbus_reconfigure(serial, serial_param(17, 9600), configured, NULL));
    zassert_ok(k_sem_take(&reconfigured, K_MSEC(200)));
    zassert_ok(configure_error);
    struct uart_config current;
    zassert_ok(uart_config_get(uart, &current));
    zassert_equal(current.baudrate, 9600);
    struct vport_stats before = stats(1);
    serial_request(16, false);
    k_msleep(20);
    zassert_equal(serial_size, 0);
    serial_request(17, false);
    wait_tx(1, before.tx_completed + 1);
    zassert_equal(serial_reply[0], 17);
    zassert_ok(tr_modbus_recover(serial, configured, NULL));
    zassert_ok(k_sem_take(&reconfigured, K_MSEC(200)));
    zassert_ok(configure_error);
    zassert_ok(tr_modbus_reconfigure(serial, serial_param(16, 115200), configured, NULL));
    zassert_ok(k_sem_take(&reconfigured, K_MSEC(200)));
    zassert_ok(configure_error);
    serial_request(16, false);
    wait_tx(1, before.tx_completed + 2);
    check_serial_reply();
}

ZTEST(communication, test_unowned_virtual_send_cannot_overwrite_modbus_context)
{
    uint8_t data[12] = {0};
    sys_put_be16(2, data + 8);
    struct vport_frame frame = {.data = data, .size = sizeof(data)};
    zassert_equal(vport_send(20, 2, 0, &frame, K_MSEC(10)), -EACCES);
    uint16_t word;
    zassert_ok(modbus_read_input_regs(client, 1, 7, &word, 1));
}

ZTEST(communication, test_network_registration_is_idempotent)
{
    zassert_ok(tr_modbus_network_start(&network));
    struct tr_modbus_network other = network;
    zassert_equal(tr_modbus_network_start(&other), -EALREADY);
    zassert_equal(tr_modbus_network_start(NULL), -EINVAL);
}

ZTEST_SUITE(communication, NULL, setup, NULL, NULL, NULL);
