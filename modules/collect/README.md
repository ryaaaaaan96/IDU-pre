# collect

应用显式注册数据连接，`co_init_bus()` 只登记采集总线，不安装 DP 参数后端或参考下发函数。

- `co_set_data_callbacks()` 复制样本和设备初始化处理函数，须在启动工作任务前调用。
- `dp_accept_samples()` 可直接作为样本处理函数；它复制入队，初始化同步采样时还等待处理结果并检查质量。
- `dp_initialize_acquired_device()` 可直接作为设备初始化后处理函数，处理 DP 初始化及参数反馈同步。
- `co_parameter_transport()` 提供可交给 `dp_params_set_transport()` 的同步读写后端。
- 使用参考下发接口时，由应用显式调用 `dp_para_reg_set_func(co_dp_fast_set)`。

未注册样本消费者时，采集发布返回 `-ENOTCONN`，不会隐式修改 DP。
参数回调在事务锁内执行，可以进行有界通信等待，但不能再次进入参数事务接口。
配置和回调上下文须覆盖运行期；运行中不支持替换数据连接。
