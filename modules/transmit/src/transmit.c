#include <dataprocess/points.h>
#include <transmit/transmit.h>
#if defined(CONFIG_DP_PROCESS)
#include <dataprocess/process.h>
#endif
#if defined(CONFIG_COLLECT)
#include <collect/collect.h>
#endif
#include <errno.h>
#include <zephyr/sys/byteorder.h>

static int read_point(struct dp_key key, uint16_t port, uint8_t fc,
                      uint16_t address, uint16_t count, struct dp_point_state *state)
{
#if defined(CONFIG_DP_PROCESS)
    return dp_process_read_at(key, port, fc, address, count, state);
#else
    ARG_UNUSED(port); ARG_UNUSED(fc); ARG_UNUSED(address); ARG_UNUSED(count);
    return dp_read(key, state);
#endif
}

int tr_read_register(uint8_t function, uint16_t address, uint16_t *value)
{
    const struct dp_equip_cfg *cfg = dp_config();
    if (!cfg || !value) { return -EINVAL; }
    if (cfg->exact_overlapping_reads) {
        size_t matches = 0;
        for (size_t i = 0; i < cfg->transmit_count; ++i) {
            struct dp_transmit_cfg t_scratch;
            const struct dp_transmit_cfg *t = dp_config_transmit(cfg, i, &t_scratch);
            if (t->function == function && address >= t->address &&
                (uint32_t)address < (uint32_t)t->address + t->count) { ++matches; }
        }
        if (matches > 1) { return -ERANGE; }
    }
    for (size_t i = 0; i < cfg->transmit_count; ++i) {
        struct dp_transmit_cfg t_scratch;
        const struct dp_transmit_cfg *t = dp_config_transmit(cfg, i, &t_scratch);
        if (t->function != function || address < t->address ||
            (uint32_t)address >= (uint32_t)t->address + t->count) { continue; }
        if (t->kind == DP_MAP_ZERO) { *value = 0; return 0; }
        struct dp_point_state state;
        int err = read_point(t->key, 0, function, address, 1, &state);
        if (err) { return err; }
        if (t->kind == DP_MAP_QUALITY) { *value = state.quality; return 0; }
        if ((state.quality & 0xffU) != DP_QUALITY_GOOD) { return -EIO; }
        *value = dp_order_word(state.value.words, t->count, address - t->address, t->order);
        return 0;
    }
    return -ENOTSUP;
}

static int input_read(uint16_t address, uint16_t *value)
{
    return tr_read_register(4, address, value);
}

static int holding_read(uint16_t address, uint16_t *value)
{
    return tr_read_register(3, address, value);
}

struct modbus_user_callbacks *tr_callbacks(void)
{
    static struct modbus_user_callbacks callbacks = {
        .input_reg_rd = input_read,
        .holding_reg_rd = holding_read,
    };
    return &callbacks;
}

static const struct dp_transmit_cfg *mapping(uint8_t fc, uint16_t address, struct dp_transmit_cfg *scratch)
{
    const struct dp_equip_cfg *cfg = dp_config();
    if (!cfg) { return NULL; }
    for (size_t i = 0; i < cfg->transmit_count; ++i) {
        const struct dp_transmit_cfg *m = dp_config_transmit(cfg, i, scratch);
        if (m->function == fc && address >= m->address &&
            (uint32_t)address < (uint32_t)m->address + m->count) { return m; }
    }
    return NULL;
}

