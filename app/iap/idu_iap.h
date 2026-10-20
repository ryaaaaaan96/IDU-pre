#ifndef IDU_IAP_H
#define IDU_IAP_H
#include <stdbool.h>
#include <zephyr/modbus/modbus.h>
int idu_iap_init(void);
bool idu_iap_ready(void);
int idu_iap_route(int iface, const struct modbus_adu *rx, struct modbus_adu *tx);
#endif
