/** @file @brief 产品通信硬件装配与启动入口。 */
#ifndef APPLICATION_COMMUNICATION_H
#define APPLICATION_COMMUNICATION_H
/** @brief 一次性装配全部通信资源；失败后须重启，重复调用返回 -EALREADY。 */
int communication_init(void);
#endif
