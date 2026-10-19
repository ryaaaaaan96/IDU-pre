#ifndef IDU_EQUIP_INSTANCES_H
#define IDU_EQUIP_INSTANCES_H

/* One row binds a reusable model to a logical device, transport and northbound
 * windows. The process/collect/transmit tables and device registry consume the
 * same list. IDs and windows must be unique (checked by dp_validate).
 *
 * X(dev, bus, unit, north_base, address_reg,
 *   sample_period, identity_period, sample_stale, identity_stale)
 *
 * The default device is a simulated MV-IO peer, not the local physical inputs.
 * Add real peers only after configuring their bus backend and register contract.
 */
#define IDU_MVIO_INSTANCES(X) \
    X(IDU_DEV_REMOTE_IO, 0, 1, 10000, 30460, 500, 5000, 2000, 15000)

#endif
