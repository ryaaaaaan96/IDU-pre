/** @file
 * @brief 整机状态指示灯接口，使用系统工作队列更新。
 */
#ifndef STATUS_LED_H
#define STATUS_LED_H

/** @brief 检查并配置 GPIO，启动期间点亮；失败返回负错误码。 */
int status_led_init(void);
/** @brief 初始化成功且进入运行状态后，启动周期显示。 */
void status_led_start(void);
/** @brief 提交立即更新请求，用于终止故障后点亮指示灯。 */
void status_led_refresh(void);

#endif
