#include <transmit/transmit.h>
#include <dataprocess/dataprocess.h>
#include <modbus_support/rtu.h>
#include <modbus_support/rtu_frame.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

static uint8_t sent[256];
static size_t sent_size;
static unsigned sends, writes, reads, custom_checks, custom_runs;
static bool fail_send, fail_alloc;
static uint64_t now_us;
static bool block_first_read;
static K_SEM_DEFINE(read_started, 0, 1);
static K_SEM_DEFINE(read_continue, 0, 1);

void *__real_k_calloc(size_t count, size_t size);

void *__wrap_k_calloc(size_t count, size_t size)
{
    return fail_alloc ? NULL : __real_k_calloc(count, size);
}

static int send_frame(void *context, const uint8_t *data, size_t size, k_timeout_t timeout)
{
    ARG_UNUSED(context);
    zassert_true(size <= sizeof(sent));
    memcpy(sent, data, size);
    sent_size = size;
    ++sends;
    return fail_send ? -EIO : 0;
}

static int point_callback(const dp_callback_context_t *context)
{
    if (context->is_write) {
        ++writes;
        if (context->signal_index == 1 && context->value->u16 == 9999) {
            return -EIO;
        }
    } else {
        ++reads;
        if (block_first_read && context->device_id == 1) {
            block_first_read = false;
            k_sem_give(&read_started);
            zassert_ok(k_sem_take(&read_continue, K_SECONDS(1)));
        }
    }
    return 0;
}

static const dp_point_config_t rows[] = {
    {.signal_key = 100,
        .size = 2,
        .type = VALUE_TYPE_U16,
        .write_callback = point_callback,
        .read_callback = point_callback,
        .data.initial.u16 = 10},
    {.signal_key = 101, .size = 2, .type = VALUE_TYPE_U16, .write_callback = point_callback},
    {.signal_key = 102,
        .size = 2,
        .type = VALUE_TYPE_U16,
        .flags = DP_POINT_FLAG_CHECK_RANGE,
        .data = {.minimum.u16 = 0, .maximum.u16 = 10}},
    {.signal_key = 103, .size = 4, .type = VALUE_TYPE_U32, .data.initial.u32 = 0x11223344},
    {.signal_key = 104,
        .size = 4,
        .type = VALUE_TYPE_WORDS,
        .data.initial.words = {0xabcd, 0xef01}},
    {.signal_key = 105, .size = 1, .type = VALUE_TYPE_U8},
    {.signal_key = 106, .size = 2, .type = VALUE_TYPE_U16, .data.initial.u16 = 0x8001},
    {.signal_key = 107,
        .size = 4,
        .type = VALUE_TYPE_U32,
        .read_callback = point_callback,
        .data.initial.u32 = 0x12345678},
    {.signal_key = 108, .size = 2, .type = VALUE_TYPE_S16, .data.initial.s16 = -10},
};
static const dp_table_t table = {.points = rows, .point_count = ARRAY_SIZE(rows)};

static const value_scale_t percent_scale = {.numerator = 100, .denominator = 1};
static const value_scale_t divide_scale = {.numerator = 1, .denominator = 100};

static const tr_modbus_point_t points[] = {
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 100,
        .count = 1,
        .mapping = {
            .device_id = 1,
            .signal_index = 0,
            .external_type = VALUE_TYPE_U16,
        },
    },
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 101,
        .count = 1,
        .mapping = {
            .device_id = 1,
            .signal_index = 1,
            .external_type = VALUE_TYPE_U16,
        },
    },
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 102,
        .count = 1,
        .mapping = {
            .device_id = 1,
            .signal_index = 2,
            .external_type = VALUE_TYPE_U16,
        },
    },
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 200,
        .count = 2,
        .mapping = {
            .device_id = 1,
            .signal_index = 0,
            .external_type = VALUE_TYPE_U32,
            .scale = &percent_scale,
        },
    },
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 300,
        .count = 2,
        .word_order = MB_LOW_WORD_FIRST,
        .mapping = {
            .device_id = 1,
            .signal_index = 3,
            .external_type = VALUE_TYPE_U32,
        },
    },
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 400,
        .count = 2,
        .mapping = {
            .device_id = 1,
            .signal_index = 4,
            .external_type = VALUE_TYPE_WORDS,
        },
    },
    {
        .area = MB_DATA_AREA_INPUT_REGISTER,
        .address = 500,
        .count = 1,
        .mapping = {
            .device_id = 1,
            .signal_index = 0,
            .external_type = VALUE_TYPE_U16,
        },
    },
    {
        .area = MB_DATA_AREA_COIL,
        .address = 10,
        .count = 1,
        .mapping = {
            .device_id = 1,
            .signal_index = 5,
            .external_type = VALUE_TYPE_U8,
        },
    },
    {
        .area = MB_DATA_AREA_DISCRETE_INPUT,
        .address = 20,
        .count = 16,
        .mapping = {
            .device_id = 1,
            .signal_index = 6,
            .external_type = VALUE_TYPE_U16,
        },
    },
    {
        .area = MB_DATA_AREA_INPUT_REGISTER,
        .address = 600,
        .count = 2,
        .mapping = {
            .device_id = 1,
            .signal_index = 7,
            .external_type = VALUE_TYPE_U32,
        },
    },
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 65535,
        .count = 1,
        .mapping = {
            .device_id = 1,
            .signal_index = 0,
            .external_type = VALUE_TYPE_U16,
        },
    },
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 700,
        .count = 1,
        .mapping = {
            .device_id = 1,
            .signal_index = 8,
            .external_type = VALUE_TYPE_S16,
        },
    },
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 701,
        .count = 2,
        .mapping = {
            .device_id = 1,
            .signal_index = 8,
            .external_type = VALUE_TYPE_S32,
        },
    },
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 800,
        .count = 1,
        .mapping = {
            .device_id = 1,
            .signal_index = 7,
            .external_type = VALUE_TYPE_U16,
            .scale = &divide_scale,
        },
    },
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 801,
        .count = 1,
        .mapping = {
            .device_id = 1,
            .signal_index = 7,
            .external_type = VALUE_TYPE_S16,
            .scale = &divide_scale,
        },
    },
};

