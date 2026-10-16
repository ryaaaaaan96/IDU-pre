#ifndef IDU_POINTS_H
#define IDU_POINTS_H

#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/zbus/zbus.h>

#define IDU_POINT_WORDS CONFIG_IDU_POINT_WORDS
#define IDU_BATCH_POINTS 12
#define IDU_POLL_REGS 32

struct idu_key {
    uint16_t dev_id;
    uint16_t sig_id;
};

enum idu_value_type { IDU_U16, IDU_S16, IDU_U32, IDU_S32, IDU_WORDS, IDU_U8 };
enum idu_source { IDU_SOURCE_CONSTANT, IDU_SOURCE_LOCAL, IDU_SOURCE_MODBUS,
                  IDU_SOURCE_PARAMETER };
/* Numeric values in the store are low word first, host endian. */
enum idu_word_order { IDU_LOW_WORD_FIRST, IDU_HIGH_WORD_FIRST,
                     IDU_LOW_WORD_BYTE_SWAP, IDU_HIGH_WORD_BYTE_SWAP };
static inline uint16_t idu_order_word(const uint16_t *words, size_t width,
                                     size_t index, enum idu_word_order order)
{
    uint16_t v = words[(order & 1) ? width - 1 - index : index];
    return (order & 2) ? (uint16_t)((v << 8) | (v >> 8)) : v;
}
enum idu_quality {
    IDU_QUALITY_GOOD,
    IDU_QUALITY_UNINITIALIZED,
    IDU_QUALITY_COMM_ERROR,
    IDU_QUALITY_STALE,
    IDU_QUALITY_BAD_VALUE,
};
#define IDU_QUALITY_SIMULATED 0x0100U

/* Host-endian register words. Numeric 32-bit values are LOW word first here.
 * IDU_WORDS is an opaque sequence (strings use high byte then low byte).
 */
struct idu_value {
    uint16_t words[IDU_POINT_WORDS];
};

struct idu_point_cfg;
struct idu_process_ctx;
typedef int (*idu_handler_fn)(struct idu_process_ctx *ctx, struct idu_key key);
/* Fast, nonblocking, pure transformation. Called by the process thread while
 * the store is locked; must not call point APIs or perform I/O. Range checking
 * is applied to the transformed numeric value. Return < 0 to reject a sample.
 */
typedef int (*idu_process_fn)(const struct idu_point_cfg *cfg,
                             struct idu_value *value,
                             const struct idu_value *old_value);

struct idu_point_cfg {
    struct idu_key key;
    enum idu_source source;
    enum idu_value_type type;
    uint8_t width;
    uint32_t stale_ms; /* 0 means no age limit; constants never expire. */
    int64_t minimum;
    int64_t maximum;
    struct idu_value initial; /* Constants and initialized parameters start valid. */
    idu_process_fn process;
    /* Parameter source only. Write preparation is pure, nonblocking and must
     * not call parameter APIs. Forwarded values are absolute/idempotent settings.
     */
    uint8_t param_flags;
    uint16_t write_address;
    enum idu_word_order write_order;
    idu_process_fn prepare_write;
    /* Explicit one-generation additive NVS migration. The only accepted older
     * schema is the exact signature with these entries omitted. Missing values
     * use initial, optionally seeded from a read-only legacy store at mount.
     * seed_missing may perform I/O, returns 0 or -errno; never reenter params.
     */
    bool param_added;
    int (*seed_missing)(const struct idu_point_cfg *cfg, struct idu_value *value);
    /* Transaction callback; use idu_txn_* accessors, never reenter the engine. */
    idu_handler_fn handler;
    bool read_callback;
};

struct idu_device_cfg {
    uint16_t dev_id;
    uint8_t unit_id;
    uint8_t bus_id;
};

struct idu_collect_field {
    uint16_t sig_id;
    uint8_t offset;
    uint8_t width;
    enum idu_word_order order;
};

/* Explicit contiguous requests: no hidden merge across undocumented gaps. */
struct idu_collect_cfg {
    uint16_t dev_id;
    uint8_t function; /* 3 or 4 */
    uint16_t address; /* Raw wire address, never implicitly +/- 1. */
    uint16_t count;
    uint32_t period_ms;
    const struct idu_collect_field *fields;
    size_t field_count;
    uint8_t priority; /* 0..3; oldest due request wins at equal priority. */
    bool init_only;
    bool model_protocol;
    uint16_t model_id; /* UPP model identity; address is the original model offset. */
};

enum idu_map_kind { IDU_MAP_VALUE, IDU_MAP_QUALITY, IDU_MAP_ZERO };
struct idu_transmit_cfg {
    uint8_t function;
    uint16_t address;
    uint16_t count;
    struct idu_key key;
    enum idu_map_kind kind;
    enum idu_word_order order;
};

struct idu_equip_cfg {
    const struct idu_point_cfg *points;
    size_t point_count;
    const struct idu_device_cfg *devices;
    size_t device_count;
    const struct idu_collect_cfg *collect;
    size_t collect_count;
    const struct idu_transmit_cfg *transmit;
    size_t transmit_count;
    /* Legacy overlapping maps require an exact start + complete point read. */
    bool exact_overlapping_reads;
};

struct idu_point_update {
    struct idu_key key;
    struct idu_value value;
    int error; /* Nonzero transport error invalidates the previous value. */
    uint8_t width;
};

struct idu_point_batch {
    int64_t sampled_at;
    uint32_t ticket; /* Assigned by publish, consumed in FIFO order. */
    enum idu_source source;
    uint16_t flags; /* IDU_QUALITY_SIMULATED or 0 */
    uint8_t count;
    struct idu_point_update points[IDU_BATCH_POINTS];
};

struct idu_point_state {
    struct idu_value value;
    int64_t sampled_at;
    uint32_t generation;
    int error;
    uint16_t quality; /* Quality enum in low byte, provenance flags above. */
};

/* Singleton equipment store; configuration has static lifetime. Init before
 * publishing/starting collectors. Initialization validates all three tables.
 */
int idu_points_validate(const struct idu_equip_cfg *cfg);
int idu_points_init(const struct idu_equip_cfg *cfg);
int idu_points_publish(struct idu_point_batch *batch, k_timeout_t timeout);
int idu_points_wait_processed(uint32_t ticket, k_timeout_t timeout);
int idu_points_read(struct idu_key key, struct idu_point_state *state);
bool idu_points_healthy(uint32_t max_idle_ms);
int idu_points_read_register(uint8_t function, uint16_t address, uint16_t *value);
struct modbus_user_callbacks *idu_forward_callbacks(void);

/* One independent acquisition bus, multiple slave devices. Polling must run
 * in a thread, never the system workqueue used by Zephyr Modbus receive work.
 */
int idu_collect_init(const struct idu_equip_cfg *cfg, int iface, bool simulated);
int idu_collect_poll(size_t block);
int idu_collect_start(void);

/* Use normal subscribers/listeners for this coalescible notification channel.
 * Acquisition channel and its message subscriber are internal; publish through
 * idu_points_publish() so queue capacity is reserved before zbus allocation.
 */
ZBUS_CHAN_DECLARE(idu_processed_chan);
struct idu_processed_event {
    uint32_t generation;
    uint16_t changed;
};

#endif
