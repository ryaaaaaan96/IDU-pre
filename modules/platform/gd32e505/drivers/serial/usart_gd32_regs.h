/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GD32E505_USART_REGS_H
#define GD32E505_USART_REGS_H

/* USART5 的状态、数据及中断编码独立，不能套用 USART0–2 的寄存器偏移。 */
#define gd32_usart_flag_get(reg, flag)                                                             \
    ((reg) == USART5 ? usart5_flag_get((reg), USART5_FLAG_##flag)                                  \
                     : usart_flag_get((reg), USART_FLAG_##flag))
#define gd32_usart_flag_clear(reg, flag)                                                           \
    ((reg) == USART5 ? usart5_flag_clear((reg), USART5_FLAG_##flag)                                \
                     : usart_flag_clear((reg), USART_FLAG_##flag))
#define gd32_usart_interrupt_enable(reg, interrupt)                                                \
    ((reg) == USART5 ? usart5_interrupt_enable((reg), USART5_INT_##interrupt)                      \
                     : usart_interrupt_enable((reg), USART_INT_##interrupt))
#define gd32_usart_interrupt_disable(reg, interrupt)                                               \
    ((reg) == USART5 ? usart5_interrupt_disable((reg), USART5_INT_##interrupt)                     \
                     : usart_interrupt_disable((reg), USART_INT_##interrupt))
#define gd32_usart_interrupt_flag_get(reg, flag)                                                   \
    ((reg) == USART5 ? usart5_interrupt_flag_get((reg), USART5_INT_FLAG_##flag)                    \
                     : usart_interrupt_flag_get((reg), USART_INT_FLAG_##flag))

static inline uint32_t gd32_usart_status(uint32_t reg)
{
    return reg == USART5 ? USART5_STAT(reg) : USART_STAT0(reg);
}

static inline uint32_t gd32_usart_tx_address(uint32_t reg)
{
    return reg == USART5 ? (uint32_t)(uintptr_t)&USART5_TDATA(reg)
                         : (uint32_t)(uintptr_t)&USART_DATA(reg);
}

static inline uint32_t gd32_usart_rx_address(uint32_t reg)
{
    return reg == USART5 ? (uint32_t)(uintptr_t)&USART5_RDATA(reg)
                         : (uint32_t)(uintptr_t)&USART_DATA(reg);
}

/* 调用方已停止 DMA；旧布局按 STAT0/DATA 清错，USART5 通过 INTC 写一清除。 */
static inline void gd32_usart_clear_rx(uint32_t reg)
{
    (void)gd32_usart_status(reg);
    (void)usart_data_receive(reg);
    if (reg == USART5) {
        USART5_INTC(reg) = USART5_INTC_PEC | USART5_INTC_FEC | USART5_INTC_NEC | USART5_INTC_OREC |
                           USART5_INTC_IDLEC;
    }
}

#endif