static tr_match_t custom_check(void *context, const tr_view_t *view, size_t *consumed)
{
    ARG_UNUSED(context);
    ++custom_checks;
    if (view->data[0] != 0xaa) {
        return TR_MISMATCH;
    }
    if (view->size < 5) {
        return TR_WAIT;
    }
    if ((uint8_t)(view->data[0] ^ view->data[1] ^ view->data[2] ^ view->data[3]) != view->data[4]) {
        return TR_MISMATCH;
    }
    *consumed = 5;
    return TR_COMPLETE;
}

static int custom_execute(void *context,
    const uint8_t *request,
    size_t size,
    uint8_t *response,
    size_t capacity,
    size_t *response_size)
{
    ARG_UNUSED(context);
    zassert_true(capacity >= size);
    memcpy(response, request, size);
    *response_size = size;
    ++custom_runs;
    return 0;
}

static unsigned route_calls;
static int route_result;
static size_t route_length;

static int raw_access(void *context,
    const struct modbus_adu *request,
    struct modbus_adu *response)
{
    zassert_equal(context, (void *)0x1234);
    ++route_calls;
    route_length = request->length;
    memcpy(response->data, request->data, 4);
    response->length = 4;
    return route_result;
}

static const tr_modbus_route_t routes[] = {
    {.address = 20049, .count = 56, .callback = raw_access, .context = (void *)0x1234},
};

static vport_t normal_port = {.send = send_frame};
static vport_t mixed_port = {.send = send_frame};
static vport_t second_port = {.send = send_frame};
static const tr_protocol_t protocols[] = {
    {.type = TR_PROTOCOL_MODBUS,
        .max_frame = 1100,
        .timeout_us = 200000,
        .modbus = {.unit = 1,
            .gap_us = 1750,
            .points = points,
            .point_count = ARRAY_SIZE(points),
            .routes = routes,
            .route_count = ARRAY_SIZE(routes)}},
    {.type = TR_PROTOCOL_CUSTOM,
        .max_frame = 16,
        .timeout_us = 5000,
        .custom = {.check = custom_check, .execute = custom_execute}},
};
static const tr_modbus_point_t second_points[] = {
    {
        .area = MB_DATA_AREA_HOLDING_REGISTER,
        .address = 100,
        .count = 1,
        .mapping = {
            .device_id = 2,
            .signal_index = 0,
            .external_type = VALUE_TYPE_U16,
        },
    },
};
static const tr_protocol_t second_protocol = {.type = TR_PROTOCOL_MODBUS,
    .max_frame = 256,
    .timeout_us = 5000,
    .modbus = {.unit = 2,
        .gap_us = 1750,
        .points = second_points,
        .point_count = ARRAY_SIZE(second_points)}};
static const tr_config_t configs[] = {
    {.port = &normal_port,
        .protocols = protocols,
        .protocol_count = 1,
        .rx_capacity = 1100,
        .tx_capacity = 256,
        .send_timeout_ms = 100},
    {.port = &mixed_port,
        .protocols = protocols,
        .protocol_count = 2,
        .rx_capacity = 1100,
        .tx_capacity = 256,
        .send_timeout_ms = 100},
    {.port = &second_port,
        .protocols = &second_protocol,
        .protocol_count = 1,
        .rx_capacity = 1100,
        .tx_capacity = 256,
        .send_timeout_ms = 100},
};

static void *setup(void)
{
    zassert_ok(dp_init());
    zassert_ok(dp_register_device(1, &table));
    zassert_ok(dp_register_device(2, &table));
    for (size_t i = 0; i < ARRAY_SIZE(configs); ++i) {
        zassert_ok(tr_register(&configs[i]));
    }
    return NULL;
}

