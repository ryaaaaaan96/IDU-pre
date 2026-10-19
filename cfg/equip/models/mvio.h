#ifndef IDU_MODEL_MVIO_H
#define IDU_MODEL_MVIO_H

#include <dataprocess/points.h>

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

/* Compatibility entry point for the reusable model and existing consumers.
 * Table declarations are maintained separately by their communication role.
 */
#include "../table/process_mvio.h"
#include "../table/collect_mvio.h"
#include "../table/transmit_mvio.h"

#endif
