#ifndef IDU_MODEL_MVIO_H
#define IDU_MODEL_MVIO_H

/* 下挂 MV-IO 的逻辑信号 ID；对应三表定义在 tables/ 的三个 .c 文件中。
 * ID 表示测点身份，协议寄存器地址由 collect/transmit 表分别指定。
 */
enum idu_mvio_signal {
    IDU_MVIO_DI = 8152,
    IDU_MVIO_CURRENT_1 = 8163,
    IDU_MVIO_PROTOCOL = 8150,
    IDU_MVIO_ADDRESS = 30459,
};

enum idu_collect_cycle {
    IDU_COLLECT_SAMPLE = 1,
};

#endif