static int event(
    vport_t *port, vport_event_type_t type, const uint8_t *data, size_t size, uint64_t elapsed)
{
    now_us += elapsed;
    const vport_event_t input = {.type = type, .data = data, .size = size, .at_us = now_us};
    return vport_input(port, &input);
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    fail_send = false;
    fail_alloc = false;
    zassert_ok(dp_restore_defaults(1));
    zassert_ok(dp_restore_defaults(2));
    block_first_read = false;
    k_sem_reset(&read_started);
    k_sem_reset(&read_continue);
    writes = reads = sends = custom_checks = custom_runs = 0;
    sent_size = 0;
    route_calls = 0;
    route_result = 0;
    for (size_t i = 0; i < ARRAY_SIZE(configs); ++i) {
        zassert_equal(event(configs[i].port, VPORT_ERROR, NULL, 0, 10000), -EIO);
        zassert_ok(event(configs[i].port, VPORT_IDLE, NULL, 0, 2000));
    }
}

static size_t encode(uint8_t unit, uint8_t fc, const uint8_t *payload, size_t size, uint8_t *out)
{
    struct modbus_adu adu = {.unit_id = unit, .fc = fc, .length = size};
    memcpy(adu.data, payload, size);
    size_t length;
    zassert_ok(mb_rtu_encode(&adu, out, 256, &length));
    return length;
}

static void request(uint8_t unit, uint8_t fc, const uint8_t *payload, size_t size)
{
    uint8_t frame[256];
    size_t length = encode(unit, fc, payload, size, frame);
    zassert_ok(event(&normal_port, VPORT_DATA, frame, length, 10000));
    int err = event(&normal_port, VPORT_POLL, NULL, 0, 2000);
    zassert_equal(err, fail_send ? -EIO : 0);
}

static struct modbus_adu response(void)
{
    struct modbus_adu adu;
    zassert_ok(mb_rtu_decode(sent, sent_size, &adu));
    return adu;
}

static void exception(uint8_t fc, uint8_t code)
{
    struct modbus_adu adu = response();
    zassert_equal(adu.fc, fc | 0x80);
    zassert_equal(adu.length, 1);
    zassert_equal(adu.data[0], code);
}

ZTEST(transmit_stream, test_register_reads_and_word_order)
{
    request(1, 3, (uint8_t[]){1, 44, 0, 2}, 4);
    struct modbus_adu adu = response();
    zassert_mem_equal(adu.data, ((uint8_t[]){4, 0x33, 0x44, 0x11, 0x22}), 5);
    request(1, 4, (uint8_t[]){2, 89, 0, 1}, 4); /* 只读 U32 的低字。 */
    adu = response();
    zassert_mem_equal(adu.data, ((uint8_t[]){2, 0x56, 0x78}), 3);
    zassert_equal(reads, 1);
    request(1, 3, (uint8_t[]){1, 144, 0, 2}, 4);
    adu = response();
    zassert_mem_equal(adu.data, ((uint8_t[]){4, 0xab, 0xcd, 0xef, 1}), 5);
    request(1, 4, (uint8_t[]){1, 244, 0, 1}, 4);
    adu = response();
    zassert_equal(sys_get_be16(adu.data + 1), 10);
}

ZTEST(transmit_stream, test_writes_scaling_and_partial_rejection)
{
    request(1, 16, (uint8_t[]){0, 200, 0, 2, 4, 0, 1, 0x11, 0x70}, 9);
    value_t value;
    zassert_ok(dp_get_para_by_index(1, 0, VALUE_TYPE_U16, &value));
    zassert_equal(value.u16, 700); /* 70000 先除 100，再送入 U16 DP。 */
    request(1, 3, (uint8_t[]){0, 200, 0, 2}, 4);
    struct modbus_adu adu = response();
    zassert_equal(sys_get_be32(adu.data + 1), 70000);
    unsigned previous = writes;
    request(1, 6, (uint8_t[]){0, 201, 0, 1}, 4);
    exception(6, 2);
    zassert_equal(writes, previous);
    request(1, 16, (uint8_t[]){1, 44, 0, 2, 4, 0xab, 0xcd, 0xef, 1}, 9);
    zassert_ok(dp_get_para_by_index(1, 3, VALUE_TYPE_U32, &value));
    zassert_equal(value.u32, 0xef01abcd);
}

