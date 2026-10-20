#include "process/idu_process.h"
#include "cfg/tables/point_metadata.h"
#include <dataprocess/process.h>
#include <errno.h>
#include <string.h>

static void string_value(struct dp_value *v, const char *text, size_t bytes)
{
    memset(v,0,sizeof(*v));
    for (size_t i=0;i<bytes && text[i];++i) {
        v->words[i/2] |= (uint16_t)(uint8_t)text[i] << (i%2 ? 0 : 8);
    }
}

int idu_aidc_process(struct dp_process_ctx *ctx, struct dp_key key)
{
    size_t count;
    const struct idu_process_binding *handlers = idu_process_bindings(&count);
    const struct idu_process_binding *h = NULL;
    for (size_t i=0;i<count;++i) { if (handlers[i].sig==key.sig_id) { h=&handlers[i]; break; } }
    if (!h) { return -ENOENT; }
    struct dp_value v;
    int err = dp_txn_get(ctx,key,false,&v);
    if (err) { return err; }
    bool write = ctx->request->operation == DP_OP_WRITE;
    if (!strcmp(h->callback,"local_equip_take_sig_effect")) {
        if (!write) { return 0; }
        /* Only the current north UART has a physical binding. Other legacy
         * UART/CAN names remain visible in metadata, not fake working ports. */
        if (key.sig_id==AIDC_LOCAL_SIG_UART_PORT1_BAUD) {
            return v.words[1] || v.words[0]<161 || v.words[0]>167 ? -ERANGE : 0;
        }
        if (key.sig_id==AIDC_LOCAL_SIG_UART_PORT1_PARITY) {
            return v.words[1] || (v.words[0]!=85 && v.words[0]!=161 && v.words[0]!=162) ? -ERANGE : 0;
        }
        if (key.sig_id==AIDC_LOCAL_SIG_UART_PORT1_STOP_BITS) {
            return v.words[1] || (v.words[0]!=161 && v.words[0]!=162) ? -ERANGE : 0;
        }
        if (key.sig_id==AIDC_LOCAL_SIG_UART_PORT1_WORKMODE) { return v.words[0]==2 && !v.words[1] ? 0 : -ENOTSUP; }
        if (key.sig_id==AIDC_LOCAL_SIG_UART_PORT1_IFS) { return write ? -ENOTSUP : 0; }
        return -ENODEV;
    }
    if (!strcmp(h->callback,"link_inner_sn_value") || !strcmp(h->callback,"device_name_3X_2516")) {
        return 0; /* all aliases refer to the same canonical value */
    }
    if (!strcmp(h->callback,"property_prtol_3X_2500_2502")) {
        uint32_t n=key.sig_id==2500 ? 0x50413000U : 0x01000300U;
        v.words[0]=n; v.words[1]=n>>16;
    } else if (!strcmp(h->callback,"property_prtol_3X_2569_2575")) {
        v.words[0]=0x2001; v.words[1]=7;
    } else if (!strcmp(h->callback,"property_prtol_3X_2581")) {
        v.words[0]=4;
    } else if (!strcmp(h->callback,"property_prtol_3X_2582_2657")) {
        if (key.sig_id==2582) { string_value(&v,"IDU-pre_Zephyr_0.1.0",30); }
        else if (key.sig_id>=2597 && key.sig_id<=2702 && (key.sig_id-2597)%15==0) {
            struct dp_key source={11+(key.sig_id-2597)/15,2582};
            struct dp_point_state state;
            err=dp_txn_get_state(ctx,source,&state); if (err) { return err; }
            if ((state.quality & 0xff) != DP_QUALITY_GOOD) {
                return ctx->request->operation == DP_OP_INIT ? 0 : -ENODATA;
            }
            v = state.value;
        } else { return -ENOENT; }
    } else {
        /* AES, network, IAP/log/recording and DSP management are separate
         * product services. Do not acknowledge unsupported actions as success. */
        return write ? -ENOTSUP : 0;
    }
    return dp_txn_emit(ctx,key,&v,DP_OP_NO_COLLECT|DP_OP_NO_STORE);
}
