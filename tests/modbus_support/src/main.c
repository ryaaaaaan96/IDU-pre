#include <modbus_support/rtu.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

static const uint8_t request[] = {1, 4, 0x1f, 0xd6, 0, 2, 0x97, 0xe7};

ZTEST(modbus_support, test_known_wire_frame_and_zeroed_transport_metadata)
{
    struct modbus_adu adu;
    memset(&adu, 0xa5, sizeof(adu));
    zassert_ok(mb_rtu_decode(request, sizeof(request), &adu));
    zassert_equal(adu.unit_id, 1);
    zassert_equal(adu.fc, 4);
    zassert_equal(adu.length, 4);
    zassert_equal(adu.crc, 0xe797);
    zassert_equal(adu.trans_id, 0);
    zassert_equal(adu.proto_id, 0);
    zassert_mem_equal(adu.data, request + 2, 4);
    uint8_t wire[sizeof(request)];
    size_t size = 0;
    zassert_ok(mb_rtu_encode(&adu, wire, sizeof(wire), &size));
    zassert_equal(size, sizeof(request));
    zassert_mem_equal(wire, request, sizeof(request));
}

ZTEST(modbus_support, test_rejects_invalid_input_without_changing_outputs)
{
    struct modbus_adu adu, before;
    memset(&adu, 0xa5, sizeof(adu));
    before = adu;
    uint8_t bad[sizeof(request)];
    memcpy(bad, request, sizeof(bad));
    bad[2] ^= 1;
    zassert_equal(mb_rtu_decode(bad, sizeof(bad), &adu), -EBADMSG);
    zassert_mem_equal(&adu, &before, sizeof(adu));
    zassert_equal(mb_rtu_decode(request, 3, &adu), -EMSGSIZE);
    zassert_equal(mb_rtu_decode(request, CONFIG_MODBUS_BUFFER_SIZE + 1, &adu), -EMSGSIZE);
    zassert_equal(mb_rtu_decode(NULL, sizeof(request), &adu), -EINVAL);
    zassert_equal(mb_rtu_decode(request, sizeof(request), NULL), -EINVAL);
    zassert_mem_equal(&adu, &before, sizeof(adu));

    zassert_ok(mb_rtu_decode(request, sizeof(request), &adu));
    memset(bad, 0x5a, sizeof(bad));
    uint8_t saved[sizeof(bad)];
    memcpy(saved, bad, sizeof(saved));
    size_t size = 123;
    zassert_equal(mb_rtu_encode(&adu, bad, sizeof(bad) - 1, &size), -EMSGSIZE);
    zassert_equal(size, 123);
    zassert_mem_equal(bad, saved, sizeof(bad));
    adu.length = sizeof(adu.data) + 1;
    zassert_equal(mb_rtu_encode(&adu, bad, sizeof(bad), &size), -EMSGSIZE);
    zassert_equal(mb_rtu_encode(NULL, bad, sizeof(bad), &size), -EINVAL);
    zassert_equal(size, 123);
    zassert_mem_equal(bad, saved, sizeof(bad));
}

ZTEST(modbus_support, test_full_capacity_and_exception_frame)
{
    struct modbus_adu adu = {.unit_id = 2, .fc = 65};
    adu.length = sizeof(adu.data);
    for (size_t i = 0; i < adu.length; ++i) {
        adu.data[i] = i;
    }
    uint8_t wire[CONFIG_MODBUS_BUFFER_SIZE];
    size_t size;
    zassert_ok(mb_rtu_encode(&adu, wire, sizeof(wire), &size));
    zassert_equal(size, sizeof(wire));
    zassert_ok(mb_rtu_validate(wire, size));
    memset(&adu, 0, sizeof(adu));
    zassert_ok(mb_rtu_decode(wire, size, &adu));
    zassert_equal(adu.length, sizeof(adu.data));
    for (size_t i = 0; i < adu.length; ++i) {
        zassert_equal(adu.data[i], (uint8_t)i);
    }

    adu.fc = 0x84;
    adu.length = 1;
    adu.data[0] = MODBUS_EXC_ILLEGAL_DATA_ADDR;
    zassert_ok(mb_rtu_encode(&adu, wire, sizeof(wire), &size));
    zassert_equal(size, 5);
    zassert_ok(mb_rtu_decode(wire, size, &adu));
    zassert_equal(adu.fc, 0x84);
    zassert_equal(adu.data[0], MODBUS_EXC_ILLEGAL_DATA_ADDR);
}

ZTEST_SUITE(modbus_support, NULL, NULL, NULL, NULL, NULL);