ZTEST(transmit_stream, test_preflight_and_nontransactional_failure)
{
    request(1, 16, (uint8_t[]){0, 101, 0, 3, 6, 0, 1, 0, 2, 0, 3}, 11);
    exception(16, 2);
    zassert_equal(writes, 0);
    request(1, 16, (uint8_t[]){0, 100, 0, 2, 4, 0, 55, 0x27, 0x0f}, 9);
    exception(16, 4);
    value_t value;
    zassert_ok(dp_get_para_by_index(1, 0, VALUE_TYPE_U16, &value));
    zassert_equal(value.u16, 55);
    zassert_ok(dp_get_para_by_index(1, 1, VALUE_TYPE_U16, &value));
    zassert_equal(value.u16, 0);
    request(1, 6, (uint8_t[]){0, 102, 0, 11}, 4);
    exception(6, 3);
    request(1, 3, (uint8_t[]){0xff, 0xff, 0, 2}, 4);
    exception(3, 2);
    request(1, 3, (uint8_t[]){0, 100, 0, 0}, 4);
    exception(3, 3);
    request(1, 99, (uint8_t[]){0, 100, 0, 1}, 4);
    exception(99, 1);
}

ZTEST(transmit_stream, test_bits_broadcast_and_other_unit)
{
    request(1, 5, (uint8_t[]){0, 10, 0xff, 0}, 4);
    request(1, 1, (uint8_t[]){0, 10, 0, 1}, 4);
    struct modbus_adu adu = response();
    zassert_equal(adu.data[1], 1);
    request(1, 15, (uint8_t[]){0, 10, 0, 1, 1, 0}, 6);
    value_t value;
    zassert_ok(dp_get_para_by_index(1, 5, VALUE_TYPE_U8, &value));
    zassert_equal(value.u8, 0);
    request(1, 2, (uint8_t[]){0, 20, 0, 16}, 4);
    adu = response();
    zassert_mem_equal(adu.data, ((uint8_t[]){2, 1, 0x80}), 3);
    /* 写功能码不能访问仅登记在输入区的地址。 */
    request(1, 5, (uint8_t[]){0, 20, 0xff, 0}, 4);
    exception(5, 2);
    request(1, 6, (uint8_t[]){1, 244, 0, 88}, 4);
    exception(6, 2);
    zassert_ok(dp_get_para_by_index(1, 6, VALUE_TYPE_U16, &value));
    zassert_equal(value.u16, 0x8001);
    unsigned previous = sends;
    request(2, 6, (uint8_t[]){0, 100, 0, 88}, 4);
    zassert_equal(sends, previous);
    request(0, 6, (uint8_t[]){0, 100, 0, 88}, 4);
    zassert_equal(sends, previous);
    zassert_ok(dp_get_para_by_index(1, 0, VALUE_TYPE_U16, &value));
    zassert_equal(value.u16, 88);
    request(0, 3, (uint8_t[]){0, 100, 0, 1}, 4);
    zassert_equal(sends, previous);
}

ZTEST(transmit_stream, test_send_failure_does_not_replay)
{
    fail_send = true;
    request(1, 6, (uint8_t[]){0, 100, 0, 42}, 4);
    zassert_equal(writes, 1);
    zassert_equal(sends, 1);
    fail_send = false;
    zassert_ok(event(&normal_port, VPORT_POLL, NULL, 0, 10000));
    zassert_equal(writes, 1);
    zassert_equal(sends, 1);
}

ZTEST(transmit_stream, test_byte_input_crc_and_receive_damage)
{
    uint8_t frame[256];
    size_t size = encode(1, 3, (uint8_t[]){0, 100, 0, 1}, 4, frame);
    for (size_t i = 0; i < size; ++i) {
        zassert_ok(event(&normal_port, VPORT_DATA, frame + i, 1, i ? 100 : 10000));
    }
    zassert_equal(sends, 0);
    zassert_ok(event(&normal_port, VPORT_IDLE, NULL, 0, 2000));
    zassert_equal(sends, 1);
    frame[size - 1] ^= 1;
    zassert_ok(event(&normal_port, VPORT_DATA, frame, size, 10000));
    zassert_ok(event(&normal_port, VPORT_IDLE, NULL, 0, 2000));
    zassert_equal(sends, 1);
    uint8_t oversized[1101] = {0};
    zassert_equal(event(&normal_port, VPORT_DATA, oversized, sizeof(oversized), 10000), -ENOBUFS);
    frame[size - 1] ^= 1;
    zassert_equal(event(&normal_port, VPORT_DATA, frame, size, 100), -EIO);
    zassert_ok(event(&normal_port, VPORT_DATA, frame, size, 10000));
    zassert_ok(event(&normal_port, VPORT_POLL, NULL, 0, 2000));
    zassert_equal(sends, 2);
}

