/** @file @brief 产品 DP、转发表和协议会话配置。 */
#ifndef APPLICATION_PROTOCOL_H
#define APPLICATION_PROTOCOL_H
#include <vport/vport.h>
#define RS485_1_CHANNEL_ID 1U
#define RS485_2_CHANNEL_ID 2U
#define RS485_3_CHANNEL_ID 3U

typedef struct {
    vport_channel_id_t channel_id;
    bool upgrade;
} protocol_channel_config_t;

/** @brief INIT 下初始化 DP、转发表及会话；不启动硬件或线程，配置按值读取。 */
int protocol_init(
    vport_t *port, const protocol_channel_config_t *configs, size_t count, uint32_t gap_us);
/** @brief 启动失败回滚；调用方先停止并等待全部输入，vport 须为 INIT/STOPPED。 */
void protocol_deinit(void);
uint8_t protocol_unit(void);
#endif