int tr_read(uint16_t port, uint8_t fc, uint16_t address, uint16_t *words, size_t count)
{
    if (!words || !count || count > 125 || (fc != 3 && fc != 4) ||
        (uint32_t)address + count > 65536) { return -EINVAL; }
    const struct dp_equip_cfg *cfg = dp_config();
    if (cfg && cfg->exact_overlapping_reads) {
        bool ambiguous = false;
        struct dp_transmit_cfg exact_copy;
        const struct dp_transmit_cfg *exact = NULL;
        for (size_t i = 0; i < cfg->transmit_count; ++i) {
            struct dp_transmit_cfg a_scratch;
            const struct dp_transmit_cfg *a = dp_config_transmit(cfg, i, &a_scratch);
            if (a->function != fc) { continue; }
            if (a->address == address && a->count == count) { exact_copy = *a; exact = &exact_copy; }
            for (size_t j = 0; j < i; ++j) {
                struct dp_transmit_cfg b_scratch;
                const struct dp_transmit_cfg *b = dp_config_transmit(cfg, j, &b_scratch);
                uint32_t begin = MAX(a->address, b->address);
                uint32_t end = MIN((uint32_t)a->address+a->count, (uint32_t)b->address+b->count);
                if (b->function == fc && begin < end && address < end && (uint32_t)address+count > begin) { ambiguous = true; }
            }
        }
        if (ambiguous) {
            if (!exact || exact->kind != DP_MAP_VALUE) { return -ERANGE; }
            struct dp_point_state state;
            int err = read_point(exact->key, port, fc, address, count, &state);
            if (err) { return err; }
            if ((state.quality & 0xff) != DP_QUALITY_GOOD) { return -ENOTSUP; }
            for (size_t i=0;i<count;++i) { words[i]=dp_order_word(state.value.words,count,i,exact->order); }
            return 0;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        struct dp_transmit_cfg scratch;
        if (!mapping(fc, address + i, &scratch)) { return -ENOENT; }
    }
    for (size_t i = 0; i < count;) {
        struct dp_transmit_cfg scratch;
        const struct dp_transmit_cfg *m = mapping(fc, address + i, &scratch);
        size_t offset = address + i - m->address;
        size_t n = MIN(m->count - offset, count - i);
        struct dp_point_state state;
        if (m->kind != DP_MAP_ZERO) {
            int err = read_point(m->key, port, fc, address, count, &state);
            if (err) { return err; }
            if (m->kind == DP_MAP_VALUE && (state.quality & 0xff) != DP_QUALITY_GOOD) { return -ENOTSUP; }
        }
        for (size_t w = 0; w < n; ++w) {
            words[i+w] = m->kind == DP_MAP_ZERO ? 0 : m->kind == DP_MAP_QUALITY ? state.quality :
                         dp_order_word(state.value.words, m->count, offset + w, m->order);
        }
        i += n;
    }
    return 0;
}

int tr_write(uint16_t port, uint16_t address, const uint16_t *words, size_t count)
{
#if defined(CONFIG_DP_PROCESS)
    if (!words || !count || count > DP_TRANSFER_WORDS || (uint32_t)address + count > 65536) { return -EINVAL; }
    struct dp_process_request request = {.operation = DP_OP_WRITE, .port_id = port,
                                          .address = address, .register_count = count};
#if defined(CONFIG_TR_MODBUS_ADAPTER)
    request.function = tr_modbus_request_function();
#endif
    if (!request.function) { request.function = count == 1 ? 6 : 16; }
    for (size_t offset = 0; offset < count;) {
        struct dp_transmit_cfg scratch;
        struct dp_point_cfg point_scratch;
        const struct dp_transmit_cfg *m = mapping(3, address + offset, &scratch);
        const struct dp_point_cfg *p = m ? dp_find(m->key, &point_scratch) : NULL;
        if (!m || !p || m->kind != DP_MAP_VALUE) { return -ENOENT; }
        /* In original tables FC03 provides write access independently of
         * FLAG_PERSISTENCE. Volatile local values still run their callback.
         * Remote samples need an explicit forwarding parameter definition.
         */
        bool volatile_reference = p->reference_data_type && p->source == DP_SOURCE_LOCAL;
        if (p->source != DP_SOURCE_PARAMETER && !volatile_reference) { return -EACCES; }
        if (m->address != address + offset || m->count != p->width || offset + m->count > count ||
            request.count == ARRAY_SIZE(request.items)) { return -EINVAL; }
        struct dp_point_update *item = &request.items[request.count++].point;
        item->key = m->key; item->width = p->width;
        for (size_t w = 0; w < p->width; ++w) { item->value.words[w] = dp_order_word(words+offset, p->width, w, m->order); }
        offset += p->width;
    }
    struct dp_process_result result;
    return dp_process_execute(&request, &result);
#else
    ARG_UNUSED(port); ARG_UNUSED(address); ARG_UNUSED(words); ARG_UNUSED(count);
    return -ENOTSUP;
#endif
}

int tr_dispatch(int iface, const struct modbus_adu *rx,
                          struct modbus_adu *tx, void *user)
{
    const struct tr_routes *routes = user;
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
            const struct tr_segment *s = &routes->segments[i];
            if (s->function && s->function != rx->fc) { continue; }
            bool overlaps = address <= s->end && (uint32_t)address + count > s->begin;
            if (!overlaps) { continue; }
            if (address < s->begin || (uint32_t)address + count - 1 > s->end) { return -ERANGE; }
            if (s->handler) { return s->handler(iface, rx, tx, s->user); }
#if defined(CONFIG_COLLECT)
            *tx = *rx;
            uint32_t target = s->target_begin + address - s->begin;
            if (target + count > 65536) { return -ERANGE; }
            sys_put_be16(target, tx->data);
            const struct dp_equip_cfg *cfg = dp_config();
            enum co_port_mode mode = CO_PORT_POLL;
            for (size_t d = 0; cfg && d < cfg->device_count; ++d) {
                if (cfg->devices[d].dev_id == s->target_dev) {
                    co_get_mode(cfg->devices[d].bus_id, &mode); break;
                }
            }
            if (mode != CO_PORT_POLL && mode != CO_PORT_PASSTHROUGH) { return -EBUSY; }
            int err = co_transfer_mode(s->target_dev, mode, tx);
            tx->unit_id = rx->unit_id; tx->trans_id = rx->trans_id;
            if (!err && (rx->fc == 6 || rx->fc == 16)) { sys_put_be16(address, tx->data); }
            return err;
#else
            return -ENOTSUP;
#endif
        }
    }
    if (rx->fc == 3 || rx->fc == 4) {
        uint16_t words[125];
        int err = tr_read(iface, rx->fc, address, words, count);
        if (err) { return err; }
        tx->length = 1 + count * 2; tx->data[0] = count * 2;
        for (size_t i = 0; i < count; ++i) { sys_put_be16(words[i], tx->data + 1 + i * 2); }
        return 0;
    }
    return -ENOENT; /* Application command registers and FC06/16 writer. */
}

int tr_attach(int iface, const struct tr_routes *routes)
{
#if defined(CONFIG_TR_MODBUS_ADAPTER)
    if (routes && routes->count && !routes->segments) { return -EINVAL; }
    for (size_t i = 0; routes && i < routes->count; ++i) {
        const struct tr_segment *a = &routes->segments[i];
        if (a->begin > a->end || (!a->handler && !a->target_dev)) { return -EINVAL; }
        for (size_t j = 0; j < i; ++j) {
            const struct tr_segment *b = &routes->segments[j];
            if ((!a->function || !b->function || a->function == b->function) &&
                a->begin <= b->end && b->begin <= a->end) { return -EEXIST; }
        }
    }
    return tr_modbus_set_router(iface, tr_dispatch, (void *)routes);
#else
    ARG_UNUSED(iface);
    ARG_UNUSED(routes);
    return -ENOTSUP;
#endif
}
