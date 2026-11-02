#include <modbus_support/server.h>
#include "modbus_hooks.h"
#include <zephyr/sys/byteorder.h>
#include <errno.h>
#include <string.h>

/* 官方回调没有 user_data，通过启动时固定的 RAW 槽位传递上下文，避免线程全局变量。 */
struct mb_server {
    int iface;
    bool used;
    bool prepared;
    bool respond;
    uint8_t exception;
    atomic_ptr_t owner;
    const struct modbus_adu *rx;
    struct modbus_adu *tx;
    mb_access_t access;
    void *context;
    mb_request_t request;
    uint8_t data[250]; /**< 标准读取上限：125 个寄存器或 2000 位。 */
};

static struct mb_server servers[CONFIG_MODBUS_NUMOF_RAW_ADU];
static K_MUTEX_DEFINE(registration_lock);

static mb_server_t *find_server(int iface)
{
    for (size_t i = 0; i < ARRAY_SIZE(servers); ++i) {
        if (servers[i].used && servers[i].iface == iface) {
            return &servers[i];
        }
    }
    return NULL;
}

static int prepare(size_t slot, mb_data_area_t area, bool writing)
{
    mb_server_t *server = &servers[slot];
    if (!server->prepared) {
        const struct modbus_adu *rx = server->rx;
        bool single = rx->fc == 5 || rx->fc == 6;
        bool bits = area == MB_DATA_AREA_COIL || area == MB_DATA_AREA_DISCRETE_INPUT;
        server->prepared = true;
        server->request = (mb_request_t){.area = area,
            .writing = writing,
            .first = sys_get_be16(rx->data),
            .count = single ? 1 : sys_get_be16(rx->data + 2),
            .data = server->data};
        size_t size = bits ? DIV_ROUND_UP(server->request.count, 8) : server->request.count * 2;
        if ((uint32_t)server->request.first + server->request.count > 65536) {
            server->exception = MODBUS_EXC_ILLEGAL_DATA_ADDR;
        } else if (size > sizeof(server->data)) {
            server->exception = MODBUS_EXC_ILLEGAL_DATA_VAL;
        } else {
            if (writing) {
                if (single && bits) {
                    server->data[0] = rx->data[2] ? 1 : 0;
                } else {
                    memcpy(server->data, rx->data + (single ? 2 : 5), size);
                }
            } else {
                memset(server->data, 0, size);
            }
            int err = server->access(server->context, &server->request);
            if (err) {
                server->exception =
                    err > 0 && err <= UINT8_MAX ? err : MODBUS_EXC_SERVER_DEVICE_FAILURE;
            }
        }
    }
    return server->exception ? -EIO : 0;
}

static int read_register(
    size_t slot, mb_data_area_t area, uint16_t address, uint16_t *value)
{
    int err = prepare(slot, area, false);
    if (!err) {
        mb_server_t *server = &servers[slot];
        *value = sys_get_be16(server->data + (address - server->request.first) * 2);
    }
    return err;
}

static int read_bit(size_t slot, mb_data_area_t area, uint16_t address, bool *value)
{
    int err = prepare(slot, area, false);
    if (!err) {
        mb_server_t *server = &servers[slot];
        size_t offset = address - server->request.first;
        *value = (server->data[offset / 8] >> (offset % 8)) & 1;
    }
    return err;
}

#define DEFINE_CALLBACKS(n, unused)                                                               \
    static int coil_rd_##n(uint16_t address, bool *value)                                         \
    {                                                                                             \
        return read_bit(n, MB_DATA_AREA_COIL, address, value);                                       \
    }                                                                                             \
    static int di_rd_##n(uint16_t address, bool *value)                                           \
    {                                                                                             \
        return read_bit(n, MB_DATA_AREA_DISCRETE_INPUT, address, value);                             \
    }                                                                                             \
    static int input_rd_##n(uint16_t address, uint16_t *value)                                    \
    {                                                                                             \
        return read_register(n, MB_DATA_AREA_INPUT_REGISTER, address, value);                        \
    }                                                                                             \
    static int holding_rd_##n(uint16_t address, uint16_t *value)                                  \
    {                                                                                             \
        return read_register(n, MB_DATA_AREA_HOLDING_REGISTER, address, value);                      \
    }                                                                                             \
    static int coil_wr_##n(uint16_t address, bool value)                                          \
    {                                                                                             \
        ARG_UNUSED(address);                                                                      \
        ARG_UNUSED(value);                                                                        \
        return prepare(n, MB_DATA_AREA_COIL, true);                                                  \
    }                                                                                             \
    static int holding_wr_##n(uint16_t address, uint16_t value)                                   \
    {                                                                                             \
        ARG_UNUSED(address);                                                                      \
        ARG_UNUSED(value);                                                                        \
        return prepare(n, MB_DATA_AREA_HOLDING_REGISTER, true);                                      \
    }
