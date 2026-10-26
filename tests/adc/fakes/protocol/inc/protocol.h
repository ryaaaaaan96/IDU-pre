/* ADC 测试仅执行输入采样；周期编排函数由链接器移除。 */
#ifndef TEST_PROTOCOL_H
#define TEST_PROTOCOL_H
void protocol_update_status(void);
int protocol_error(void);
#endif
