/** @file @brief RS485-1 物理收发，仅供 APP 协议装配使用。 */
#ifndef RS485_1_H
#define RS485_1_H

#include <vport/vport.h>

/** @brief 启动期配置硬件及端口发送回调，不开放接收，不创建线程。 */
int rs485_1_init(vport_t *port);
/** @brief 协议绑定且接收线程就绪后开放 UART 接收。 */
void rs485_1_start(void);
/**
 * @brief 单线程读取一个事件，最多等待 1 ms；发生队列竞争时返回 -EAGAIN。
 * @note DATA 指针仅在下次调用前有效；错误和轮询事件不含数据。
 */
int rs485_1_receive(vport_event_t *event);

#endif
