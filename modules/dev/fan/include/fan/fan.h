#ifndef FAN_FAN_H_
#define FAN_FAN_H_

#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

/** 输出实例配置；只依赖标准 PWM 设备描述。 */
typedef struct {
    struct pwm_dt_spec pwm;
    uint32_t frequency_hz;
} fan_output_config_t;

/** 输入实例配置，与输出无归属关系。 */
typedef struct {
    struct pwm_dt_spec pwm;
    uint32_t timeout_ms;
} fan_input_config_t;

/** 一个输出实例只管理一路 PWM；实例零初始化后由 init 初始化一次。 */
typedef struct {
    struct pwm_dt_spec pwm;
    struct k_mutex lock;
    atomic_t ready;
    uint32_t frequency_hz;
    uint32_t duty_percent;
    uint32_t duty_per_mille;
} fan_output_t;

/** 一个输入实例只管理一路周期反馈，不与输出实例绑定；对象须常驻。 */
typedef struct fan_input {
    struct pwm_dt_spec pwm;
    struct k_spinlock lock;
    atomic_t ready;
    uint32_t timeout_ms;
    uint32_t tick_hz;
    uint32_t period_ticks;
    /* ISR 只更新待处理快照，换算结果由公共输入线程发布。 */
    uint32_t pending_period;
    int64_t pending_ticks;
    int64_t next_check_ms;
    int64_t sample_ticks;
    bool pending;
    bool measuring;
    bool paused;
    struct fan_input *next;
} fan_input_t;

/** 初始化单路输出为 0%，不创建线程。 */
int fan_output_init(fan_output_t *output, const fan_output_config_t *config);
/** 设置本实例占空比及频率；frequency_hz 为 0 时保留。共享定时器周期由调用方协调。 */
int fan_output_set(fan_output_t *output, uint32_t duty_percent, uint32_t frequency_hz);
/** 以千分比设置占空比，范围 0–1000；frequency_hz 为 0 时保留。 */
int fan_output_set_per_mille(fan_output_t *output, uint32_t duty_per_mille, uint32_t frequency_hz);
/** 初始化单路输入，共用线程默认每 500 ms 启动一次单次周期捕获。 */
int fan_input_init(fan_input_t *input, const fan_input_config_t *config);
/** 暂停捕获并保存最终快照，用于外部反馈源切换；仅可在线程上下文调用。 */
int fan_input_pause(fan_input_t *input);
/** 清除旧快照，稳定等待后恢复捕获；等待由共用线程执行，不阻塞调用者。 */
int fan_input_resume(fan_input_t *input, uint32_t settle_ms);

/** 读取整数 RPM；无完整周期或过期返回 -EAGAIN，失败时不修改输出。 */
int fan_input_get_speed(fan_input_t *input, uint32_t pulses_per_revolution, uint32_t *rpm);

#endif
