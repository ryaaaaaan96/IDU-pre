#include "cfg/inc/device.h"
#include "cfg/inc/imported_signals.h"
#include "communication/config/protocol_config.h"
#include "communication/ports/idu_acquisition.h"
#include "communication/ports/idu_virtual.h"
#include <collect/collect.h>
#include <dataprocess/params.h>
#include <dataprocess/points.h>
#include <dataprocess/process.h>
#include <transmit/transmit.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>
#include <errno.h>

static int configure_serial(struct modbus_iface_param *cfg)
{
    const struct idu_protocol_profile *profile = idu_protocol_config();

    return profile->serial ? profile->serial(cfg) : 0;
}

static void *setup(void)
{
    zassert_ok(idu_register_point_tables());
    zassert_ok(dp_init());
    const struct dp_equip_cfg *cfg=dp_config();
    zassert_not_null(cfg);
    zassert_equal(cfg->point_count,553);
    zassert_equal(cfg->collect_count,339);
    zassert_equal(cfg->transmit_count,108);
    zassert_ok(dp_validate(cfg));
    int iface=idu_acquisition_port_init(); zassert_true(iface>=0);
    zassert_ok(co_init(cfg,iface,true));
    zassert_ok(idu_aidc_bus_init(cfg));
    zassert_ok(dp_params_init(cfg,DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings))));
    zassert_ok(dp_process_init_device(0));
    return NULL;
}

ZTEST(aidc,test_original_tables_and_model_acquisition)
{
    const struct dp_equip_cfg *cfg=dp_config();
    for (size_t i=11;i<cfg->collect_count;++i) {
        struct co_cfg poll;
        zassert_not_null(dp_config_collect(cfg, i, &poll));
        zassert_true(poll.model_protocol);
        zassert_ok(co_poll(i));
    }
    k_sleep(K_MSEC(50));
    for (uint16_t dev=11;dev<=18;++dev) {
        struct dp_point_state state;
        zassert_ok(dp_read((struct dp_key){dev,2582},&state));
        zassert_equal(state.quality,DP_QUALITY_SIMULATED|DP_QUALITY_GOOD);
    }
    uint16_t v; zassert_ok(tr_read(0,3,41654,&v,1)); zassert_equal(v,1);
    zassert_ok(tr_read(0,4,10012,&v,1)); zassert_equal(v,1);
}

ZTEST(aidc, test_imported_parameter_defaults_and_restore_policy)
{
    DM_SIG_DATA_S data;
    bool restore;
    zassert_ok(idu_point_parameters_get(IDU_PARAMETER_COUNTRY_COMMON,
        247, AIDC_LOCAL_SIG_UART_PORT1_BAUD, &data, &restore));
    zassert_equal(data.def_value.u32, 165);
    zassert_equal(data.min_value.u32, 161);
    zassert_equal(data.max_value.u32, 167);
    zassert_true(restore);
    zassert_true(idu_point_restore(247, AIDC_LOCAL_SIG_UART_PORT1_BAUD, 4, NULL));
    zassert_ok(idu_point_parameters_get(IDU_PARAMETER_COUNTRY_COMMON,
        247, AIDC_LOCAL_SIG_DEV_SN, &data, &restore));
    zassert_false(restore);
    zassert_false(idu_point_restore(247, AIDC_LOCAL_SIG_DEV_SN, 20, NULL));
    const uint16_t zero_sn[10] = {0};
    zassert_mem_equal(data.def_value.pst, zero_sn, sizeof(zero_sn));

    /* 8 个 DSP 实例仍然共用同一组初值和范围。 */
    for (uint16_t dev = 11; dev <= 18; ++dev) {
        zassert_ok(idu_point_parameters_get(IDU_PARAMETER_COUNTRY_COMMON,
            dev, AIDC_AC2_BAT_LOW_THRESH, &data, &restore));
        zassert_equal(data.def_value.s32, 500);
        zassert_equal(data.min_value.s32, 0);
        zassert_equal(data.max_value.s32, 65535);
        zassert_true(restore);
    }
}

