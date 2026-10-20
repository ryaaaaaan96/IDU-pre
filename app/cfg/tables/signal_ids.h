#ifndef IDU_SIGNAL_IDS_H
#define IDU_SIGNAL_IDS_H

#define IDU_DEVICE_TYPE 0x2221
#define IDU_PROTOCOL_VERSION 0x01000101U

/* Logical device and signal IDs, independent of wire register addresses. */
#define IDU_DEV_LOCAL 1
#define IDU_DEV_REMOTE_IO 10
#define IDU_SIG_DI 8152
#define IDU_SIG_CURRENT_1 8163
#define IDU_SIG_PROTOCOL 8150
#define IDU_SIG_ADDRESS 30459
#define IDU_SIG_SAVED_ADDRESS 30461 /* Internal key; legacy wire address is 30459. */
#define IDU_SIG_VIRTUAL_MODE 10012
#define IDU_SIG_SAMPLE_PERIOD 31000
#define IDU_SIG_LABEL 31001
#define IDU_SIG_REMOTE_SETPOINT 31500
#define IDU_SIG_REMOTE_OFFSET 31501

#endif
