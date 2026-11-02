/**
 * @file
 * @brief 产品协议装配和运行维护接口。
 */
#ifndef APPLICATION_PROTOCOL_H
#define APPLICATION_PROTOCOL_H

/**
 * @brief 初始化 DP、登记本地点表，并在启用时启动 APP 转发端口线程。
 */
int protocol_init(void);

#endif
