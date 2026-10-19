#include "idu_pre.h"
#include "idu_equip.h"
#include "comm_cfg.h"
#include <transmit/modbus_adapter.h>
#include <transmit/transmit.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(idu_protocol, LOG_LEVEL_INF);
static int interface = -1;
static bool configured, changing;
static struct modbus_iface_param active, pending;
K_MUTEX_DEFINE(protocol_lock);

static void changed(int iface, int error, void *user)
{
    ARG_UNUSED(iface); ARG_UNUSED(user);
    k_mutex_lock(&protocol_lock,K_FOREVER);
    if (!error) { active=pending; }
    if (error == -ENOTRECOVERABLE) { configured=false; }
    changing=false;
    k_mutex_unlock(&protocol_lock);
    if (error) { LOG_ERR("Serial configuration failed: %d",error); }
}

int idu_protocol_set_address(uint8_t address)
{
    if (address < 1 || address > 16) { return -EINVAL; }
    const struct idu_protocol_profile *profile = idu_protocol_config();
    k_mutex_lock(&protocol_lock,K_FOREVER);
    if (interface < 0) {
        interface=modbus_iface_get_by_name(DEVICE_DT_NAME(DT_ALIAS(idu_forward)));
        if (interface < 0) { k_mutex_unlock(&protocol_lock); return interface; }
        int attach_err = profile->attach ? profile->attach(interface) : tr_attach(interface, NULL);
        if (attach_err) { interface = -1; k_mutex_unlock(&protocol_lock); return attach_err; }
    }
    struct modbus_iface_param param={.mode=MODBUS_MODE_RTU,
        .server={.user_cb=idu_parameter_callbacks(),.unit_id=address},
        .serial={.baud=DT_PROP(DT_PARENT(DT_ALIAS(idu_forward)),current_speed),
                 .parity=UART_CFG_PARITY_NONE,.stop_bits=UART_CFG_STOP_BITS_1}};
    int err = profile->serial ? profile->serial(&param) : 0;
    if (!err && !configured) {
        err=tr_modbus_init_server(interface,param,idu_parameter_write,true);
        if (!err) { active=param; configured=true; }
    } else if (!err && !changing && (active.server.unit_id!=param.server.unit_id ||
                active.serial.baud!=param.serial.baud || active.serial.parity!=param.serial.parity ||
                active.serial.stop_bits!=param.serial.stop_bits)) {
        pending=param;
        err=tr_modbus_reconfigure(interface,param,changed,NULL);
        changing=!err;
        if (err == -EBUSY) { err = 0; } /* A recovery owns the port; retry next sample. */
    }
    k_mutex_unlock(&protocol_lock);
    /* Legacy idle/TX recovery interval: 2000 ten-millisecond ticks. */
    return err ? err : tr_modbus_service(interface, 20000);
}
