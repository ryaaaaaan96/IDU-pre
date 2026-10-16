#ifndef IDU_AIDC_H
#define IDU_AIDC_H
#include <idu/process.h>
int idu_aidc_process(struct idu_process_ctx *ctx, struct idu_key key);
int idu_aidc_bus_init(const struct idu_equip_cfg *cfg);
int idu_aidc_serial(struct modbus_iface_param *param);
#endif
