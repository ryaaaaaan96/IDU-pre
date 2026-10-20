#ifndef COLLECT_LIB_H
#define COLLECT_LIB_H

#include <stdint.h>
#include <collect/collect.h>
#include <dataprocess/reference_tables.h>

typedef enum { SET_PRI_L1 = 1, SET_PRI_L2 = 2, SET_PRI_L3 = 3 } CO_SET_PRI;
typedef enum {
    CO_DEV_UNKNOWN = 0, CO_DEV_OFFLINE = 1, CO_DEV_ONLINE = 2, CO_DEV_EXCEPTION = 3,
} CO_DEV_ONLIE;
typedef enum { SCAN_PRI_LOW = 0, SCAN_PRI_HIGH = 1 } CO_SCAN_PRI;
/* Table metadata values; declaration does not enable unsupported FC17. */
typedef enum {
    FLAG_FRAME = 1, FLAG_0x03_W_0x06 = 2, FLAG_0x03_w_0x10 = 4, FLAG_0x17_W_CODE = 8,
} CO_TABLE_FLAG;

/* Reference numeric values; MODE_OTHER is unsupported, not CO_PORT_SLEEP. */
typedef enum {
    MODE_IDLE = 0, MODE_INIT = 1, MODE_UPDATE = 2, MODE_IV = 3,
    MODE_FAULT = 4, MODE_TRAN = 5, MODE_OTHER = 6,
} CO_PORT_MODE;
typedef enum { CO_SCAN_PKG_ON = 0, CO_SCAN_PKG_OFF = 1 } CO_SCAN_PKG_FLAG;
typedef struct { uint16_t port_id; } CO_CLEAN_QUEUE_ST;

/* port_id is the configured co_bus_cfg.id. Returns 0 or -errno, never ARC
 * status codes. Thread context only. timeout_ms bounds lock acquisition;
 * an active device initialization remains busy. Zero means do not wait.
 */
int co_set_port_mode(uint16_t port_id, uint8_t mode, uint32_t timeout_ms);
int co_get_port_mode(uint16_t port_id, uint8_t *mode);
int co_dev_init(uint16_t dev_id);
/* Cancels queued requests once with -ECANCELED; an executing request finishes. */
int co_clean_set_queue(CO_CLEAN_QUEUE_ST *queue);
/* RTU: address/function; model backend: model/offset. All matching blocks are
 * changed together. Mixed protocols for one device are rejected as ambiguous.
 * ON/OFF do not alter explicit co_poll() or initialization-only reads.
 */
int co_set_scan_pkg_flag(uint8_t dev_id, uint16_t reg_addr_model,
                         uint16_t reg_type_offset, uint8_t flag);

#endif
