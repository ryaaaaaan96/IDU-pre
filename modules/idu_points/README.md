# IDU point tables

Zephyr 4.4.2 module for equipment collection, processing, parameter storage and
Modbus forwarding. See [IDU-pre integration and migration status](../../docs/point-tables.md#module-extension).
No legacy platform library is linked.

## Integration

Append this directory to `ZEPHYR_EXTRA_MODULES` before `find_package(Zephyr ...)`.
Enable `IDU_POINTS`, both Modbus roles, and `IDU_PARAMS` for business transactions.
Supply static `idu_equip_cfg` tables. Initialize the point store, register every
acquisition bus, initialize/recover parameter storage, run process initialization,
and start collectors. Configure northbound interfaces with `idu_forward_attach()`
and `idu_modbus_init_server(..., true)` for independent forwarding workqueues.
The system workqueue remains available for southbound Modbus receive work.

Use `idu_points_publish()` for local/acquired samples. Publication queues a copied
zbus message; completion is observable through state generations or
`idu_processed_chan`. Constants start valid, other telemetry starts unavailable.
Numeric values use low-word-first host-endian storage. Collection and forwarding
support both word orders and optional byte swapping within each word.

## Process and parameter contracts

`process` is the original pure, nonblocking value transform under the store lock.
`handler` is the transaction callback in `idu/process.h`: WRITE, READ (when
`read_callback` is enabled), INIT, TIMER and successful SAMPLE operations. It runs
outside the store lock and must use `idu_txn_get`, `idu_txn_get_state`,
`idu_txn_contains` and `idu_txn_emit`. All original inputs are visible before any
handler runs. Outputs do not recursively invoke handlers. Reentry is rejected.
An invalid sample batch does not invoke transaction callbacks. Sample provenance
is retained in derived telemetry; a read of a dependency does not by itself prove
that its value is valid, so check its state when availability matters.

`idu_process_execute()` returns a result; `idu_process_submit()` copies its request
and invokes the supplied completion exactly once after acceptance, including on
failure/busy. The completion pointer is valid only during the callback. Jobs run
on a dedicated thread. Read context contains the source interface, function,
register address and count. Per-device INIT, one-shot and periodic timers are
available. Busy timers retry; timers are scheduling callbacks, not hard realtime.

Independent per-output flags:

| Flag | Effect |
| --- | --- |
| `IDU_OP_NO_COLLECT` | Update the data pool/storage without downstream writes |
| `IDU_OP_NO_DM` | Downstream write only; leave the cached and durable value unchanged |
| `IDU_OP_NO_STORE` | Update the cache/downstream, but not its durable value |
| `IDU_OP_DEFER` | Defer the durable value until the 1 s debounce or explicit flush |

For a cache-only update combine NO_COLLECT and NO_STORE. Point attributes declare
WRITE/PERSIST/FORWARD/RESTORE/DEFER_STORE independently. NO_STORE changes cannot
leak into a later unrelated snapshot. A deferred durable value is kept separately
from transient cache updates. `idu_process_save_reboot(delay_ms)` requests a flush
then reboot; a failed flush prevents reboot and is visible in parameter status.
Call it after a successful transaction/completion when a command requires reboot.

All emitted outputs receive type/range/permission validation before commit.
Commit groups are per device and flags. They are **not** a distributed atomic
transaction: a later group can fail after an earlier one succeeded. The result
lists successful, failed and unattempted (`-ECANCELED`) outputs. Pure parameter
`prepare_write` callbacks and downstream checks run within their commit group.
The lower-level `idu_params_*` setters intentionally bypass business handlers;
use the process API or `idu_forward_write` for normal application writes.

A forwarded parameter group must be a contiguous set of readable, idempotent
absolute settings on one device. A durable intent pins device/unit/bus, precedes
I/O, and stores old/new wire images. Readback resolves lost acknowledgements;
recovery never replays a write. Even NO_STORE downstream operations use this
journal, while the final durable parameter values remain unchanged. Flash access
failure or unresolved intent blocks further parameter writes until reconciled.
Unknown storage schemas are rejected without erasing data. Explicit `param_added`
entries allow exactly one additive schema transition: the previous signature must
match all other parameter definitions unchanged. `seed_missing` runs only for
missing values during mount; it may read a legacy store, but cannot reenter the
parameter API. Its result is validated and committed as a complete NVS snapshot.
An existing pending intent is validated first and reconciled by readback before
the upgraded snapshot is committed. A new empty partition also saves one initial
snapshot, preventing repeated legacy import on subsequent boots.

## Collection and forwarding

Each registered bus owns a mutex, worker, queue and mode. POLL performs periodic
collection. INIT performs backend initialization, initialization-only reads,
process callbacks and desired-parameter synchronization. UPDATE/IV/FAULT/
PASSTHROUGH select matching queued requests; SLEEP pauses all I/O. Ordinary
parameter reads/writes require POLL or the owning INIT context. Use
`idu_collect_transfer_mode()` for a mode-specific service. These modes provide
scheduling ownership; they do not implement IAP/IV/fault-record payload protocols.

Queued requests and due polls use priority 0..3 plus aging (1 s advantage per
level). An in-flight request is never preempted. Direct synchronous transactions
serialize on the bus but do not enter the priority queue. Cancellation completes
queued requests once with `-ECANCELED`; an already running request finishes.
Device status includes bus, simulated flag, UNKNOWN/ONLINE/OFFLINE/EXCEPTION,
counters and last successful response. `idu_device_events` is a coalescible event.

`idu_forward_routes` supports whole address segments with a callback or downstream
address remapping. Requests crossing a segment boundary are rejected. Read-only
03/04 and whole-point 06/16 maps remain available. Modbus passthrough uses a
registered bus backend; this is not a raw UART byte tunnel or a UPP implementation.
Routes have static lifetime and are attached before receiving requests.

`idu_modbus_reconfigure()` copies settings and applies them on a control workqueue.
It drains/plugs forwarding work, waits for old TX completion, then disables and
reinitializes the interface. New requests during this transition may need retry.
A failed configuration restores the previous settings; failure of that recovery
returns `-ENOTRECOVERABLE`. Completion reports actual application of the settings.
New submissions are gated while waiting; a handler that does not finish within
2 s produces `-ETIMEDOUT` instead of blocking the control queue in drain/cancel.
`idu_modbus_recover()` can reset a backend whose TX-complete event was lost.
`idu_modbus_service(iface, timeout_ms)` schedules idle recovery and reports a
stuck handler to the board health monitor. `idu_modbus_get_health()` reports the
number of recovery attempts, last error and configuration-in-progress state.
Southbound `idu_collect_reconfigure()` requires an idle bus and leaves it asleep
on failure; the caller must reconfigure successfully before resuming it.

## Bounds and compatibility

Defaults: 64 points, 512 total value words, 16 polling blocks, 2 buses, 16 devices,
12 points per sample, 16 words per point, 24 transaction entries, 32 registers per
collection/write and 2 isolated forwarding ports. Kconfig bounds all storage.
The value pool allocates each point's actual width and is validated at startup.
Transaction entries include captured dependencies as well as emitted outputs.

The AIDC example increases point width to 32 words and changes these bounds for
its 552-point profile. `ZBUS_MSG_SUBSCRIBER_NET_BUF_STATIC_DATA_SIZE` must cover
`sizeof(idu_point_batch)` despite isolated pools in Zephyr 4.4.2. Tables are static;
lookup is linear. Measure timing and stack margins on the target before release.

NVS requires FLASH, FLASH_MAP, FLASH_PAGE_LAYOUT and NVS_DATA_CRC. Reserve at least
three equal erase sectors. The example uses four 4 KB sectors per profile.
Parameter and device events are notifications; state/result APIs are authoritative.

`compat/prepare_modbus.py` generates build-local copies of three reviewed Zephyr
4.4.2 sources for whole-PDU routing, isolated RX scheduling and one-byte RAW
exceptions. The west checkout is unchanged. Strict source hashes intentionally
stop builds after a Zephyr upgrade until this adapter has been reviewed and tested.

Per-interface `idu_modbus_set_policy()` optionally replaces the unit filter after
CRC/RAW validation and permits legacy broadcast routing/replies. A route returns
`IDU_MODBUS_SILENT` to consume a frame without replying. With no policy, standard
unit filtering and broadcast behavior remain unchanged. Only the MV-IO app
installs its 247/broadcast rules; they are not enabled by the AIDC profile.
