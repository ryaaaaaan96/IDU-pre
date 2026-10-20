#include "protocol_config.h"
#include <zephyr/sys/util.h>

/* Legacy acceptance uses the first address, independently of callback-table
 * traversal. Preserve these windows even when individual registers are gaps. */
static const struct idu_register_range read_starts[] = {
    {8149, 8199}, {39278, 39286}, {2503, 2532}, {4989, 5014}, {2568, 2571},
    {IDU_MVIO_REG_ADDRESS, IDU_MVIO_REG_ADDRESS}, {2581, 2581}, {5486, 5486},
    {IDU_MVIO_REG_IAP_CAPABILITY, IDU_MVIO_REG_IAP_CAPABILITY}, {31996, 31997},
};

static const struct idu_register_range write_starts[] = {
    {8149, 8199}, {39278, 39286}, {IDU_MVIO_REG_ADDRESS, IDU_MVIO_REG_ADDRESS},
};

/* Standard IDU extensions are isolated from legacy broadcast and unit 247. */
static const struct idu_register_range extensions[] = {
    {10000, 10023}, {31000, 31008}, {31500, 31502}, {31090, 31093},
    {32000, 32003}, {30460, 30460},
};

static const struct idu_legacy_span input[] = {
    {2503, 30, 1}, {2568, 4, 1}, {2581, 1, 15}, {4805, 10, 1}, {4989, 26, 1},
    {5486, 2, 1}, {8149, 1, 1}, {8150, 1, 2}, {8152, 48, 1},
};

static const struct idu_legacy_span holding[] = {
    {4885, 1, 1}, {8149, 10, 1}, {8178, 10, 1}, {20003, 1, 1}, {30459, 1, 1},
    {31996, 2, 1}, {39278, 6, 1}, {39284, 1, 2},
};

const struct idu_mvio_protocol_cfg *idu_mvio_protocol_config(void)
{
    static const struct idu_mvio_protocol_cfg config = {
        .read_starts = read_starts, .read_start_count = ARRAY_SIZE(read_starts),
        .write_starts = write_starts, .write_start_count = ARRAY_SIZE(write_starts),
        .extensions = extensions, .extension_count = ARRAY_SIZE(extensions),
        .input = input, .input_count = ARRAY_SIZE(input),
        .holding = holding, .holding_count = ARRAY_SIZE(holding),
    };
    return &config;
}
