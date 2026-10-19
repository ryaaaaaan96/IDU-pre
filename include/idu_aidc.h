#ifndef IDU_AIDC_H
#define IDU_AIDC_H
#include <dataprocess/process.h>
struct modbus_iface_param;
int idu_aidc_process(struct dp_process_ctx *ctx, struct dp_key key);
int idu_aidc_bus_init(const struct dp_equip_cfg *cfg);
int idu_aidc_serial(struct modbus_iface_param *param);
#endif