ZTEST(transmit_stream, test_protocol_lock_rotation_and_resync)
{
    uint8_t frame[] = {0xaa, 5, 42, 55, 0xaa ^ 5 ^ 42 ^ 55};
    zassert_ok(event(&mixed_port, VPORT_DATA, frame, 2, 10000));
    /* 首次 Modbus 未确定仍锁定，不提前调用后续协议。 */
    zassert_equal(custom_checks, 0);
    zassert_ok(event(&mixed_port, VPORT_IDLE, NULL, 0, 2000));
    zassert_true(custom_checks > 0);
    zassert_equal(custom_runs, 0);
    zassert_ok(event(&mixed_port, VPORT_DATA, frame + 2, 3, 100));
    zassert_equal(custom_runs, 1);
    zassert_ok(event(&mixed_port, VPORT_DATA, frame, sizeof(frame), 10000));
    zassert_equal(custom_runs, 2); /* 成功后继续锁定，无须等 RTU 边界。 */
    uint8_t modbus[256];
    size_t size = encode(1, 3, (uint8_t[]){0, 100, 0, 1}, 4, modbus);
    zassert_ok(event(&mixed_port, VPORT_DATA, modbus, size, 10000));
    zassert_ok(event(&mixed_port, VPORT_IDLE, NULL, 0, 2000));
    zassert_equal(sends, 3);
    uint8_t noise[] = {0, 0xaa, 5, 42, 55, 0xaa ^ 5 ^ 42 ^ 55};
    zassert_ok(event(&mixed_port, VPORT_DATA, noise, sizeof(noise), 10000));
    zassert_ok(event(&mixed_port, VPORT_IDLE, NULL, 0, 2000));
    zassert_equal(custom_runs, 3);
}

ZTEST(transmit_stream, test_no_new_byte_timeout_and_instance_isolation)
{
    uint8_t partial[] = {0xaa, 5};
    zassert_ok(event(&mixed_port, VPORT_DATA, partial, sizeof(partial), 10000));
    zassert_ok(event(&mixed_port, VPORT_POLL, NULL, 0, 6000));
    zassert_equal(custom_runs, 0);
    uint8_t tail[] = {42, 55, 0xaa ^ 5 ^ 42 ^ 55};
    zassert_ok(event(&mixed_port, VPORT_DATA, tail, sizeof(tail), 100));
    zassert_equal(custom_runs, 0); /* 不能在轮转后重新计时，接上已过期的旧前缀。 */
    uint8_t frame[256];
    size_t size = encode(2, 3, (uint8_t[]){0, 100, 0, 1}, 4, frame);
    zassert_ok(event(&normal_port, VPORT_DATA, frame, size, 10000));
    zassert_ok(event(&second_port, VPORT_DATA, frame, size, 0));
    zassert_ok(event(&normal_port, VPORT_POLL, NULL, 0, 2000));
    zassert_equal(sends, 0);
    zassert_ok(event(&second_port, VPORT_POLL, NULL, 0, 0));
    zassert_equal(sends, 1);
}

ZTEST(transmit_stream, test_registration_and_port_validation)
{
    zassert_equal(tr_register(&configs[0]), -EALREADY);
    vport_t unbound = {.send = send_frame};
    tr_config_t config = configs[0];
    config.port = &unbound;
    tr_protocol_t invalid = protocols[0];
    config.protocols = &invalid;
    tr_modbus_point_t overlap[2] = {points[0], points[0]};
    invalid.modbus.points = overlap;
    invalid.modbus.point_count = 2;
    zassert_equal(tr_register(&config), -EEXIST);
    zassert_is_null(unbound.receive);
    invalid = protocols[0];
    config.tx_capacity = 255;
    zassert_equal(tr_register(&config), -EINVAL);
    config.tx_capacity = 256;
    fail_alloc = true;
    zassert_equal(tr_register(&config), -ENOMEM);
    fail_alloc = false;
    zassert_is_null(unbound.receive);
    zassert_equal(vport_input(&unbound, &(vport_event_t){.type = VPORT_DATA}), -EINVAL);
    zassert_equal(vport_input(&unbound, &(vport_event_t){.type = VPORT_POLL}), -ENODEV);
    zassert_equal(
        vport_input(&normal_port, &(vport_event_t){.type = VPORT_POLL, .at_us = 0}), -EINVAL);
}

ZTEST(transmit_stream, test_common_scale_rounding_and_overflow)
{
    int64_t output = 77;
    zassert_ok(value_scale_apply(NULL, false, -123, &output));
    zassert_equal(output, -123);
    const value_scale_t fractional = {.numerator = 3, .denominator = 2, .offset = 5};
    zassert_ok(value_scale_apply(&fractional, false, 7, &output));
    zassert_equal(output, 15);
    zassert_ok(value_scale_apply(&fractional, true, 15, &output));
    zassert_equal(output, 6);
    zassert_ok(value_scale_apply(&fractional, false, -7, &output));
    zassert_equal(output, -5);
    const value_scale_t twice = {.numerator = 2, .denominator = 1};
    output = 77;
    zassert_equal(value_scale_apply(&twice, false, INT64_MAX, &output), -ERANGE);
    zassert_equal(output, 77);
    const value_scale_t negative = {.numerator = 1, .denominator = -1};
    zassert_equal(value_scale_apply(&negative, false, INT64_MIN, &output), -ERANGE);
    const value_scale_t offset = {.numerator = 1, .denominator = 1, .offset = 1};
    zassert_equal(value_scale_apply(&offset, false, INT64_MAX, &output), -ERANGE);
    zassert_equal(value_scale_apply(&offset, true, INT64_MIN, &output), -ERANGE);
    zassert_equal(value_scale_apply(&(value_scale_t){0}, false, 1, &output), -EINVAL);
    zassert_equal(value_scale_apply(&offset, false, 1, NULL), -EINVAL);
    zassert_equal(output, 77);
}

