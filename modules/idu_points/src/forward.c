#include <idu/points.h>

static int input_read(uint16_t address, uint16_t *value)
{
    return idu_points_read_register(4, address, value);
}

static int holding_read(uint16_t address, uint16_t *value)
{
    return idu_points_read_register(3, address, value);
}

struct modbus_user_callbacks *idu_forward_callbacks(void)
{
    static struct modbus_user_callbacks callbacks = {
        .input_reg_rd = input_read,
        .holding_reg_rd = holding_read,
    };
    return &callbacks;
}

#if defined(CONFIG_IDU_PARAMS)
#include <idu/forward.h>
#include <idu/process.h>
#include <idu/collect.h>
#include <errno.h>
#include <zephyr/sys/byteorder.h>

static const struct idu_transmit_cfg *mapping(uint8_t fc, uint16_t address)
{
    const struct idu_equip_cfg *cfg = idu_points_config();
    if (!cfg) { return NULL; }
    for (size_t i = 0; i < cfg->transmit_count; ++i) {
        const struct idu_transmit_cfg *m = &cfg->transmit[i];
        if (m->function == fc && address >= m->address &&
            (uint32_t)address < (uint32_t)m->address + m->count) { return m; }
    }
    return NULL;
}

int idu_forward_read(uint16_t port, uint8_t fc, uint16_t address, uint16_t *words, size_t count)
{
    if (!words || !count || count > 125 || (fc != 3 && fc != 4) ||
        (uint32_t)address + count > 65536) { return -EINVAL; }
    const struct idu_equip_cfg *cfg = idu_points_config();
    if (cfg && cfg->exact_overlapping_reads) {
        bool ambiguous = false;
        const struct idu_transmit_cfg *exact = NULL;
        for (size_t i = 0; i < cfg->transmit_count; ++i) {
            const struct idu_transmit_cfg *a = &cfg->transmit[i];
            if (a->function != fc) { continue; }
            if (a->address == address && a->count == count) { exact = a; }
            for (size_t j = 0; j < i; ++j) {
                const struct idu_transmit_cfg *b = &cfg->transmit[j];
                uint32_t begin = MAX(a->address, b->address);
                uint32_t end = MIN((uint32_t)a->address+a->count, (uint32_t)b->address+b->count);
                if (b->function == fc && begin < end && address < end && (uint32_t)address+count > begin) { ambiguous = true; }
            }
        }
        if (ambiguous) {
            if (!exact || exact->kind != IDU_MAP_VALUE) { return -ERANGE; }
            struct idu_point_state state;
            int err = idu_process_read_at(exact->key, port, fc, address, count, &state);
            if (err) { return err; }
            if ((state.quality & 0xff) != IDU_QUALITY_GOOD) { return -ENOTSUP; }
            for (size_t i=0;i<count;++i) { words[i]=idu_order_word(state.value.words,count,i,exact->order); }
            return 0;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (!mapping(fc, address + i)) { return -ENOENT; }
    }
    for (size_t i = 0; i < count;) {
        const struct idu_transmit_cfg *m = mapping(fc, address + i);
        size_t offset = address + i - m->address;
        size_t n = MIN(m->count - offset, count - i);
        struct idu_point_state state;
        if (m->kind != IDU_MAP_ZERO) {
            int err = idu_process_read_at(m->key, port, fc, address, count, &state);
            if (err) { return err; }
            if (m->kind == IDU_MAP_VALUE && (state.quality & 0xff) != IDU_QUALITY_GOOD) { return -ENOTSUP; }
        }
        for (size_t w = 0; w < n; ++w) {
            words[i+w] = m->kind == IDU_MAP_ZERO ? 0 : m->kind == IDU_MAP_QUALITY ? state.quality :
                         idu_order_word(state.value.words, m->count, offset + w, m->order);
        }
        i += n;
    }
    return 0;
}

int idu_forward_write(uint16_t port, uint16_t address, const uint16_t *words, size_t count)
{
    if (!words || !count || count > IDU_POLL_REGS || (uint32_t)address + count > 65536) { return -EINVAL; }
    struct idu_process_request request = {.operation = IDU_OP_WRITE, .port_id = port,
                                          .function = idu_modbus_request_function(),
                                          .address = address, .register_count = count};
    if (!request.function) { request.function = count == 1 ? 6 : 16; }
    for (size_t offset = 0; offset < count;) {
        const struct idu_transmit_cfg *m = mapping(3, address + offset);
        const struct idu_point_cfg *p = m ? idu_points_find(m->key) : NULL;
        if (!m || !p || m->kind != IDU_MAP_VALUE) { return -ENOENT; }
        if (p->source != IDU_SOURCE_PARAMETER) { return -EACCES; }
        if (m->address != address + offset || m->count != p->width || offset + m->count > count ||
            request.count == ARRAY_SIZE(request.items)) { return -EINVAL; }
        struct idu_point_update *item = &request.items[request.count++].point;
        item->key = m->key; item->width = p->width;
        for (size_t w = 0; w < p->width; ++w) { item->value.words[w] = idu_order_word(words+offset, p->width, w, m->order); }
        offset += p->width;
    }
    struct idu_process_result result;
    return idu_process_execute(&request, &result);
}

int idu_forward_dispatch(int iface, const struct modbus_adu *rx,
                          struct modbus_adu *tx, void *user)
{
    const struct idu_forward_routes *routes = user;
    if (!rx || !tx || rx->length < 4 || rx->length > sizeof(rx->data)) { return -EINVAL; }
    tx->unit_id = rx->unit_id;
    tx->fc = rx->fc;
    tx->trans_id = rx->trans_id;
    tx->proto_id = rx->proto_id;
    uint16_t address = sys_get_be16(rx->data);
    uint16_t count = rx->fc == 6 ? 1 : sys_get_be16(rx->data + 2);
    if (!count || (uint32_t)address + count > 65536) { return -EINVAL; }
    if ((rx->fc == 3 || rx->fc == 4) && (rx->length != 4 || count > 125 || 1 + count * 2 > sizeof(tx->data))) { return -EINVAL; }
    if (rx->fc == 6 && rx->length != 4) { return -EINVAL; }
    if (rx->fc == 16 && (count > 123 || rx->length != 5 + 2 * count || rx->data[4] != 2 * count)) { return -EINVAL; }
    if (routes) {
        for (size_t i = 0; i < routes->count; ++i) {
            const struct idu_forward_segment *s = &routes->segments[i];
            if (s->function && s->function != rx->fc) { continue; }
            bool overlaps = address <= s->end && (uint32_t)address + count > s->begin;
            if (!overlaps) { continue; }
            if (address < s->begin || (uint32_t)address + count - 1 > s->end) { return -ERANGE; }
            if (s->handler) { return s->handler(iface, rx, tx, s->user); }
            *tx = *rx;
            uint32_t target = s->target_begin + address - s->begin;
            if (target + count > 65536) { return -ERANGE; }
            sys_put_be16(target, tx->data);
            const struct idu_equip_cfg *cfg = idu_points_config();
            enum idu_port_mode mode = IDU_PORT_POLL;
            for (size_t d = 0; cfg && d < cfg->device_count; ++d) {
                if (cfg->devices[d].dev_id == s->target_dev) {
                    idu_collect_get_mode(cfg->devices[d].bus_id, &mode); break;
                }
            }
            if (mode != IDU_PORT_POLL && mode != IDU_PORT_PASSTHROUGH) { return -EBUSY; }
            int err = idu_collect_transfer_mode(s->target_dev, mode, tx);
            tx->unit_id = rx->unit_id; tx->trans_id = rx->trans_id;
            if (!err && (rx->fc == 6 || rx->fc == 16)) { sys_put_be16(address, tx->data); }
            return err;
        }
    }
    if (rx->fc == 3 || rx->fc == 4) {
        uint16_t words[125];
        int err = idu_forward_read(iface, rx->fc, address, words, count);
        if (err) { return err; }
        tx->length = 1 + count * 2; tx->data[0] = count * 2;
        for (size_t i = 0; i < count; ++i) { sys_put_be16(words[i], tx->data + 1 + i * 2); }
        return 0;
    }
    return -ENOENT; /* Application command registers and FC06/16 writer. */
}

int idu_forward_attach(int iface, const struct idu_forward_routes *routes)
{
    if (routes && routes->count && !routes->segments) { return -EINVAL; }
    for (size_t i = 0; routes && i < routes->count; ++i) {
        const struct idu_forward_segment *a = &routes->segments[i];
        if (a->begin > a->end || (!a->handler && !a->target_dev)) { return -EINVAL; }
        for (size_t j = 0; j < i; ++j) {
            const struct idu_forward_segment *b = &routes->segments[j];
            if ((!a->function || !b->function || a->function == b->function) &&
                a->begin <= b->end && b->begin <= a->end) { return -EEXIST; }
        }
    }
    return idu_modbus_set_router(iface, idu_forward_dispatch, (void *)routes);
}
#endif
