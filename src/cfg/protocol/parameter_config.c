#include "signal_ids.h"
#include "protocol_config.h"
#include "idu_process.h"
#include <zephyr/sys/util.h>

/* Command registers bind an exact one-word request to a product action.
 * They do not create persistent parameter points or alter the NVS schema. */
static const struct idu_parameter_command commands[] = {
    {31090, 0xa55a, IDU_DEV_LOCAL, idu_process_restore},
    {31091, 0xa55a, IDU_DEV_REMOTE_IO, idu_process_restore},
    {31092, 1, 0, idu_process_flush},
    {31093, 1, 0, idu_process_recover},
};

static const struct idu_parameter_status_register status[] = {
    {32000, IDU_PARAMETER_FLAGS},
    {32001, IDU_PARAMETER_ERROR},
    {32002, IDU_PARAMETER_SEQUENCE_LOW},
    {32003, IDU_PARAMETER_SEQUENCE_HIGH},
};

const struct idu_parameter_command *idu_parameter_command_table(size_t *count)
{
    *count = ARRAY_SIZE(commands);
    return commands;
}

const struct idu_parameter_status_register *idu_parameter_status_table(size_t *count)
{
    *count = ARRAY_SIZE(status);
    return status;
}