ZTEST(transmit_stream, test_intercharacter_gap_and_no_false_rtu_resync)
{
    uint8_t frame[256];
    size_t size = encode(1, 3, (uint8_t[]){0, 100, 0, 1}, 4, frame);
    zassert_ok(event(&normal_port, VPORT_DATA, frame, 3, 10000));
    zassert_ok(event(&normal_port, VPORT_DATA, frame + 3, size - 3, 1000));
    zassert_ok(event(&normal_port, VPORT_IDLE, NULL, 0, 2000));
    zassert_equal(sends, 0); /* 帧内间隔超过 1.5 字符限制，不能响应。 */
    uint8_t noise[256] = {0x99};
    memcpy(noise + 1, frame, size);
    zassert_ok(event(&normal_port, VPORT_DATA, noise, size + 1, 10000));
    zassert_ok(event(&normal_port, VPORT_IDLE, NULL, 0, 2000));
    zassert_equal(sends, 0); /* 丢弃噪声不能伪造 RTU 起始边界。 */
    zassert_ok(event(&normal_port, VPORT_DATA, frame, size, 10000));
    zassert_ok(event(&normal_port, VPORT_IDLE, NULL, 0, 2000));
    zassert_equal(sends, 1);
}

ZTEST(transmit_stream, test_batched_frames_and_retained_boundaries)
{
    /* 先锁定自定义协议，让它等待；缓存期间记录 RTU 的真实分隔。 */
    uint8_t custom[] = {0xaa, 5, 42, 55, 0xaa ^ 5 ^ 42 ^ 55};
    zassert_ok(event(&mixed_port, VPORT_DATA, custom, sizeof(custom), 10000));
    zassert_ok(event(&mixed_port, VPORT_IDLE, NULL, 0, 2000));
    zassert_equal(custom_runs, 1);
    uint8_t frames[10];
    memcpy(frames, custom, 5);
    memcpy(frames + 5, custom, 5);
    zassert_ok(event(&mixed_port, VPORT_DATA, frames, sizeof(frames), 10000));
    zassert_equal(custom_runs, 3);
    zassert_equal(sends, 3);
    uint8_t broken[] = {0x11, 0x22, 0x33};
    zassert_ok(event(&mixed_port, VPORT_DATA, broken, sizeof(broken), 10000));
    uint8_t frame[256];
    size_t size = encode(1, 3, (uint8_t[]){0, 100, 0, 1}, 4, frame);
    zassert_ok(event(&mixed_port, VPORT_DATA, frame, size, 10000));
    zassert_equal(sends, 3);
    zassert_ok(event(&mixed_port, VPORT_POLL, NULL, 0, 2000));
    zassert_equal(sends, 4); /* 丢弃旧坏帧后保留新 RTU 帧的真实起点。 */
}

ZTEST(transmit_stream, test_noise_stress_and_recovery)
{
    uint32_t random = 0x13572468;
    for (size_t iteration = 0; iteration < 1000; ++iteration) {
        uint8_t bytes[31];
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        size_t size = random % sizeof(bytes) + 1;
        for (size_t i = 0; i < size; ++i) {
            random = random * 1664525U + 1013904223U;
            bytes[i] = (uint8_t)(random >> 24);
        }
        int err = event(&mixed_port, VPORT_DATA, bytes, size, random % 3000);
        zassert_true(err == 0 || err == -ENOBUFS || err == -EIO);
        if (!(iteration % 3)) {
            zassert_ok(event(&mixed_port, VPORT_POLL, NULL, 0, 6000));
        }
    }
    zassert_equal(event(&mixed_port, VPORT_ERROR, NULL, 0, 10000), -EIO);
    zassert_ok(event(&mixed_port, VPORT_IDLE, NULL, 0, 2000));
    unsigned previous = sends;
    uint8_t frame[256];
    size_t size = encode(1, 3, (uint8_t[]){0, 100, 0, 1}, 4, frame);
    zassert_ok(event(&mixed_port, VPORT_DATA, frame, size, 10000));
    zassert_ok(event(&mixed_port, VPORT_IDLE, NULL, 0, 2000));
    zassert_equal(sends, previous + 1);
}

