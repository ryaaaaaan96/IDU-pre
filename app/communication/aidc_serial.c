#include "communication/idu_protocol.h"
#include "cfg/inc/imported_signals.h"
#include <dataprocess/params.h>
#include <zephyr/modbus/modbus.h>
#include <errno.h>

static uint16_t parameter(uint16_t sig, uint16_t fallback)
{
    struct dp_point_state state;
    return dp_params_read((struct dp_key){247,sig},&state) ? fallback : state.value.words[0];
}

int idu_aidc_serial(struct modbus_iface_param *param)
{
    static const uint32_t baud[]={9600,19200,38400,57600,115200,230400,460800};
    uint16_t b=parameter(AIDC_LOCAL_SIG_UART_PORT1_BAUD,165);
    uint16_t parity=parameter(AIDC_LOCAL_SIG_UART_PORT1_PARITY,85);
    uint16_t stop=parameter(AIDC_LOCAL_SIG_UART_PORT1_STOP_BITS,161);
    if (b<161 || b>167 || (parity!=85 && parity!=161 && parity!=162) || (stop!=161 && stop!=162)) { return -ERANGE; }
    param->serial.baud=baud[b-161];
    param->serial.parity=parity==85 ? UART_CFG_PARITY_NONE : parity==161 ? UART_CFG_PARITY_ODD : UART_CFG_PARITY_EVEN;
    param->serial.stop_bits=stop==161 ? UART_CFG_STOP_BITS_1 : UART_CFG_STOP_BITS_2;
    param->server.unit_id=parameter(AIDC_LOCAL_SIG_TR_ADDR,1);
    return 0;
}
