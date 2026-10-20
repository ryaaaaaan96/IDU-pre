#ifndef DP_REFERENCE_INTERNAL_H
#define DP_REFERENCE_INTERNAL_H
#include <dataprocess/process.h>
#include <platform_callback.h>

size_t dp_reference_size(const struct dp_point_cfg *point);
void dp_reference_pack(uint8_t *bytes, const struct dp_value *value, size_t size);
void dp_reference_unpack(struct dp_value *value, const uint8_t *bytes, size_t size);
int dp_reference_range(const struct dp_point_cfg *point, const struct dp_value *value);
int dp_reference_default(const struct dp_point_cfg *point, struct dp_value *value, bool policy);
void dp_reference_notify(const struct dp_point_cfg *point, const struct dp_value *old,
                         const struct dp_value *value);
void dp_reference_drain_events(void);
struct dp_process_ctx *dp_reference_context(void);
const struct dp_process_request *dp_process_current_request(void);
int dp_reference_finish(struct dp_process_ctx *ctx, int error);
int dp_store_reset_device(uint16_t dev_id);
int dp_reference_restore(bool factory);
int dp_reference_collect(PARA_DATA_S *data);

#define DP_FINISH_FLUSH BIT(0)
#define DP_FINISH_REBOOT BIT(1)
#define DP_FINISH_DEFAULTS BIT(2)
#define DP_FINISH_FACTORY BIT(3)
#endif
