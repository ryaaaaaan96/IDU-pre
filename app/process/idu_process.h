#ifndef IDU_PROCESS_H
#define IDU_PROCESS_H

#include <stdint.h>
/* 采样映射和业务回调；整机启动见 main.c，协议执行归通用模块。 */
typedef struct func_para_all FUNC_PARA_ALL;

int idu_process_restore(uint16_t dev);
int idu_process_flush(uint16_t unused);
int idu_process_recover(uint16_t unused);

struct idu_snapshot;

/* 应用线程发布样本和读取采样周期。 */
int idu_snapshot_publish(const struct idu_snapshot *snapshot);
uint16_t idu_sample_period_ms(void);

/* Profile-specific point handlers; selected by the product process table. */
int32_t idu_default_point(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                          void *new_value, void *old_value);
int32_t idu_initialize_point(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value);

#if defined(CONFIG_IDU_AIDC_PROFILE)
struct modbus_iface_param;
int idu_forwarding_settings(struct modbus_iface_param *param);

int32_t idu_serial_setting(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value);
int32_t idu_protocol_identity(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value);
int32_t idu_firmware_version(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value);
int32_t idu_point_alias(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value);
int32_t idu_unsupported_service(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value);
#endif

#endif
