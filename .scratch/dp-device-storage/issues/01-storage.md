Status: resolved
Blocked by: None

# 实现设备参数存储

## 验收

- 线性/hash 两种模式保持核心和产品行为。
- 实际 NVS 模拟验证跨启动加载、删点重加、设备隔离和未保存丢失。
- 保存失败留 dirty、保存期间修改不丢失、周期线程保存。
- 删除的擦除/映射清理失败可跨启动恢复，槽位耗尽/回收行为明确。
- 未知布局不擦除，不兼容/越界加载失败，原参数区/IAP 保留。
- 产品固件构建、签名校验和相关 Python 检查。

## Comments

未执行实板断电与串口测试，不宣称验证了实物 Flash 或 IAP 路由。

## Answer

实现完成，按用户要求未暂存、未提交。

2026-10-28 验证结果：

- dp_core、process_runtime：线性/hash 各一套，均通过。
  最终日志 /tmp/dp-final-{dp_core,process_runtime}-{normal,hash}-{build,test}.log。
- dp_storage：线性/hash 各 11 次场景运行通过，文件模拟 Flash 跨进程恢复。
  最终日志 /tmp/dp-final-dp_storage-{normal,hash}-{build,test}.log。
- periodic.conf：50 ms 专用配置验证保存线程，0.120 秒场景通过；产品仍为 5000 ms。
  /tmp/dp-storage-periodic-{build,test}.log。
- 故障及并发：Flash 保存失败保持 dirty；保存快照后修改保留新版本；回调结果保存；
  Flash 阻塞期间 RAM set/get 可用，删除等待保存线程完成；
  删除擦除失败和删除管理映射失败分别跨启动恢复；未识别布局保留；
  类型/长度及范围不兼容登记失败；设备槽位耗尽和回收。
- ADC 14 项通过：/tmp/dp-persist-adc-{build,test}.log。
- 构建配置 Python 5 项、镜像 Python 6 项通过。
  /tmp/dp-persist-{build,image}-python.log。镜像测试使用工作区 .venv/bin/python。
- 默认 IAPv3 与 --no-upload 构建及签名校验通过，无编译警告。
  /tmp/dp-persist-firmware.log、/tmp/dp-persist-no-upload.log。
  默认应用 Flash/RAM 90140/46064 B；无上传应用 76368/26680 B。
  MCUboot 36884/18096 B。默认签名 digest：
  5a0ab88271a00e10841da09752ed5ecd42ae56f5cc6219d2ad26dd7283946f2e。
- clang-format dry-run、ruff、bash -n、git diff --check 通过。

未运行完整回归；未进行实板烧录、断电、Flash 耗时、栈高水位或串口上传验证。

## 锁约定

仅两把应用自有锁，均在 points.c 私有：设备/保存锁串行化后台与生命周期，
点值锁仅用于 RAM 快照和版本确认；时间获取、回调和 Flash 都不在点值锁内。
普通 set/get 不获取设备锁。非持久点不参加保存扫描，不比较持久值或更新保存版本。
登记/删除必须由应用暂停并等待全部 DP 业务访问（包括其他设备）后执行，
避免共享链表/hash 变更与业务查找竞争。后台不依赖应用暂停，由设备锁保护。