ZTEST(aidc,test_original_write_map_and_serial_enum)
{
    uint16_t words[]={162,0}; /* legacy enum for 19200 baud */
    zassert_ok(tr_write(7,41655,words,2));
    struct modbus_iface_param cfg={0}; zassert_ok(configure_serial(&cfg));
    zassert_equal(cfg.serial.baud,19200);
    zassert_ok(dp_params_reload()); zassert_ok(configure_serial(&cfg)); zassert_equal(cfg.serial.baud,19200);
    words[0]=160; zassert_equal(tr_write(7,41655,words,2),-ERANGE);
    words[0]=85; zassert_ok(tr_write(7,41657,words,2));
    zassert_ok(configure_serial(&cfg)); zassert_equal(cfg.serial.parity,UART_CFG_PARITY_NONE);
    words[0]=123; zassert_equal(tr_write(7,41657,words,2),-ERANGE);
}

ZTEST(aidc,test_ambiguous_legacy_addresses_require_whole_point)
{
    struct dp_point_update points[]={
        {.key={247,1001},.width=2,.value.words={0x1234,0x12}},
        {.key={247,1002},.width=2,.value.words={0x4321,0x23}},
    };
    zassert_ok(dp_commit(points,2));
    uint16_t words[3];
    zassert_ok(tr_read(1,4,43001,words,2)); zassert_equal(words[0],0x1234);
    zassert_ok(tr_read(1,4,43002,words,2)); zassert_equal(words[0],0x4321);
    zassert_equal(tr_read(1,4,43001,words,3),-ERANGE);
    zassert_equal(tr_read(1,4,43002,words,1),-ERANGE);
}
ZTEST(aidc, test_direct_business_callbacks)
{
    DATA_SET_S item = {.dev_id = 247, .sig_id = AIDC_LOCAL_SIG_2500, .sig_size = 4};
    PARA_DATA_S request = {.set_num = 1, .p_set_data = &item};
    zassert_equal(dp_fast_para_get(&request), 4);
    zassert_equal(item.p_value.u32, 0x50413000U);
    item.sig_id = AIDC_LOCAL_SIG_2502;
    zassert_equal(dp_fast_para_get(&request), 4);
    zassert_equal(item.p_value.u32, 0x01000300U);

    uint8_t version[30] = {0};
    item.sig_id = AIDC_LOCAL_SIG_VERSION;
    item.sig_size = sizeof(version);
    item.p_value.pst = version;
    zassert_equal(dp_fast_para_get(&request), sizeof(version));
    /* Protocol strings retain high-byte-first characters in each word. */
    zassert_equal(version[0], 'D');
    zassert_equal(version[1], 'I');

    struct dp_param_status before, after;
    uint8_t value[16] = {1};
    uint8_t original[16], current[16];
    zassert_equal(dp_get_sig_value(247, AIDC_LOCAL_SIG_TR_AES_KEY_VAL,
                                  sizeof(original), original), sizeof(original));
    dp_params_get_status(&before);
    zassert_equal(dp_set_sig_value(247, AIDC_LOCAL_SIG_TR_AES_KEY_VAL,
                                   sizeof(value), value), -ENOTSUP);
    dp_params_get_status(&after);
    zassert_equal(after.sequence, before.sequence);
    zassert_equal(dp_get_sig_value(247, AIDC_LOCAL_SIG_TR_AES_KEY_VAL,
                                  sizeof(current), current), sizeof(current));
    zassert_mem_equal(current, original, sizeof(original));

    uint16_t volatile_value = 27;
    zassert_ok(tr_write(7, 30098, &volatile_value, 1));
    zassert_ok(tr_read(7, 3, 30098, &volatile_value, 1));
    zassert_equal(volatile_value, 27);
    dp_params_get_status(&after);
    zassert_equal(after.sequence, before.sequence); /* FC03 alone never means NVS. */
    volatile_value = 1;
    zassert_equal(tr_write(7, 31999, &volatile_value, 1), -ENOTSUP);
    struct dp_point_state state;
    zassert_ok(dp_read((struct dp_key){247, AIDC_LOCAL_SIG_31999}, &state));
    zassert_equal(state.value.words[0], 0);
    zassert_equal(state.quality, DP_QUALITY_UNINITIALIZED);
    zassert_equal(tr_read(7, 3, 31999, &volatile_value, 1), -ENOTSUP);

}

ZTEST_SUITE(aidc,NULL,setup,NULL,NULL,NULL);
