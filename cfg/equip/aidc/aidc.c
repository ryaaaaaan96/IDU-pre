#include "idu_aidc.h"
#include <collect/collect.h>
#include <dataprocess/params.h>
#include <errno.h>
#include <string.h>
#include "generated/symbols.h"

struct model_point {
    uint16_t sig, model, offset, legacy_cycle;
    uint8_t width;
    struct dp_value initial;
};
static const struct model_point models[] = {
#include "generated/models.inc"
};
struct legacy_handler { uint16_t sig; const char *name, *callback; };
static const struct legacy_handler handlers[] = {
#include "generated/handlers.inc"
};

static int model_read(void *user, uint16_t dev, uint16_t model, uint16_t offset,
                       uint16_t *words, size_t count)
{
    ARG_UNUSED(user);
    if (dev < 11 || dev > 18) { return -ENOENT; }
    for (size_t i = 0; i < ARRAY_SIZE(models); ++i) {
        const struct model_point *m = &models[i];
        if (m->model == model && m->offset == offset && m->width == count) {
            memcpy(words, m->initial.words, count * sizeof(*words)); return 0;
        }
    }
    return -ENOENT;
}

int idu_aidc_bus_init(const struct dp_equip_cfg *cfg)
{
    struct co_bus_cfg bus = {.id=1,.iface=-1,.simulated=true,.read_model=model_read};
    return co_init_bus(cfg, &bus);
}

static void string_value(struct dp_value *v, const char *text, size_t bytes)
{
    memset(v,0,sizeof(*v));
    for (size_t i=0;i<bytes && text[i];++i) {
        v->words[i/2] |= (uint16_t)(uint8_t)text[i] << (i%2 ? 0 : 8);
    }
}

int idu_aidc_process(struct dp_process_ctx *ctx, struct dp_key key)
{
    const struct legacy_handler *h = NULL;
    for (size_t i=0;i<ARRAY_SIZE(handlers);++i) { if (handlers[i].sig==key.sig_id) { h=&handlers[i]; break; } }
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

static uint16_t parameter(uint16_t sig, uint16_t fallback)
{
    struct dp_point_state state;
    return dp_params_read((struct dp_key){247,sig},&state) ? fallback : state.value.words[0];
}

int idu_aidc_serial(struct modbus_iface_param *param)
{
    static const uint32_t baud[]={9600,19200,38400,57600,115200,230400,460800};
    uint16_t b=parameter(AIDC_LOCAL_SIG_UART_PORT1_BAUD,165);
    uint16_t parity=parameter(AIDC_LOCAL_SIG_UART_PORT1_PARITY,85);
    uint16_t stop=parameter(AIDC_LOCAL_SIG_UART_PORT1_STOP_BITS,161);
    if (b<161 || b>167 || (parity!=85 && parity!=161 && parity!=162) || (stop!=161 && stop!=162)) { return -ERANGE; }
    param->serial.baud=baud[b-161];
    param->serial.parity=parity==85 ? UART_CFG_PARITY_NONE : parity==161 ? UART_CFG_PARITY_ODD : UART_CFG_PARITY_EVEN;
    param->serial.stop_bits=stop==161 ? UART_CFG_STOP_BITS_1 : UART_CFG_STOP_BITS_2;
    param->server.unit_id=parameter(AIDC_LOCAL_SIG_TR_ADDR,1);
    return 0;
}
