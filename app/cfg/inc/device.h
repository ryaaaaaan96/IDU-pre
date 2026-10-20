#ifndef IDU_DEVICE_CONFIG_H
#define IDU_DEVICE_CONFIG_H

#include <data_process.h>
#include <collect_lib.h>
#include <transmit_lib.h>

/* 注册产品点表回调；随后由调用者执行通用模块 dp_init()。 */
int idu_register_point_tables(void);
const DEV_TABLE_S *idu_device_table(void);
uint16_t idu_device_count(void);
const DM_SIG_CFG_S *idu_process_table(uint8_t dev, uint16_t *count);
const MODBUS_RTU_TABLE_S *idu_collect_table(uint16_t *count);
const UPP_M_MOD_REG *idu_model_table(uint16_t *count);
const MB_TABLE_CELL_T *idu_transmit_table(uint16_t *count);
int idu_point_default(uint16_t dev, uint16_t sig, DM_SIG_DATA_S *value, bool *recover);
bool idu_point_restore(uint16_t dev, uint16_t sig, uint8_t size, uint8_t *value);

#endif
