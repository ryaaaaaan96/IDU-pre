#include "protocol_config.h"
#if defined(CONFIG_IDU_AIDC_PROFILE)
#include "communication/idu_protocol.h"
#else
#include "communication/idu_mvio.h"
#endif

/* Project-specific protocol callbacks belong to the communication layer.
 * The port implementation applies these static bindings. */
const struct idu_protocol_profile *idu_protocol_config(void)
{
    static const struct idu_protocol_profile profile = {
#if defined(CONFIG_IDU_AIDC_PROFILE)
        .serial = idu_aidc_serial,
#else
        .attach = idu_mvio_attach,
#endif
    };
    return &profile;
}
