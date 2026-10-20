#include "signal_ids.h"
#include "ports.h"
#include <transmit/registers.h>
#include "process/idu_process.h"
#include <zephyr/sys/util.h>
#if defined(CONFIG_IDU_IAPV3)
#include <iap/iap.h>
#endif

/* Command registers bind an exact one-word request to a product action.
 * They do not create persistent parameter points or alter the NVS schema. */
static const struct tr_command commands[] = {
    {31090, 0xa55a, IDU_DEV_LOCAL, idu_process_restore},
    {31091, 0xa55a, IDU_DEV_REMOTE_IO, idu_process_restore},
    {31092, 1, 0, idu_process_flush},
    {31093, 1, 0, idu_process_recover},
};

static const struct tr_status_register status[] = {
    {32000, TR_PARAMETER_FLAGS},
    {32001, TR_PARAMETER_ERROR},
    {32002, TR_PARAMETER_SEQUENCE_LOW},
    {32003, TR_PARAMETER_SEQUENCE_HIGH},
};

const struct tr_register_service idu_parameter_registers = {
    .commands = commands, .command_count = ARRAY_SIZE(commands),
    .status = status, .status_count = ARRAY_SIZE(status),
#if defined(CONFIG_IDU_IAPV3)
    .extension = iap_route,
#endif
};
