# DP 点表接口简化

四个主接口：dp_register / dp_register_table / dp_set_para / dp_get_para。
删除冗余兼容接口、停止状态、空操作；保留产品实际调用的参数恢复与状态查询。
同步迁移应用、IAP 和相关测试，绘制当前结构。NVS 格式保持兼容。
