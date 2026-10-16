#ifndef IDU_FORWARD_H
#define IDU_FORWARD_H
#include <idu/modbus_adapter.h>
struct idu_forward_segment {
    uint16_t begin, end;
    uint8_t function; /* zero: any function with a register-address PDU */
    idu_modbus_route_fn handler;
    void *user;
    uint16_t target_dev, target_begin; /* used when handler is NULL */
};
struct idu_forward_routes {
    const struct idu_forward_segment *segments;
    size_t count;
};
int idu_forward_attach(int iface, const struct idu_forward_routes *routes);
int idu_forward_dispatch(int iface, const struct modbus_adu *request,
                          struct modbus_adu *response, void *user);
int idu_forward_read(uint16_t port, uint8_t fc, uint16_t address, uint16_t *words, size_t count);
int idu_forward_write(uint16_t port, uint16_t address, const uint16_t *words, size_t count);
#endif
