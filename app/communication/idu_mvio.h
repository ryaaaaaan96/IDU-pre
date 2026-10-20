#ifndef IDU_MVIO_H
#define IDU_MVIO_H
#include <dataprocess/points.h>
int idu_mvio_attach(int iface);
int idu_mvio_seed_address(const struct dp_point_cfg *cfg, struct dp_value *value);
int idu_mvio_prepare_address(const struct dp_point_cfg *cfg, struct dp_value *value,
                             const struct dp_value *old);
#endif
