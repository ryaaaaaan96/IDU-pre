#include <modbus_support/rtu_frame.h>
#include <modbus_support/rtu.h>
#include <errno.h>

int mb_rtu_probe(const uint8_t *data,
    size_t size,
    size_t boundary_size,
    bool at_boundary,
    bool broken,
    bool expired,
    size_t *frame_size)
{
    if (!data || !frame_size || boundary_size > size) {
        return -EINVAL;
    }
    if (!at_boundary || broken) {
        return -EBADMSG;
    }
    if (boundary_size) {
        if (mb_rtu_validate(data, boundary_size)) {
            return -EBADMSG;
        }
        *frame_size = boundary_size;
        return 0;
    }
    return expired ? -EBADMSG : -EAGAIN;
}
