#include <iap/iap.h>
#include <zephyr/storage/flash_map.h>
#include "iap_keys.h"

#define STAGING DT_CHOSEN(iap_staging)
#define JOURNAL DT_CHOSEN(iap_journal)
#define RAM DT_CHOSEN(zephyr_sram)
#define PRIMARY DT_NODELABEL(slot0_partition)

#ifdef CONFIG_IAP_REBOOT_DP_PARAMS
#include <dataprocess/params.h>
static int prepare_reboot(void *user)
{
    ARG_UNUSED(user);
    return dp_params_prepare_reboot();
}
static void cancel_reboot(void *user)
{
    ARG_UNUSED(user);
    dp_params_cancel_reboot();
}
#endif

int iap_start(void)
{
    const struct iap_config config = {
        .staging_area_id = DT_FIXED_PARTITION_ID(STAGING),
        .journal_area_id = DT_FIXED_PARTITION_ID(JOURNAL),
        .aes_key = iap_aes_key,
        .image = {
            .max_size = CONFIG_IAP_IMAGE_MAX_SIZE,
            .load_address = DT_FIXED_PARTITION_ADDR(PRIMARY),
            .sram_start = DT_REG_ADDR(RAM),
            .sram_end = DT_REG_ADDR(RAM) + DT_REG_SIZE(RAM),
            .public_key = iap_public_key,
            .key_hash = iap_key_hash,
        },
#ifdef CONFIG_IAP_REBOOT_DP_PARAMS
        .prepare_reboot = prepare_reboot,
        .cancel_reboot = cancel_reboot,
#endif
    };
    return iap_init(&config);
}
