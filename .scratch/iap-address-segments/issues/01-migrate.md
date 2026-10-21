Status: resolved
Blocked by: None

# 将 IAPv3 接入设备协议地址段

按[已确认方案](../spec.md)实现参考三字段段表与 `PRO_ADDR_SLAVE` 设备关联，
保留现有升级线协议及普通点表行为，删除独立扩展入口。

## Answer

实现与验证完成。协议段在标准 PDU 校验前按首地址分派，IAP 自行校验变长载荷，
段内错误不回退；广播由配置显式允许。登记表与配置位于 Flash。
完整回归 45 次 native_sim、298 项 Ztest 通过，IDU/IAPv3 构建和镜像验签通过。
详见[验证记录](../../../docs/testing.md)；实板测试未执行。