LISTIFY(CONFIG_MODBUS_NUMOF_RAW_ADU, DEFINE_CALLBACKS, (), _)

#define CALLBACK_ROW(n, unused)                                                                   \
    {.coil_rd = coil_rd_##n,                                                                      \
        .discrete_input_rd = di_rd_##n,                                                           \
        .input_reg_rd = input_rd_##n,                                                             \
        .holding_reg_rd = holding_rd_##n,                                                         \
        .coil_wr = coil_wr_##n,                                                                   \
        .holding_reg_wr = holding_wr_##n}
static struct modbus_user_callbacks callbacks[] = {
    LISTIFY(CONFIG_MODBUS_NUMOF_RAW_ADU, CALLBACK_ROW, (, ), _)};

static int raw_response(int iface, const struct modbus_adu *response, void *context)
{
    mb_server_t *server = context;
    if (server->iface != iface || atomic_ptr_get(&server->owner) != k_current_get()) {
        return -EACCES;
    }
    *server->tx = *response;
    server->respond = true;
    return 0;
}

/* 以下是固定 Zephyr 版本的构建期扩展，不是官方公共接口。 */
int mb_support_raw_claim(struct modbus_context *ctx)
{
    mb_server_t *server = find_server(modbus_iface_get_by_ctx(ctx));
    if (!server) {
        return 0;
    }
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    return atomic_ptr_get(&server->owner) == k_current_get() ? 1 : -EACCES;
}

int mb_support_submit_work(struct k_work *work)
{
    struct modbus_context *ctx = CONTAINER_OF(work, struct modbus_context, server_work);
    mb_server_t *server = find_server(modbus_iface_get_by_ctx(ctx));
    if (!server) {
        return k_work_submit(work);
    }
    if (atomic_ptr_get(&server->owner) != k_current_get()) {
        return -EACCES;
    }
    work->handler(work);
    return 0;
}

bool mb_support_accept_request(struct modbus_context *ctx)
{
    mb_server_t *server = find_server(modbus_iface_get_by_ctx(ctx));
    if (!server) {
        return !ctx->rx_adu.unit_id || ctx->rx_adu.unit_id == ctx->unit_id;
    }
    uint8_t fc = ctx->rx_adu.fc;
    if (!ctx->rx_adu.unit_id) {
        return fc == 5 || fc == 6 || fc == 15 || fc == 16;
    }
    return ctx->rx_adu.unit_id == ctx->unit_id;
}

uint8_t mb_support_exception_code(struct modbus_context *ctx, uint8_t exception)
{
    mb_server_t *server = find_server(modbus_iface_get_by_ctx(ctx));
    return server && server->exception ? server->exception : exception;
}

int mb_server_open(uint8_t unit, mb_server_t **output)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!unit || unit > 247 || !output) {
        return -EINVAL;
    }
    k_mutex_lock(&registration_lock, K_FOREVER);
    int result = -ENOSPC;
    for (size_t i = 0; i < ARRAY_SIZE(servers); ++i) {
        char name[] = "RAW_0";
        name[4] += i;
        int iface = modbus_iface_get_by_name(name);
        if (servers[i].used || mb_support_check_available(iface)) {
            continue;
        }
        mb_server_t *server = &servers[i];
        server->iface = iface;
        const struct modbus_iface_param param = {
            .mode = MODBUS_MODE_RAW,
            .server = {.unit_id = unit, .user_cb = &callbacks[i]},
            .rawcb = {.raw_tx_cb = raw_response, .user_data = server},
        };
        result = modbus_init_server(iface, param);
        if (!result) {
            server->used = true;
            *output = server;
        }
        break;
    }
    k_mutex_unlock(&registration_lock);
    return result;
}

int mb_server_close(mb_server_t *server)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!server || !server->used) {
        return -EINVAL;
    }
    if (atomic_ptr_get(&server->owner)) {
        return -EBUSY;
    }
    k_mutex_lock(&registration_lock, K_FOREVER);
    int err = modbus_disable(server->iface);
    if (!err) {
        memset(server, 0, sizeof(*server));
    }
    k_mutex_unlock(&registration_lock);
    return err;
}

int mb_server_process(mb_server_t *server,
    const struct modbus_adu *request,
    struct modbus_adu *response,
    bool *respond,
    mb_access_t access,
    void *context)
{
    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }
    if (!server || !server->used || !request || !response || !respond || !access) {
        return -EINVAL;
    }
    if (!atomic_ptr_cas(&server->owner, NULL, k_current_get())) {
        return -EBUSY;
    }
    server->rx = request;
    server->tx = response;
    server->access = access;
    server->context = context;
    server->prepared = false;
    server->respond = false;
    server->exception = 0;
    int err = modbus_raw_submit_rx(server->iface, request);
    *respond = server->respond;
    server->rx = NULL;
    server->tx = NULL;
    server->access = NULL;
    server->context = NULL;
    atomic_ptr_clear(&server->owner);
    return err;
}
