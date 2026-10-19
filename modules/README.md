# dataprocess / collect / transmit

Three reusable Zephyr 4.4.2 modules for data processing, equipment acquisition
and protocol forwarding. See [IDU-pre integration and migration status](../docs/point-tables.md#module-extension).
No legacy platform library is linked.

| Module | Public headers | Responsibility |
| --- | --- | --- |
| `dataprocess` | `dataprocess/points.h`, `dataprocess/process.h`, `dataprocess/params.h` | Shared data contracts/store, `dp_*` processing, optional NVS |
| `collect` | `collect/collect.h` | `co_*` acquisition, bus scheduling and parameter transport backend |
| `transmit` | `transmit/transmit.h`, `transmit/modbus_adapter.h` | `tr_*` register mappings, port modes and Modbus workqueues |

Product configuration lives in [`src/cfg/`](../src/cfg/README.md), outside these
modules: `include/` defines configuration contracts, `equipment/` binds instances,
`tables/` assembles the three tables, `protocol/` declares protocol policy and
commands, and `aidc/` provides generated metadata. Business callbacks remain in
`src/process/`; hardware and transport code remain in their application layers.
Moving the configuration preserves its data and behavior. `dataprocess` has no
Modbus or collector dependency. The other two depend on its data contracts;
downstream forwarding is optional when `collect` is absent.

## Integration

Include `modules.cmake` before `find_package(Zephyr ...)` to register all three,
or append individual module directories to `ZEPHYR_EXTRA_MODULES`.
Enable `DATAPROCESS`; enable `COLLECT` with the Modbus client role and `TRANSMIT`
with the server role as needed. `DP_PROCESS` enables business
transactions, sample/read/init handlers, asynchronous jobs and timers independently
of flash. `DP_PARAMS` adds parameter points, durable transactions and NVS recovery;
parameter tables are rejected without it. With `DP_PROCESS=n`, pure point
transforms and basic forwarding remain available; tables declaring business
handlers or read callbacks are rejected instead of silently ignoring them.
`TR_MODBUS_ADAPTER` enables whole-PDU routing and isolated forwarding workers,
and can be selected without `DP_PARAMS`. The full IDU-pre application selects it.
Supply static `dp_equip_cfg` tables. Call `dp_init()`, register every acquisition
bus with `co_init_bus()`, and initialize/recover parameter storage when enabled.
With `DP_PROCESS`, call `dp_process_start()` to run the declared INIT/TIMER plan
before `co_start()` starts collection. Configure northbound interfaces with `tr_attach()`
and `tr_modbus_init_server(..., true)` for independent forwarding workqueues.
The system workqueue remains available for southbound Modbus receive work.

`dp_params_set_transport()` registers copied read/write/provenance callbacks,
matching the platform's `dp_para_reg_set_func` dependency direction. `collect`
registers its backend on bus initialization. When combining collection with a
different parameter transport, finish all `co_init_bus()` calls first, then
register the custom operations before `dp_params_init()`; later collector
initialization would register its own backend again. The callback registration
is copied, while its user data must remain valid. Replacing the backend after
parameter initialization returns `-EBUSY`; identical registrations are idempotent.
`dp_params_init(cfg, partition_id)` mounts storage without
knowing a Modbus interface. Local parameters work with no transport; forwarded
operations without a backend return `-ENODEV` before any intent/cache effects.

Use `dp_publish()` for local/acquired samples. Publication queues a copied
zbus message; completion is observable through state generations or
`dp_processed_chan`. Constants start valid, other telemetry starts unavailable.
Numeric values use low-word-first host-endian storage. Collection and forwarding
support both word orders and optional byte swapping within each word.

## Process and parameter contracts

`process` is the original pure, nonblocking value transform under the store lock.
`handler` is the transaction callback in `dataprocess/process.h`: WRITE, READ (when
`read_callback` is enabled), INIT, TIMER and successful SAMPLE operations. It runs
outside the store lock and must use `dp_txn_get`, `dp_txn_get_state`,
`dp_txn_contains` and `dp_txn_emit`. All original inputs are visible before any
handler runs. Outputs do not recursively invoke handlers. Reentry is rejected.
An invalid sample batch does not invoke transaction callbacks. Sample provenance
is retained in derived telemetry; a read of a dependency does not by itself prove
that its value is valid, so check its state when availability matters.

`dp_process_execute()` returns a result; `dp_process_submit()` copies its request
and invokes the supplied completion exactly once after acceptance, including on
failure/busy. The completion pointer is valid only during the callback. Jobs run
on a dedicated thread. Read context contains the source interface, function,
register address and count. Per-device INIT, one-shot and periodic timers are
available. Busy timers retry; timers are scheduling callbacks, not hard realtime.

`dp_process_start(const struct dp_process_plan *plan)` is a thread startup API
called after `dp_init()` and optional parameter recovery. Choose either
`initialize_all` or ordered `init_keys`, and optionally supply `dp_timer_spec`
entries. The whole plan is validated and timer slots are reserved before INIT
callbacks run. Plan timers start only after all INIT calls succeed; failure
releases reservations for retry, but does not roll back earlier successful INIT
transactions. A successful start is one-time; another call returns `-EALREADY`.
The plan is consumed during the call. Timer delays and periods are finite,
relative Zephyr timeouts; a `K_NO_WAIT` period means one-shot. Individual timers
can also use `dp_process_timer_start()` / `dp_process_timer_cancel()`; a completed
one-shot releases its slot.

Independent per-output flags:

| Flag | Effect |
| --- | --- |
| `DP_OP_NO_COLLECT` | Update the data pool/storage without downstream writes |
| `DP_OP_NO_DM` | Downstream write only; leave the cached and durable value unchanged |
| `DP_OP_NO_STORE` | Update the cache/downstream, but not its durable value |
| `DP_OP_DEFER` | Defer the durable value until the 1 s debounce or explicit flush |

For a cache-only update combine NO_COLLECT and NO_STORE. Point attributes declare
WRITE/PERSIST/FORWARD/RESTORE/DEFER_STORE independently. NO_STORE changes cannot
leak into a later unrelated snapshot. A deferred durable value is kept separately
from transient cache updates. `dp_process_save_reboot(delay_ms)` requests a flush
then reboot; a failed flush prevents reboot and is visible in parameter status.
Call it after a successful transaction/completion when a command requires reboot.

All emitted outputs receive type/range/permission validation before commit.
Commit groups are per device and flags. They are **not** a distributed atomic
transaction: a later group can fail after an earlier one succeeded. The result
lists successful, failed and unattempted (`-ECANCELED`) outputs. Pure parameter
`prepare_write` callbacks and downstream checks run within their commit group.
The lower-level `dp_params_*` setters intentionally bypass business handlers;
use the process API or `tr_write` for normal application writes.

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
`co_transfer_mode()` for a mode-specific service. These modes provide
scheduling ownership; they do not implement IAP/IV/fault-record payload protocols.

Workers sleep on a per-bus semaphore until a matching request, schedule/mode
change or the next polling deadline. Sleeping buses and buses without periodic
work wait indefinitely. Queue capacity is `CO_QUEUE_DEPTH`; a full queue
returns `-ENOSPC` and does not accept the request. Completion callbacks must remain
bounded because they run on the bus worker (or the cancelling caller).

Queued requests and due polls use priority 0..3 plus aging (1 s advantage per
level). An in-flight request is never preempted. Direct synchronous transactions
serialize on the bus but do not enter the priority queue. Cancellation completes
queued requests once with `-ECANCELED`; an already running request finishes.
Device status includes bus, simulated flag, UNKNOWN/ONLINE/OFFLINE/EXCEPTION,
counters and last successful response. `co_device_events` is a coalescible event.

`tr_routes` supports whole address segments with a callback or downstream
address remapping. Requests crossing a segment boundary are rejected. Read-only
03/04 and whole-point 06/16 maps remain available. Modbus passthrough uses a
registered bus backend; this is not a raw UART byte tunnel or a UPP implementation.
Routes have static lifetime and are attached before receiving requests.

Protocol-address reads belong to `transmit/transmit.h`: `tr_read_register()`
reads one mapped register, while `tr_read()` accepts a source port and a register
span. `tr_callbacks()` supplies the standard Zephyr Modbus read callbacks.
These APIs can use basic cached mappings with `DP_PROCESS=n`; enabling process
also enables configured read handlers. Use `dp_read()` from
`dataprocess/points.h` to read a point by `{dev_id, sig_id}` instead of a protocol
address. Whole-point parameter writes use `tr_write()` and require `DP_PROCESS`.

Northbound port controls require `TR_MODBUS_ADAPTER` and operate per interface:

| Mode | Behavior |
| --- | --- |
| `TR_MODE_SLAVE` | Normal routing/register handling; an optional handler registered with `tr_reg_table_func()` can handle the request first |
| `TR_MODE_SLEEP` | Suppress new callbacks and replies; a handler or serial TX already in progress may finish |
| `TR_MODE_CALLBACK` | Dispatch through the registered callback; never fall through to the normal register writer |

Use `tr_set_port_mode()` / `tr_get_port_mode()` to control the mode. Register a
CALLBACK handler before selecting that mode. `tr_reg_pkt_filter_func()` installs
a filter, and `tr_pkt_filter_config()` enables or disables it. Enabled filters
run before mode handlers on accepted complete ADUs. Standard broadcasts do not
reach these callbacks unless a per-interface legacy broadcast policy permits it.
Mode and filter handlers return `-ENOENT` to continue, `0` for a reply,
`TR_MODBUS_SILENT` to consume silently, or a negative errno / positive Modbus
exception. In CALLBACK mode, `-ENOENT` produces an unsupported-operation response
instead of falling through. Registrations are copied; referenced user data must
remain valid while in use.

`tr_mb_port_work_control(iface, TR_PORT_GET_STATS, &stats)` reads
`struct tr_port_stats`; `TR_PORT_CLEAR_STATS` takes `NULL`. `requests` counts
accepted requests reaching dispatch, `responses` counts prepared replies rather
than physical TX completions, `filtered` counts requests consumed by the filter,
`sleep_drops` counts valid frames discarded while sleeping, and `callback_errors`
counts callback error/exception results. These counters do not establish that a
peer received a response; use the adapter health API for recovery state.

`tr_modbus_reconfigure()` copies settings and applies them on a control workqueue.
It drains/plugs forwarding work, waits for old TX completion, then disables and
reinitializes the interface. New requests during this transition may need retry.
A failed configuration restores the previous settings; failure of that recovery
returns `-ENOTRECOVERABLE`. Completion reports actual application of the settings.
New submissions are gated while waiting; a handler that does not finish within
2 s produces `-ETIMEDOUT` instead of blocking the control queue in drain/cancel.
`tr_modbus_recover()` can reset a backend whose TX-complete event was lost.
`tr_modbus_service(iface, timeout_ms)` schedules idle recovery and reports a
stuck handler to the board health monitor. `tr_modbus_get_health()` reports the
number of recovery attempts, last error and configuration-in-progress state.
Southbound `co_reconfigure()` requires an idle bus and leaves it asleep
on failure; the caller must reconfigure successfully before resuming it.

## Bounds and compatibility

Defaults: 64 points, 512 total value words, 16 polling blocks, 2 buses, 16 devices,
12 points per sample, 16 words per point, 24 transaction entries, 32 registers per
collection/write and 2 isolated forwarding ports. Kconfig bounds all storage.
Each bus queues at most 4 requests plus one in flight by default. The business
thread stack is controlled by `DP_PROCESS_STACK_SIZE` (default 6144 bytes).
The value pool allocates each point's actual width and is validated at startup.
Transaction entries include captured dependencies as well as emitted outputs.

The AIDC example increases point width to 32 words and changes these bounds for
its 552-point profile. `ZBUS_MSG_SUBSCRIBER_NET_BUF_STATIC_DATA_SIZE` must cover
`sizeof(dp_point_batch)` despite isolated pools in Zephyr 4.4.2. Tables are static;
lookup is linear. Measure timing and stack margins on the target before release.

The application's reusable MV-IO model binds device instances at compile time
through `src/cfg/equipment/instances.h`. The three tables stay const and collection field
arrays are shared; no runtime copy of model definitions or heap allocation is
needed. See [the model and pipeline design](../docs/collect-process-transmit.md).

NVS requires FLASH, FLASH_MAP, FLASH_PAGE_LAYOUT and NVS_DATA_CRC. Reserve at least
three equal erase sectors. The example uses four 4 KB sectors per profile.
Parameter and device events are notifications; state/result APIs are authoritative.

`transmit/compat/prepare_modbus.py` generates build-local copies of three reviewed Zephyr
4.4.2 sources for whole-PDU routing, isolated RX scheduling and one-byte RAW
exceptions. The west checkout is unchanged. Strict source hashes intentionally
stop builds after a Zephyr upgrade until this adapter has been reviewed and tested.

Per-interface `tr_modbus_set_policy()` optionally replaces the unit filter after
CRC/RAW validation and permits legacy broadcast routing/replies. A route returns
`TR_MODBUS_SILENT` to consume a frame without replying. With no policy, standard
unit filtering and broadcast behavior remain unchanged. Only the MV-IO app
installs its 247/broadcast rules; they are not enabled by the AIDC profile.
