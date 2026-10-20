#ifndef DP_POINTS_H
#define DP_POINTS_H

#include <stddef.h>
#include <stdint.h>
#include <data_process.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#define DP_POINT_WORDS CONFIG_DP_POINT_WORDS
#define DP_BATCH_POINTS 12
#define DP_TRANSFER_WORDS 32

struct dp_key {
    uint16_t dev_id;
    uint16_t sig_id;
};

enum dp_value_type { DP_U16, DP_S16, DP_U32, DP_S32, DP_WORDS, DP_U8 };
enum dp_source { DP_SOURCE_CONSTANT, DP_SOURCE_LOCAL, DP_SOURCE_MODBUS,
                  DP_SOURCE_PARAMETER };
/* Numeric values in the store are low word first, host endian. */
enum dp_word_order { DP_LOW_WORD_FIRST, DP_HIGH_WORD_FIRST,
                     DP_LOW_WORD_BYTE_SWAP, DP_HIGH_WORD_BYTE_SWAP };
static inline uint16_t dp_order_word(const uint16_t *words, size_t width,
                                     size_t index, enum dp_word_order order)
{
    uint16_t v = words[(order & 1) ? width - 1 - index : index];
    return (order & 2) ? (uint16_t)((v << 8) | (v >> 8)) : v;
}
enum dp_quality {
    DP_QUALITY_GOOD,
    DP_QUALITY_UNINITIALIZED,
    DP_QUALITY_COMM_ERROR,
    DP_QUALITY_STALE,
    DP_QUALITY_BAD_VALUE,
};
#define DP_QUALITY_SIMULATED 0x0100U

/* Host-endian register words. Numeric 32-bit values are LOW word first here.
 * DP_WORDS is an opaque sequence (strings use high byte then low byte).
 */
struct dp_value {
    uint16_t words[DP_POINT_WORDS];
};

struct dp_point_cfg;
struct dp_process_ctx;
typedef int (*dp_handler_fn)(struct dp_process_ctx *ctx, struct dp_key key);
/* Fast, nonblocking, pure transformation. Called by the process thread while
 * the store is locked; must not call point APIs or perform I/O. Range checking
 * is applied to the transformed numeric value. Return < 0 to reject a sample.
 */
typedef int (*dp_process_fn)(const struct dp_point_cfg *cfg,
                             struct dp_value *value,
                             const struct dp_value *old_value);

struct dp_point_cfg {
    struct dp_key key;
    enum dp_source source;
    enum dp_value_type type;
    uint8_t width;
    uint32_t stale_ms; /* 0 means no age limit; constants never expire. */
    int64_t minimum;
    int64_t maximum;
    struct dp_value initial; /* Constants and initialized parameters start valid. */
    dp_process_fn process;
    /* Parameter source only. Write preparation is pure, nonblocking and must
     * not call parameter APIs. Forwarded values are absolute/idempotent settings.
     */
    uint8_t param_flags;
    uint16_t write_address;
    enum dp_word_order write_order;
    dp_process_fn prepare_write;
    /* Explicit one-generation additive NVS migration. The only accepted older
     * schema is the exact signature with these entries omitted. Missing values
     * use initial, optionally seeded from a read-only legacy store at mount.
     * seed_missing may perform I/O, returns 0 or -errno; never reenter params.
     */
    bool param_added;
    int (*seed_missing)(const struct dp_point_cfg *cfg, struct dp_value *value);
    /* Transaction callback; use dp_txn_* accessors, never reenter the engine. */
    dp_handler_fn handler;
    bool read_callback;
    /* Reference byte interface: zero uses the native numeric/storage width. */
    uint8_t size_bytes;
    uint32_t reference_data_type;
    DM_PF_SIG_PROC reference_handler;
};

struct dp_device_cfg {
    uint16_t dev_id;
    uint8_t unit_id;
    uint8_t bus_id;
    uint16_t dev_code;
};

struct co_field {
    uint16_t sig_id;
    uint8_t offset;
    uint8_t width;
    enum dp_word_order order;
};

/* Explicit contiguous requests: no hidden merge across undocumented gaps. */
struct co_cfg {
    uint16_t dev_id;
    uint8_t function; /* 3 or 4 */
    uint16_t address; /* Raw wire address, never implicitly +/- 1. */
    uint16_t count;
    uint32_t period_ms;
    const struct co_field *fields;
    size_t field_count;
    uint8_t priority; /* 0..3; oldest due request wins at equal priority. */
    bool init_only;
    bool model_protocol;
    uint16_t model_id; /* UPP model identity; address is the original model offset. */
};

