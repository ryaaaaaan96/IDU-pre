#ifndef IAP_H
#define IAP_H

#include <iap/sgz.h>
#include <errno.h>
#include <stdbool.h>

struct modbus_adu;

struct iap_config {
    uint8_t staging_area_id;
    uint8_t journal_area_id;
    const uint8_t *aes_key; /* 16-byte legacy transport interoperability key */
    struct iap_image_policy image;
    /* Optional paired hooks, called in the IAP thread after verification.
     * prepare must make application state durable and freeze mutations;
     * on error it must undo its own partial work. cancel follows only a
     * successful prepare if committing the journal/upgrade then fails. */
    int (*prepare_reboot)(void *user);
    void (*cancel_reboot)(void *user);
    void *user;
};

/* Single receiver. Initialize once during application startup, before routing.
 * Config is copied; keys and user context must remain valid for its lifetime.
 * Failed initialization releases flash handles and can be retried.
 * Uses MCUboot slot0/slot1 and a 4 KiB erase unit for staging and NVS.
 * Reboot health confirmation remains the application's responsibility. */
int iap_init_config(const struct iap_config *config);
bool iap_ready(void);
/* CONFIG_IAP_CONFIGURED: same explicit initialization, using devicetree, generated
 * verification/transport keys and the selected reboot policy. */
int iap_init(void);

/* Caller supplies an already unit-filtered complete Modbus ADU, including
 * legacy oversized FC16 packets. Return 0 to reply, -ENOENT for an unowned address,
 * IAP_ROUTE_SILENT to consume without a reply. No transport is registered here.
 * Interface IDs must be nonnegative. Multiple ports share one upload owner.
 * When registered in a PRO_ADDR_SLAVE table, TR selects by start address and
 * treats -ENOENT as an address error, never falling through to point writes. */
#define IAP_ROUTE_SILENT (-ECANCELED)
int iap_route(int iface, const struct modbus_adu *request, struct modbus_adu *response);

#endif
