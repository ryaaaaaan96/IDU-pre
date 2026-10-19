#ifndef IDU_MODEL_MVIO_H
#define IDU_MODEL_MVIO_H

#include <collect/collect.h>

/* Reusable downstream MV-IO model. Bind the logical device ID separately from
 * the bus/unit ID and the northbound address base. Expansion produces const
 * tables in flash; instances share the immutable collection field arrays.
 * Register numbers below describe the downstream protocol, not the local BSP.
 */
enum idu_mvio_signal {
    IDU_MVIO_DI = 8152,
    IDU_MVIO_CURRENT_1 = 8163,
    IDU_MVIO_PROTOCOL = 8150,
    IDU_MVIO_ADDRESS = 30459,
};

/* Process point definitions. */
/* Shared MV-IO signal definitions; expand once per logical device.
 */
#define IDU_MVIO_CURRENT_POINT(dev, n, stale) \
    {.key = {dev, IDU_MVIO_CURRENT_1 + (n)}, .source = DP_SOURCE_MODBUS, \
     .type = DP_U16, .width = 1, .maximum = UINT16_MAX, .stale_ms = stale}

#define IDU_MVIO_POINTS(dev, sample_stale, identity_stale) \
    {.key = {dev, IDU_MVIO_DI}, .source = DP_SOURCE_MODBUS, \
     .type = DP_U16, .width = 1, .maximum = UINT16_MAX, .stale_ms = sample_stale}, \
    IDU_MVIO_CURRENT_POINT(dev, 0, sample_stale), \
    IDU_MVIO_CURRENT_POINT(dev, 1, sample_stale), \
    IDU_MVIO_CURRENT_POINT(dev, 2, sample_stale), \
    IDU_MVIO_CURRENT_POINT(dev, 3, sample_stale), \
    IDU_MVIO_CURRENT_POINT(dev, 4, sample_stale), \
    IDU_MVIO_CURRENT_POINT(dev, 5, sample_stale), \
    IDU_MVIO_CURRENT_POINT(dev, 6, sample_stale), \
    IDU_MVIO_CURRENT_POINT(dev, 7, sample_stale), \
    {.key = {dev, IDU_MVIO_PROTOCOL}, .source = DP_SOURCE_MODBUS, \
     .type = DP_U32, .width = 2, .maximum = UINT32_MAX, .stale_ms = identity_stale}, \
    {.key = {dev, IDU_MVIO_ADDRESS}, .source = DP_SOURCE_MODBUS, \
     .type = DP_U16, .width = 1, .minimum = 1, .maximum = 247, \
     .stale_ms = identity_stale}

/* Collection fields and blocks. */
/* Shared MV-IO wire fields and polling blocks; bind device and cadence separately.
 */
#define IDU_MVIO_CURRENT_FIELD(n) \
    {.sig_id = IDU_MVIO_CURRENT_1 + (n), .offset = n, .width = 1}

/* Declare once in the collect translation unit; reusable across instances. */
#define IDU_MVIO_COLLECT_FIELDS(name) \
    static const struct co_field name##_di[] = { \
        {.sig_id = IDU_MVIO_DI, .width = 1}, \
    }; \
    static const struct co_field name##_currents[] = { \
        IDU_MVIO_CURRENT_FIELD(0), IDU_MVIO_CURRENT_FIELD(1), \
        IDU_MVIO_CURRENT_FIELD(2), IDU_MVIO_CURRENT_FIELD(3), \
        IDU_MVIO_CURRENT_FIELD(4), IDU_MVIO_CURRENT_FIELD(5), \
        IDU_MVIO_CURRENT_FIELD(6), IDU_MVIO_CURRENT_FIELD(7), \
    }; \
    static const struct co_field name##_protocol[] = { \
        {.sig_id = IDU_MVIO_PROTOCOL, .width = 2, .order = DP_LOW_WORD_FIRST}, \
    }; \
    static const struct co_field name##_address[] = { \
        {.sig_id = IDU_MVIO_ADDRESS, .width = 1}, \
    }

