/**
 * @file
 * @brief 产品设备与信号键。
 */
#ifndef SIGNAL_KEYS_H
#define SIGNAL_KEYS_H

#define DEVICE_TYPE 0x2221
#define PROTOCOL_VERSION 0x01000101U

/**
 * @brief 逻辑设备与信号键；与 process 行索引和协议寄存器地址独立。
 */
#define DEVICE_LOCAL 1
#define DEVICE_REMOTE_IO 10

/**
 * @brief 本地与南向设备共用这些信号键，通过设备 ID 区分测点。
 */
#define SIGNAL_DI 8152
#define SIGNAL_CURRENT_1 8163
#define SIGNAL_CURRENT_2 8164
#define SIGNAL_CURRENT_3 8165
#define SIGNAL_CURRENT_4 8166
#define SIGNAL_CURRENT_5 8167
#define SIGNAL_CURRENT_6 8168
#define SIGNAL_CURRENT_7 8169
#define SIGNAL_CURRENT_8 8170
#define SIGNAL_PROTOCOL 8150
#define SIGNAL_ADDRESS 30459

#define SIGNAL_SAVED_ADDRESS 30461 /**< 内部键；通信地址为 30459。 */
#define SIGNAL_VIRTUAL_MODE 10012
#define SIGNAL_SAMPLE_PERIOD 31000
#define SIGNAL_LABEL 31001
#define SIGNAL_REMOTE_SETPOINT 31500
#define SIGNAL_REMOTE_OFFSET 31501
#define SIGNAL_RESTORE_LOCAL 31090
#define SIGNAL_RESTORE_REMOTE 31091
#define SIGNAL_PARAMETER_STATUS 32000 /**< 四字：标志、错误、序号低字、高字。 */

#endif
