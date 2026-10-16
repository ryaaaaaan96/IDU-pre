#include "idu_equip.h"
#include "idu_aidc.h"
#include <idu/collect.h>
#include <idu/forward.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>
#include <errno.h>

static void *setup(void)
{
    const struct idu_equip_cfg *cfg=idu_equipment_config();
    zassert_equal(cfg->point_count,552);
    zassert_equal(cfg->collect_count,332);
    zassert_equal(cfg->transmit_count,108);
    zassert_ok(idu_points_validate(cfg));
    zassert_ok(idu_points_init(cfg));
    int iface=idu_acquisition_port_init(); zassert_true(iface>=0);
    zassert_ok(idu_collect_init(cfg,iface,true));
    zassert_ok(idu_aidc_bus_init(cfg));
    zassert_ok(idu_params_init(cfg,DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings)),iface,true));
    zassert_ok(idu_process_init_device(0));
    return NULL;
}

ZTEST(aidc,test_original_tables_and_model_acquisition)
{
    const struct idu_equip_cfg *cfg=idu_equipment_config();
    for (size_t i=4;i<cfg->collect_count;++i) {
        zassert_true(cfg->collect[i].model_protocol);
        zassert_ok(idu_collect_poll(i));
    }
    k_sleep(K_MSEC(50));
    for (uint16_t dev=11;dev<=18;++dev) {
        struct idu_point_state state;
        zassert_ok(idu_points_read((struct idu_key){dev,2582},&state));
        zassert_equal(state.quality,IDU_QUALITY_SIMULATED|IDU_QUALITY_GOOD);
    }
    uint16_t v; zassert_ok(idu_forward_read(0,3,41654,&v,1)); zassert_equal(v,1);
    zassert_ok(idu_forward_read(0,4,10012,&v,1)); zassert_equal(v,1);
}

ZTEST(aidc,test_original_write_map_and_serial_enum)
{
    uint16_t words[]={162,0}; /* legacy enum for 19200 baud */
    zassert_ok(idu_forward_write(7,41655,words,2));
    struct modbus_iface_param cfg={0}; zassert_ok(idu_aidc_serial(&cfg));
    zassert_equal(cfg.serial.baud,19200);
    zassert_ok(idu_params_reload()); zassert_ok(idu_aidc_serial(&cfg)); zassert_equal(cfg.serial.baud,19200);
    words[0]=160; zassert_equal(idu_forward_write(7,41655,words,2),-ERANGE);
    words[0]=85; zassert_ok(idu_forward_write(7,41657,words,2));
    zassert_ok(idu_aidc_serial(&cfg)); zassert_equal(cfg.serial.parity,UART_CFG_PARITY_NONE);
    words[0]=123; zassert_equal(idu_forward_write(7,41657,words,2),-ERANGE);
}

ZTEST(aidc,test_ambiguous_legacy_addresses_require_whole_point)
{
    struct idu_point_update points[]={
        {.key={247,1001},.width=2,.value.words={0x1234,0x12}},
        {.key={247,1002},.width=2,.value.words={0x4321,0x23}},
    };
    zassert_ok(idu_points_commit(points,2));
    uint16_t words[3];
    zassert_ok(idu_forward_read(1,4,43001,words,2)); zassert_equal(words[0],0x1234);
    zassert_ok(idu_forward_read(1,4,43002,words,2)); zassert_equal(words[0],0x4321);
    zassert_equal(idu_forward_read(1,4,43001,words,3),-ERANGE);
    zassert_equal(idu_forward_read(1,4,43002,words,1),-ERANGE);
}
ZTEST_SUITE(aidc,NULL,setup,NULL,NULL,NULL);
