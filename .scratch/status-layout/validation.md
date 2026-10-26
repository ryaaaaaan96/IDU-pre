# status 公开与内部接口布局

按用户要求：inc/status.h 为唯一公开头文件；status.c、status_led.c、image_confirm.c
位于 status 根目录；status_led.h、image_confirm.h 为根目录的内部接口。
删除原 LED 和镜像确认子目录，更新应用、测试、构建和文档引用。
LED 周期常量移入内部 status_led.h；只有对应白盒测试包含内部头，业务使用 inc/status.h。

验证：系统编排 39、runtime 16、任务监控 12 场景、镜像确认 3 测试全部通过。
MCUboot + IAPv3 固件编译及验签通过，镜像摘要与移动前相同，SGZ 152032 字节。
diff 空白检查通过。未实板验证。
日志：工作区 build/status-layout-{acquisition,runtime,watchdog,image,firmware}.log。
