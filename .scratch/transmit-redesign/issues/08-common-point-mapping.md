# 内嵌通用测点映射

Status: resolved
Blocked by: None

## 目标与实现

按已确认方案二，将 tr_point_t 替换为 tr_modbus_point_t，内嵌 tr_mapping_t。
通用映射保存 device_id、signal_index、external_type、scale、calculation_type；
Modbus 行保存 object_type、address、count、word_order。

mapping.c 集中 DP 访问、放缩和写入换算预检；Modbus 保留地址覆盖、整点写入及编码。
预检不访问 DP，保持整笔换算预检后才执行回调的原有行为。
应用仍通过 transmit 的 tr_register() 接入，不增加公开读写接口或映射查找表。
同步应用配置、端口声明、模拟器点表、模块和产品文档及设计规格。

## 验证

transmit_stream 16/16、protocol_port 1/1 模拟器测试通过，
覆盖字序、放缩、类型转换、位访问、预检与业务失败等既有场景。
默认 MCUboot + IAPv3 产品固件构建及镜像校验通过，产物导出至
build/hex/IDU-pre-mcuboot-iapv3/；这不表示串口 IAP 路由已经接入。
git diff --check 通过。未进行实板验证；按用户要求不提交。
