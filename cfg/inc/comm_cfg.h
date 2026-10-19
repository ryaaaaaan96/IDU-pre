#ifndef IDU_COMM_CFG_H
#define IDU_COMM_CFG_H

#include <stddef.h>
#include <stdint.h>

struct modbus_iface_param;

/* Static project bindings, selected before a northbound port is started. */
struct idu_protocol_profile {
    int (*attach)(int iface); /* NULL selects ordinary transmit table routing. */
    int (*serial)(struct modbus_iface_param *param); /* Optional parameter override. */
};

struct idu_parameter_command {
    uint16_t address;
    uint16_t expected_value;
    uint16_t dev_id;
    int (*execute)(uint16_t dev_id);
};

enum idu_parameter_status_field {
    IDU_PARAMETER_FLAGS,
    IDU_PARAMETER_ERROR,
    IDU_PARAMETER_SEQUENCE_LOW,
    IDU_PARAMETER_SEQUENCE_HIGH,
};

struct idu_parameter_status_register {
    uint16_t address;
    enum idu_parameter_status_field field;
};

struct idu_register_range {
    uint16_t first;
    uint16_t last;
};

/* Ordered callback entries: legacy traversal skips gaps between spans. This
 * is deliberately distinct from a contiguous transmit address segment. */
struct idu_legacy_span {
    uint16_t first;
    uint16_t points;
    uint8_t width;
};

struct idu_mvio_protocol_cfg {
    const struct idu_register_range *read_starts;
    size_t read_start_count;
    const struct idu_register_range *write_starts;
    size_t write_start_count;
    const struct idu_register_range *extensions;
    size_t extension_count;
    const struct idu_legacy_span *input;
    size_t input_count;
    const struct idu_legacy_span *holding;
    size_t holding_count;
};

enum idu_mvio_register {
    IDU_MVIO_REG_ADDRESS = 30459,
    IDU_MVIO_REG_IAP_CAPABILITY = 20003,
    IDU_MVIO_REG_IAP_VERSION = 31996,
};

const struct idu_protocol_profile *idu_protocol_config(void);
const struct idu_parameter_command *idu_parameter_command_table(size_t *count);
const struct idu_parameter_status_register *idu_parameter_status_table(size_t *count);
const struct idu_mvio_protocol_cfg *idu_mvio_protocol_config(void);

#endif
