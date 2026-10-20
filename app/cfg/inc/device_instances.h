#ifndef IDU_EQUIP_INSTANCES_H
#define IDU_EQUIP_INSTANCES_H

/* X(逻辑设备号, 总线, 从机地址, 北向窗口基址, 地址寄存器)。
 * 各实例共享 const MV-IO 处理/采集表；启动采集前须配置对应总线后端。
 */
#define IDU_MVIO_INSTANCES(X) \
    X(IDU_DEV_REMOTE_IO, 0, 1, 10000, 30460)

#endif
