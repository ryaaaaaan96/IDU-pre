# DP 按 key/index 读写

将原 set/get 扩展成四个显式接口，旧入口删除。index 使用 size_t，从零开始，
对应 process 点表枚举；越界返回 -ENOENT。共用原回调、校验及持久化逻辑，
不增加常驻索引或锁。现有调用方迁至 by_key。
