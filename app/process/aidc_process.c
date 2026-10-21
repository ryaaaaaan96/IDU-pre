#include "process/idu_process.h"
#include "protocol/inc/imported_signals.h"
#include "protocol/inc/signal_ids.h"
#include <dataprocess/process.h>
#include <errno.h>
#include <string.h>

static FUNC_PARA_S *point_argument(FUNC_PARA_ALL *in)
{
    if (!in || in->para_num != 1 || !in->para_all ||
        !in->para_all[0].pvalue || !in->para_all[0].sig_size ||
        in->para_all[0].sig_size > sizeof(struct dp_value)) {
        return NULL;
    }
    return &in->para_all[0];
}

static int publish_value(FUNC_PARA_S *item, const struct dp_value *value)
{
    for (size_t i = 0; i < item->sig_size; ++i) {
        item->pvalue[i] = value->words[i / 2] >> (8 * (i % 2));
    }
    return dp_set_para_only_dm_no_store(item->dev_id, item->sig_id,
                                         item->sig_size, item->pvalue);
}

int32_t idu_serial_setting(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                           void *new_value, void *old_value)
{
    ARG_UNUSED(out); ARG_UNUSED(new_value); ARG_UNUSED(old_value);
    FUNC_PARA_S *item = point_argument(in);
    if (!item) { return -EINVAL; }
    if (in->type != DP_PARA_SET) { return 0; }
    if (item->sig_size != sizeof(uint32_t)) { return -EINVAL; }
    uint32_t value = 0;
    for (size_t i = 0; i < sizeof(value); ++i) {
        value |= (uint32_t)item->pvalue[i] << (8 * i);
    }
    /* Only the north UART has a physical binding. */
    switch (item->sig_id) {
    case AIDC_LOCAL_SIG_UART_PORT1_BAUD:
        return value < 161 || value > 167 ? -ERANGE : 0;
    case AIDC_LOCAL_SIG_UART_PORT1_PARITY:
        return value != 85 && value != 161 && value != 162 ? -ERANGE : 0;
    case AIDC_LOCAL_SIG_UART_PORT1_STOP_BITS:
        return value != 161 && value != 162 ? -ERANGE : 0;
    case AIDC_LOCAL_SIG_UART_PORT1_WORKMODE:
        return value == 2 ? 0 : -ENOTSUP;
    case AIDC_LOCAL_SIG_UART_PORT1_IFS:
        return -ENOTSUP;
    default:
        return -ENODEV;
    }
}

int32_t idu_protocol_identity(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value)
{
    ARG_UNUSED(out); ARG_UNUSED(new_value); ARG_UNUSED(old_value);
    FUNC_PARA_S *item = point_argument(in);
    if (!item) { return -EINVAL; }
    struct dp_value value = {0};
    uint32_t number;
    switch (item->sig_id) {
    case AIDC_LOCAL_SIG_2500: number = 0x50413000U; break;
    case AIDC_LOCAL_SIG_2502: number = 0x01000300U; break;
    case AIDC_LOCAL_SIG_2569:
    case AIDC_LOCAL_SIG_2571: number = 0x00072001U; break;
    case AIDC_LOCAL_SIG_2581: number = 4; break;
    default: return -ENOENT;
    }
    value.words[0] = number;
    value.words[1] = number >> 16;
    return publish_value(item, &value);
}

int32_t idu_firmware_version(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                             void *new_value, void *old_value)
{
    ARG_UNUSED(out); ARG_UNUSED(new_value); ARG_UNUSED(old_value);
    FUNC_PARA_S *item = point_argument(in);
    if (!item) { return -EINVAL; }
    struct dp_value value = {0};
    if (item->sig_id == AIDC_LOCAL_SIG_VERSION) {
        const char version[] = "IDU-pre_Zephyr_0.1.0";
        for (size_t i = 0; i < sizeof(version) - 1; ++i) {
            value.words[i / 2] |= (uint16_t)(uint8_t)version[i] << (i % 2 ? 0 : 8);
        }
    } else {
        const uint16_t stride = AIDC_LOCAL_SIG_2612 - AIDC_LOCAL_SIG_2597;
        if (item->sig_id < AIDC_LOCAL_SIG_2597 || item->sig_id > AIDC_LOCAL_SIG_2702 ||
            (item->sig_id - AIDC_LOCAL_SIG_2597) % stride) { return -ENOENT; }
        struct dp_key source = {
            IDU_DEV_DSP_FIRST + (item->sig_id - AIDC_LOCAL_SIG_2597) / stride,
            AIDC_LOCAL_SIG_VERSION,
        };
        struct dp_point_state state;
        int err = dp_read(source, &state);
        if (err) { return err; }
        if ((state.quality & 0xff) != DP_QUALITY_GOOD) {
            return in->type == DP_PARA_INIT ? 0 : -ENODATA;
        }
        value = state.value;
    }
    return publish_value(item, &value);
}

int32_t idu_point_alias(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                        void *new_value, void *old_value)
{
    ARG_UNUSED(out); ARG_UNUSED(new_value); ARG_UNUSED(old_value);
    /* Aliases already address the same canonical value in the transmit table. */
    return point_argument(in) ? 0 : -EINVAL;
}

int32_t idu_unsupported_service(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                                void *new_value, void *old_value)
{
    ARG_UNUSED(out); ARG_UNUSED(new_value); ARG_UNUSED(old_value);
    if (!point_argument(in)) { return -EINVAL; }
    /* Network, AES, logs and DSP management require separate product services. */
    return in->type == DP_PARA_SET ? -ENOTSUP : 0;
}