ZTEST(transmit_stream, test_read_converts_before_scaling)
{
    value_t value = {.u32 = 100000};
    zassert_ok(dp_set_para_by_index(1, 7, VALUE_TYPE_U32, &value));

    /* U32 100000 先转 U16 得到 34464，再除以 100 得到 344。 */
    request(1, 3, (uint8_t[]){3, 32, 0, 1}, 4);
    struct modbus_adu adu = response();
    zassert_equal(adu.fc, 3);
    zassert_equal(sys_get_be16(adu.data + 1), 344);

    /* 同一原值先转 S16 得到 -31072，再向零除以 100 得到 -310。 */
    request(1, 3, (uint8_t[]){3, 33, 0, 1}, 4);
    adu = response();
    zassert_equal(adu.fc, 3);
    zassert_equal(sys_get_be16(adu.data + 1), (uint16_t)-310);

    zassert_ok(dp_get_para_by_index(1, 7, VALUE_TYPE_U32, &value));
    zassert_equal(value.u32, 100000);
}

ZTEST(transmit_stream, test_write_scales_in_external_type)
{
    /* U16 1000 逆放缩为 100000，先截断为 34464，再交给 U32 的 DP 点。 */
    request(1, 6, (uint8_t[]){3, 32, 3, 232}, 4);
    struct modbus_adu adu = response();
    zassert_equal(adu.fc, 6);
    value_t value;
    zassert_ok(dp_get_para_by_index(1, 7, VALUE_TYPE_U32, &value));
    zassert_equal(value.u32, 34464);

    /* S16 -1000 逆放缩为 -100000，按 S16 表示为 31072 后提交。 */
    request(1, 6, (uint8_t[]){3, 33, 0xfc, 0x18}, 4);
    adu = response();
    zassert_equal(adu.fc, 6);
    zassert_ok(dp_get_para_by_index(1, 7, VALUE_TYPE_U32, &value));
    zassert_equal(value.u32, 31072);
}

ZTEST(transmit_stream, test_signed_wire_and_dp_narrowing)
{
    request(1, 3, (uint8_t[]){2, 189, 0, 2}, 4);
    struct modbus_adu adu = response();
    zassert_equal(sys_get_be32(adu.data + 1), (uint32_t)-10);
    uint8_t payload[] = {2, 189, 0, 2, 4, 0, 0, 0, 0};
    sys_put_be32((uint32_t)-70000, payload + 5);
    request(1, 16, payload, sizeof(payload));
    value_t value;
    zassert_ok(dp_get_para_by_index(1, 8, VALUE_TYPE_S16, &value));
    zassert_equal(value.s16, -4464);
    request(1, 3, (uint8_t[]){2, 188, 0, 1}, 4);
    adu = response();
    zassert_equal(sys_get_be16(adu.data + 1), (uint16_t)-4464);
}

ZTEST(transmit_stream, test_no_request_time_allocation)
{
    fail_alloc = true;
    request(1, 6, (uint8_t[]){0, 100, 0, 42}, 4);
    request(1, 3, (uint8_t[]){0, 100, 0, 1}, 4);
    fail_alloc = false;
    struct modbus_adu adu = response();
    zassert_equal(sys_get_be16(adu.data + 1), 42);
}

typedef struct {
    vport_t *port;
    uint8_t unit;
    uint64_t at_us;
    uint16_t value;
} parallel_request_t;

static K_THREAD_STACK_DEFINE(first_stack, 4096);
static K_THREAD_STACK_DEFINE(second_stack, 4096);
static struct k_thread first_thread, second_thread;

static void parallel_request(void *argument, void *b, void *c)
{
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    parallel_request_t *request = argument;
    uint8_t frame[256];
    size_t size = encode(request->unit, 3, (uint8_t[]){0, 100, 0, 1}, 4, frame);
    vport_event_t input = {
        .type = VPORT_DATA, .data = frame, .size = size, .at_us = request->at_us};
    zassert_ok(vport_input(request->port, &input));
    input = (vport_event_t){.type = VPORT_POLL, .at_us = request->at_us + 2000};
    zassert_ok(vport_input(request->port, &input));
    struct modbus_adu adu = response();
    zassert_equal(adu.unit_id, request->unit);
    request->value = sys_get_be16(adu.data + 1);
}

ZTEST(transmit_stream, test_parallel_raw_interfaces_do_not_share_context)
{
    value_t value = {.u16 = 33};
    zassert_ok(dp_set_para_by_index(2, 0, VALUE_TYPE_U16, &value));
    now_us += 10000;
    parallel_request_t first = {.port = &normal_port, .unit = 1, .at_us = now_us};
    parallel_request_t second = {.port = &second_port, .unit = 2, .at_us = now_us};
    now_us += 2000;
    block_first_read = true;
    k_thread_create(&first_thread,
        first_stack,
        K_THREAD_STACK_SIZEOF(first_stack),
        parallel_request,
        &first,
        NULL,
        NULL,
        5,
        0,
        K_NO_WAIT);
    zassert_ok(k_sem_take(&read_started, K_SECONDS(1)));
    const struct modbus_adu foreign = {.unit_id = 1, .fc = 6, .length = 4, .data = {0, 100, 0, 99}};
    zassert_equal(modbus_raw_submit_rx(modbus_iface_get_by_name("RAW_0"), &foreign), -EACCES);
    k_thread_create(&second_thread,
        second_stack,
        K_THREAD_STACK_SIZEOF(second_stack),
        parallel_request,
        &second,
        NULL,
        NULL,
        5,
        0,
        K_NO_WAIT);
    zassert_ok(k_thread_join(&second_thread, K_SECONDS(1)));
    zassert_equal(second.value, 33); /* 第一实例仍在业务回调中等待，第二实例必须能够完成。 */
    k_sem_give(&read_continue);
    zassert_ok(k_thread_join(&first_thread, K_SECONDS(1)));
    zassert_equal(first.value, 10);
}