enum dp_map_kind { DP_MAP_VALUE, DP_MAP_QUALITY, DP_MAP_ZERO };
struct dp_transmit_cfg {
    uint8_t function;
    uint16_t address;
    uint16_t count;
    struct dp_key key;
    enum dp_map_kind kind;
    enum dp_word_order order;
};

struct dp_equip_cfg {
    const struct dp_point_cfg *points;
    size_t point_count;
    const struct dp_device_cfg *devices;
    size_t device_count;
    const struct co_cfg *collect;
    size_t collect_count;
    const struct dp_transmit_cfg *transmit;
    size_t transmit_count;
    /* Legacy overlapping maps require an exact start + complete point read. */
    bool exact_overlapping_reads;
    /* Optional original catalogs may reside entirely in flash. */
    const DEV_TABLE_S *reference_devices;
    size_t reference_device_count;
    /* Explicitly authorized previous NVS schema for size-mismatch conversion.
     * Zero rejects unknown schemas as before. Pending intents never migrate.
     */
    uint32_t previous_parameter_schema;
};

struct dp_point_update {
    struct dp_key key;
    struct dp_value value;
    int error; /* Nonzero transport error invalidates the previous value. */
    uint8_t width;
};

struct dp_point_batch {
    int64_t sampled_at;
    uint32_t ticket; /* Assigned by publish, consumed in FIFO order. */
    enum dp_source source;
    uint16_t flags; /* DP_QUALITY_SIMULATED or 0 */
    uint8_t count;
    struct dp_point_update points[DP_BATCH_POINTS];
};

struct dp_point_state {
    struct dp_value value;
    int64_t sampled_at;
    uint32_t generation;
    int error;
    uint16_t quality; /* Quality enum in low byte, provenance flags above. */
};

/* Singleton equipment store; configuration has static lifetime. Init before
 * publishing/starting collectors. Initialization validates all three tables.
 */
int dp_validate(const struct dp_equip_cfg *cfg);
int dp_init_config(const struct dp_equip_cfg *cfg);
/* Local telemetry input, from threads only (-EWOULDBLOCK in an ISR).
 * Words use the dp_value layout, not the reference platform's byte-buffer ABI.
 * The single-point form reports no acquisition error; the batch form can report
 * failed acquisitions. Only DP_SOURCE_LOCAL points are writable through these
 * APIs; parameter writes and transaction callback outputs use their own APIs.
 *
 * count is 1..DP_BATCH_POINTS; each width must match its configured point.
 * Validate the whole batch before enqueueing one copied message. Return 0 means
 * accepted, not processed: timeout bounds enqueueing only. Input memory can be
 * reused after return. If ticket is non-NULL, clear it on entry and fill it only
 * on success. Wait with dp_wait_processed(), then inspect dp_read() quality/error
 * for processing results. Pure transforms and SAMPLE handlers run in DP.
 */
int dp_submit_sig_value(struct dp_key key, const uint16_t *words, size_t word_count,
                     k_timeout_t timeout, uint32_t *ticket);
int dp_submit_sig_values(const struct dp_point_update *values, size_t count,
                      k_timeout_t timeout, uint32_t *ticket);
/* General acquisition ingress, also used by collect for remote samples. */
int dp_publish(struct dp_point_batch *batch, k_timeout_t timeout);
int dp_wait_processed(uint32_t ticket, k_timeout_t timeout);
int dp_read(struct dp_key key, struct dp_point_state *state);
bool dp_healthy(uint32_t max_idle_ms);
/* Immutable configuration and definitions are available without DP_PROCESS. */
const struct dp_equip_cfg *dp_config(void);
const struct dp_point_cfg *dp_find(struct dp_key key);
/* Use normal subscribers/listeners for this coalescible notification channel.
 * Acquisition channel and its message subscriber are internal; publish through
 * dp_publish() so queue capacity is reserved before zbus allocation.
 */
ZBUS_CHAN_DECLARE(dp_processed_chan);
struct dp_processed_event {
    uint32_t generation;
    uint16_t changed;
};

#endif