#define IDU_MVIO_COLLECT(name, dev, sample_period, identity_period) \
    {.dev_id = dev, .function = 4, .address = 8152, .count = 1, \
     .period_ms = sample_period, .fields = name##_di, \
     .field_count = ARRAY_SIZE(name##_di)}, \
    {.dev_id = dev, .function = 4, .address = 8163, .count = 8, \
     .period_ms = sample_period, .fields = name##_currents, \
     .field_count = ARRAY_SIZE(name##_currents)}, \
    {.dev_id = dev, .function = 4, .address = 8150, .count = 2, \
     .period_ms = identity_period, .fields = name##_protocol, \
     .field_count = ARRAY_SIZE(name##_protocol)}, \
    {.dev_id = dev, .function = 3, .address = 30459, .count = 1, \
     .period_ms = identity_period, .fields = name##_address, \
     .field_count = ARRAY_SIZE(name##_address)}

/* Northbound value and quality mappings. */
/* MV-IO northbound value and quality mappings; bind address windows separately.
 */
#define IDU_MVIO_MAP(dev, base, offset, sig, words) \
    {.function = 4, .address = (base) + (offset), .count = words, .key = {dev, sig}}
#define IDU_MVIO_QUALITY(dev, base, offset, sig) \
    {.function = 4, .address = (base) + (offset), .count = 1, \
     .key = {dev, sig}, .kind = DP_MAP_QUALITY}
#define IDU_MVIO_TRANSMIT_CURRENT(dev, base, n) \
    IDU_MVIO_MAP(dev, base, 1 + (n), IDU_MVIO_CURRENT_1 + (n), 1)
#define IDU_MVIO_TRANSMIT_QUALITY(dev, base, n) \
    IDU_MVIO_QUALITY(dev, base, 14 + (n), IDU_MVIO_CURRENT_1 + (n))

/* Base+12/+13 remain available to the application; address_reg is a separate
 * FC03 window. Parameter write mappings belong to the application profile.
 */
#define IDU_MVIO_TRANSMIT(dev, base, address_reg) \
    IDU_MVIO_MAP(dev, base, 0, IDU_MVIO_DI, 1), \
    IDU_MVIO_TRANSMIT_CURRENT(dev, base, 0), \
    IDU_MVIO_TRANSMIT_CURRENT(dev, base, 1), \
    IDU_MVIO_TRANSMIT_CURRENT(dev, base, 2), \
    IDU_MVIO_TRANSMIT_CURRENT(dev, base, 3), \
    IDU_MVIO_TRANSMIT_CURRENT(dev, base, 4), \
    IDU_MVIO_TRANSMIT_CURRENT(dev, base, 5), \
    IDU_MVIO_TRANSMIT_CURRENT(dev, base, 6), \
    IDU_MVIO_TRANSMIT_CURRENT(dev, base, 7), \
    IDU_MVIO_QUALITY(dev, base, 9, IDU_MVIO_DI), \
    IDU_MVIO_MAP(dev, base, 10, IDU_MVIO_PROTOCOL, 2), \
    IDU_MVIO_TRANSMIT_QUALITY(dev, base, 0), \
    IDU_MVIO_TRANSMIT_QUALITY(dev, base, 1), \
    IDU_MVIO_TRANSMIT_QUALITY(dev, base, 2), \
    IDU_MVIO_TRANSMIT_QUALITY(dev, base, 3), \
    IDU_MVIO_TRANSMIT_QUALITY(dev, base, 4), \
    IDU_MVIO_TRANSMIT_QUALITY(dev, base, 5), \
    IDU_MVIO_TRANSMIT_QUALITY(dev, base, 6), \
    IDU_MVIO_TRANSMIT_QUALITY(dev, base, 7), \
    IDU_MVIO_QUALITY(dev, base, 22, IDU_MVIO_PROTOCOL), \
    IDU_MVIO_QUALITY(dev, base, 23, IDU_MVIO_ADDRESS), \
    {.function = 3, .address = address_reg, .count = 1, .key = {dev, IDU_MVIO_ADDRESS}}

#endif
