#ifndef IDU_TABLE_TRANSMIT_MVIO_H
#define IDU_TABLE_TRANSMIT_MVIO_H

#include <dataprocess/points.h>

/* MV-IO northbound value and quality mappings; bind address windows separately.
 * Signal IDs are provided by models/mvio.h.
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
