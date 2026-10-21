#include "process/idu_process.h"
#include "protocol/inc/protocol.h"
#include "protocol/inc/imported_signals.h"
#include <dataprocess/params.h>
#include <zephyr/modbus/modbus.h>
#include <errno.h>

static uint16_t parameter(uint16_t sig, uint16_t fallback)
{
    struct dp_point_state state;
    int err = dp_params_read((struct dp_key){247, sig}, &state);

    return err ? fallback : state.value.words[0];
}

int idu_forwarding_settings(struct modbus_iface_param *param)
{
    /* 参考点表保存协议编码；仅在业务边界转换成 Zephyr 串口配置。 */
    static const uint32_t baud_rates[] = {
        9600, 19200, 38400, 57600, 115200, 230400, 460800,
    };
    uint16_t baud = parameter(AIDC_LOCAL_SIG_UART_PORT1_BAUD, 165);
    uint16_t parity = parameter(AIDC_LOCAL_SIG_UART_PORT1_PARITY, 85);
    uint16_t stop = parameter(AIDC_LOCAL_SIG_UART_PORT1_STOP_BITS, 161);

    if (baud < 161 || baud > 167 ||
        (parity != 85 && parity != 161 && parity != 162) ||
        (stop != 161 && stop != 162)) {
        return -ERANGE;
    }
    struct modbus_serial_param serial = {.baud = baud_rates[baud - 161]};
    serial.parity = parity == 85 ? UART_CFG_PARITY_NONE :
                          parity == 161 ? UART_CFG_PARITY_ODD : UART_CFG_PARITY_EVEN;
    serial.stop_bits = stop == 161 ? UART_CFG_STOP_BITS_1 : UART_CFG_STOP_BITS_2;
    if (param->mode == MODBUS_MODE_RAW) {
        int err = idu_forward_serial_configure(serial);
        if (err) { return err; }
    } else { param->serial = serial; }
    param->server.unit_id = parameter(AIDC_LOCAL_SIG_TR_ADDR, 1);
    return 0;
}
