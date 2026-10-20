#ifndef IDU_PROCESS_H
#define IDU_PROCESS_H

#include <stdbool.h>
#include <stdint.h>
/* 采集/status 只使用服务接口，无需依赖 DP 头文件。 */
typedef struct func_para_all FUNC_PARA_ALL;

struct dp_point_cfg;
struct dp_value;
struct dp_key;
struct dp_process_ctx;

int idu_process_restore(uint16_t dev);
int idu_process_flush(uint16_t unused);
int idu_process_recover(uint16_t unused);

struct idu_snapshot;

/* Application-thread services; callers do not need point-table or NVS internals. */
int idu_equipment_init(void);
int idu_snapshot_publish(const struct idu_snapshot *snapshot);
uint16_t idu_sample_period_ms(void);
bool idu_equipment_healthy(uint32_t max_idle_ms);
#ifdef CONFIG_BOOTLOADER_MCUBOOT
bool idu_equipment_confirm_ready(void);
#endif

/* Profile-specific point handlers; selected by the product process table. */
int32_t idu_default_point(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                          void *new_value, void *old_value);
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
int32_t idu_initialize_point(FUNC_PARA_ALL *in, FUNC_PARA_ALL *out,
                              void *new_value, void *old_value);

#endif
