#ifndef IDU_TABLE_PROCESS_MVIO_H
#define IDU_TABLE_PROCESS_MVIO_H

#include <dataprocess/points.h>

/* Shared MV-IO signal definitions; expand once per logical device.
 * Signal IDs are provided by models/mvio.h.
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

#endif
