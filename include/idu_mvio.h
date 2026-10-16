#ifndef IDU_MVIO_H
#define IDU_MVIO_H
#include <idu/points.h>
int idu_mvio_attach(int iface);
int idu_mvio_seed_address(const struct idu_point_cfg *cfg, struct idu_value *value);
int idu_mvio_prepare_address(const struct idu_point_cfg *cfg, struct idu_value *value,
                             const struct idu_value *old);
#endif
