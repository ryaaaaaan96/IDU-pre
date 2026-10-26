# LED 独立目录

按用户要求，status_led.c/.h 移至 app/status/status_led/，与 image_confirm/ 同级。
更新头文件引用、CMake 和当前文档路径，不修改 LED 行为。
MCUboot + IAPv3 固件编译及验签通过，镜像摘要与移动前一致。
diff 空白检查通过，未实板验证。日志：工作区 build/status-led-directory.log。
