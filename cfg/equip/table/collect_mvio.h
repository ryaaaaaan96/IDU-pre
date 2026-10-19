#ifndef IDU_TABLE_COLLECT_MVIO_H
#define IDU_TABLE_COLLECT_MVIO_H

#include <collect/collect.h>

/* Shared MV-IO wire fields and polling blocks; bind device and cadence separately.
 * Signal IDs are provided by models/mvio.h.
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

#endif