ZTEST(transmit_stream, test_address_route_filtering_and_terminal_result)
{
    request(1, 6, (uint8_t[]){0x4e, 0x51, 0, 1}, 4);
    zassert_equal(route_calls, 1);
    zassert_equal(response().fc, 6);
    unsigned previous = sends;
    request(2, 6, (uint8_t[]){0x4e, 0x51, 0, 1}, 4);
    zassert_equal(route_calls, 1);
    request(0, 6, (uint8_t[]){0x4e, 0x51, 0, 1}, 4);
    zassert_equal(route_calls, 2);
    zassert_equal(sends, previous);
    route_result = TR_MODBUS_ROUTE_SILENT;
    request(1, 6, (uint8_t[]){0x4e, 0x88, 0, 1}, 4);
    zassert_equal(route_calls, 3); /* 最后一个地址 20104。 */
    zassert_equal(sends, previous);
    request(1, 6, (uint8_t[]){0x4e, 0x89, 0, 1}, 4);
    exception(6, MODBUS_EXC_ILLEGAL_DATA_ADDR);
    zassert_equal(route_calls, 3);
    route_result = -ENOENT;
    uint8_t frame[256];
    size_t length = encode(1, 6, (uint8_t[]){0x4e, 0x51, 0, 1}, 4, frame);
    previous = sends;
    zassert_ok(event(&normal_port, VPORT_DATA, frame, length, 10000));
    zassert_equal(event(&normal_port, VPORT_POLL, NULL, 0, 2000), -ENOENT);
    zassert_equal(sends, previous); /* 回调失败不进入标准点表。 */
    zassert_equal(writes, 0);
}

ZTEST(transmit_stream, test_address_route_legacy_long_fc16_and_crc)
{
    struct modbus_adu adu = {.unit_id = 1, .fc = 16, .length = 1051};
    sys_put_be16(20049, adu.data);
    sys_put_be16(523, adu.data + 2);
    adu.data[4] = (uint8_t)1046; /* 旧协议字节计数截断，不能先走标准校验。 */
    uint8_t frame[1100];
    size_t size;
    zassert_ok(mb_rtu_encode(&adu, frame, sizeof(frame), &size));
    for (size_t i = 0; i < size; ++i) {
        zassert_ok(event(&normal_port, VPORT_DATA, frame + i, 1, i ? 87 : 10000));
    }
    zassert_ok(event(&normal_port, VPORT_POLL, NULL, 0, 2000));
    zassert_equal(route_calls, 1);
    zassert_equal(route_length, 1051);
    zassert_equal(response().fc, 16);
    frame[size - 1] ^= 1;
    zassert_ok(event(&normal_port, VPORT_DATA, frame, size, 10000));
    zassert_ok(event(&normal_port, VPORT_POLL, NULL, 0, 2000));
    zassert_equal(route_calls, 1);
    /* 超长标准点请求仍由官方服务端拒绝，不能借大缓冲放宽标准限制。 */
    sys_put_be16(100, adu.data);
    zassert_ok(mb_rtu_encode(&adu, frame, sizeof(frame), &size));
    zassert_ok(event(&normal_port, VPORT_DATA, frame, size, 10000));
    zassert_ok(event(&normal_port, VPORT_POLL, NULL, 0, 2000));
    exception(16, MODBUS_EXC_ILLEGAL_DATA_VAL);
    zassert_equal(writes, 0);
}

ZTEST(transmit_stream, test_address_route_registration_validation)
{
    vport_t port = {.send = send_frame};
    tr_config_t config = configs[0];
    tr_protocol_t protocol = protocols[0];
    config.port = &port;
    config.protocols = &protocol;
    tr_modbus_route_t invalid[2] = {routes[0], routes[0]};
    protocol.modbus.routes = invalid;
    protocol.modbus.route_count = 2;
    zassert_equal(tr_register(&config), -EEXIST);
    protocol.modbus.route_count = 1;
    invalid[0].address = 100;
    zassert_equal(tr_register(&config), -EEXIST);
    invalid[0].address = 65535;
    invalid[0].count = 2;
    zassert_equal(tr_register(&config), -EINVAL);
    invalid[0] = routes[0];
    invalid[0].callback = NULL;
    zassert_equal(tr_register(&config), -EINVAL);
    zassert_is_null(port.receive);
}

ZTEST_SUITE(transmit_stream, NULL, setup, before, NULL, NULL);
