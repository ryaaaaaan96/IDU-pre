#ifndef IDU_IAP_H
#define IDU_IAP_H
#include <stdbool.h>
#include <zephyr/modbus/modbus.h>
/* Source compatibility for application orchestration and existing mocks.
 * New integrations use <iap/iap.h>. There is no IDU-specific implementation. */
int iap_start(void);
bool iap_ready(void);
int iap_route(int iface, const struct modbus_adu *rx, struct modbus_adu *tx);
#define idu_iap_init iap_start
#define idu_iap_ready iap_ready
#define idu_iap_route iap_route
#endif
