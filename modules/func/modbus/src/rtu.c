#include <modbus_support/rtu.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <errno.h>
#include <string.h>

int mb_rtu_validate(const uint8_t *frame, size_t size)
{
    if (!frame) {
        return -EINVAL;
    }
    if (size < 4 || size - 4 > sizeof(((struct modbus_adu *)0)->data)) {
        return -EMSGSIZE;
    }
    uint16_t crc = sys_get_le16(frame + size - 2);
    if (crc16_ansi(frame, size - 2) != crc) {
        return -EBADMSG;
    }
    return 0;
}

int mb_rtu_decode(const uint8_t *frame, size_t size, struct modbus_adu *adu)
{
    if (!adu) {
        return -EINVAL;
    }
    int err = mb_rtu_validate(frame, size);
    if (err) {
        return err;
    }

    memset(adu, 0, sizeof(*adu));
    adu->unit_id = frame[0];
    adu->fc = frame[1];
    adu->length = size - 4;
    adu->crc = sys_get_le16(frame + size - 2);
    memcpy(adu->data, frame + 2, adu->length);
    return 0;
}

int mb_rtu_encode(const struct modbus_adu *adu, uint8_t *frame, size_t capacity, size_t *size)
{
    if (!adu || !frame || !size) {
        return -EINVAL;
    }
    if (adu->length > sizeof(adu->data) || capacity < adu->length + 4U) {
        return -EMSGSIZE;
    }
    frame[0] = adu->unit_id;
    frame[1] = adu->fc;
    memcpy(frame + 2, adu->data, adu->length);
    sys_put_le16(crc16_ansi(frame, adu->length + 2U), frame + adu->length + 2U);
    *size = adu->length + 4U;
    return 0;
}
