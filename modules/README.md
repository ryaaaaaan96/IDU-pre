# Application-owned modules

[Documentation index](../docs/README.md) · [Architecture](../docs/collect-process-transmit.md)

Three reusable modules provide data processing, equipment acquisition and
protocol forwarding; a fourth provides standalone QuickLZ decoding. The
`gd32e505` hardware module supplies the out-of-tree SoC port. All have Zephyr
module metadata for integration with Zephyr 4.4.2. See
[architecture and application integration](../docs/collect-process-transmit.md).
No legacy platform library is linked.

Business-facing headers keep the reference names `data_process.h`,
`collect_lib.h`, `transmit_lib.h` and `platform_callback.h`. They use standard C
and Zephyr types directly; no ARC layer is implemented. Original and native
extension APIs have distinct names where their contracts differ. Read the
[reverse-engineering evidence and coverage](../docs/reference-platform.md)
before migrating callers; these headers do not promise full library or ABI
compatibility.

| Module | Public headers | Responsibility |
| --- | --- | --- |
| `dataprocess` | `data_process.h`, `platform_callback.h`, `dataprocess/{points,process,params}.h` | Shared data contracts/store, `dp_*` processing, optional NVS |
| `collect` | `collect_lib.h`, `collect/collect.h` | `co_*` acquisition, bus scheduling and parameter transport backend |
| `transmit` | `transmit_lib.h`, `transmit/{transmit,modbus_adapter}.h` | `tr_*` register mappings, port modes and Modbus workqueues |
| `quicklz` | `quicklz/quicklz.h` | `qlz_decode_level3()` bounded QuickLZ level 3 decoding in plain C |
| [`gd32e505`](gd32e505/README.md) | Zephyr `soc_root`, `dts_root`, `adc_*` | E505 SoC, devicetree clock startup and synchronous ADC0 driver |

Product point-table configuration lives in [`app/cfg/`](../app/cfg/README.md),
outside these modules. `devices/` binds device instances and reusable models;
`tables/` holds collection, processing and forwarding tables, signal IDs and
metadata. Imported data is inline in marked C sections, with symbols in
`tables/imported_signals.h` and provenance in
[`docs/point-table-import.json`](../docs/point-table-import.json).
`tables/point_metadata.h` and `tables/point_metadata.c` expose collection models and
processing bindings; a binding's `callback` holds the original callback name as
a string. Execution remains in `app/process/`. Register-to-signal mappings belong
to the tables. Protocol routing, command dispatch and their contracts
live in [`app/communication/config/`](../app/communication/config), while
application options live in [`app/Kconfig`](../app/Kconfig). Business callbacks
remain in `app/process/`; hardware and transport code remain in their application
layers. Moving the configuration preserves its data and behavior. `dataprocess` has no
Modbus or collector dependency. `collect` and `transmit` depend on its data contracts;
downstream forwarding is optional when `collect` is absent.

Application composition lives in `app/startup.c`, sample mapping in
`app/point_service.c`, and engineering conversion in
`app/process/engineering.c`. `app/idu_point_service.h` exposes initialization,
snapshot publication and sample-period access. The existing
`idu_equipment_init()` name remains available. These product responsibilities
stay in the application; there is no separate generic equipment module.

## Integration

Include `modules.cmake` before `find_package(Zephyr ...)` to register these modules,
or append individual module directories to `ZEPHYR_EXTRA_MODULES`.
`hardware.cmake` registers only the SoC module and is also used by Sysbuild before
image discovery. IDU board GPIO policy and boot partitions remain outside it;
the SoC sources are compiled only when `gd32e505` is selected.
Enable `DATAPROCESS`; enable `COLLECT` with the Modbus client role and `TRANSMIT`
with the server role as needed. `DP_PROCESS` enables business
transactions, sample/read/init handlers, asynchronous jobs and timers independently
of flash. `DP_PARAMS` adds parameter points, durable transactions and NVS recovery;
parameter tables are rejected without it. With `DP_PROCESS=n`, pure point
transforms and basic forwarding remain available; tables declaring business
handlers or read callbacks are rejected instead of silently ignoring them.
`TR_MODBUS_ADAPTER` enables whole-PDU routing and isolated forwarding workers,
and can be selected without `DP_PARAMS`. The full IDU-pre application selects it.
Supply static `dp_equip_cfg` tables. IDU's `app/startup.c` explicitly calls
`dp_register_config_provider(idu_equipment_config)` then `dp_init()`, creates
the application acquisition interface, registers all buses
through `co_init()` / `co_init_bus()`, then initializes and recovers parameters.
The application decides whether a storage failure permits degraded operation.
It calls `dp_process_start()` for the per-point plan and `dp_reference_start()`
for registered global INIT/TIMER tables before `co_start()` starts collection.
This order is application orchestration, not `SYS_INIT` registration.
Once the first sampled address is available, the communication layer binds the
northbound interface with `tr_attach()` and
`tr_modbus_init_server(..., true)` for independent forwarding workqueues.
The system workqueue remains available for southbound Modbus receive work.

`dp_params_set_transport()` registers copied read/write/provenance callbacks for
durable native parameter transactions. `dp_para_reg_set_func()` separately
registers the reference direct-write path used by `dp_set_para_not_dm()`.
`collect` registers both backends on bus initialization. When combining collection with a
different parameter transport, finish all `co_init_bus()` calls first, then
register the custom operations before `dp_params_init()`; later collector
initialization would register its own backend again. The callback registration
is copied, while its user data must remain valid. Replacing the backend after
parameter initialization returns `-EBUSY`; identical registrations are idempotent.
`dp_params_init(cfg, partition_id)` mounts storage without
knowing a Modbus interface. Local parameters work with no transport; forwarded
operations without a backend return `-ENODEV` before any intent/cache effects.

Use `dp_submit_sig_value()` or `dp_submit_sig_values()` for local telemetry. Collection
continues to use `dp_publish()` with its source and provenance. Both paths queue
a copied zbus message for the DP processing thread. Constants start valid, other
telemetry starts unavailable.
Numeric values use low-word-first host-endian storage. Collection and forwarding
support both word orders and optional byte swapping within each word.

## Local sample submission

The asynchronous `dp_submit_sig_value(s)` APIs accept local telemetry in host-endian
16-bit words; success means the sample was copied into the bounded queue. Wait for
its ticket and inspect point quality to determine the result. Synchronous
`dp_set_sig_value()` uses bytes and runs the transaction before returning.
Detailed sample contracts are in [the DP guide](dataprocess/README.md#采样与事务).

## QuickLZ decoding

Enable `CONFIG_QUICKLZ` for the `quicklz/quicklz.h` API
`qlz_decode_level3()`. The decoder has no hardware, cryptography or Zephyr runtime
dependency and can also be compiled as plain C. It only decodes QuickLZ blocks;
version 1.5 level 3 compressed and stored blocks use caller-provided buffers,
without streaming history or allocation. Success returns the decoded byte count.
SGZ container parsing, keys, image checks and board-specific vector validation
remain in `app/iap/`. It is independent of the three point-processing modules.

## Process and parameter contracts

See [the DP guide](dataprocess/README.md#事务标志与提交) for callback scope,
flags and commit boundaries. Native parameter storage/recovery and product
register semantics are described in [point tables](../docs/point-tables.md#参数下发与持久化).
Reference services, callback policies and known differences are documented in
[the compatibility guide](../docs/reference-platform.md#覆盖清单).

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
| `TR_MODE_SLAVE` | Normal routing/register handling; an optional handler registered with `tr_port_set_handler()` can handle the request first |
| `TR_MODE_SLEEP` | Suppress new callbacks and replies; a handler or serial TX already in progress may finish |
| `TR_MODE_CALLBACK` | Dispatch through the registered callback; never fall through to the normal register writer |

Use `tr_port_set_mode()` / `tr_port_get_mode()` to control the mode. Register a
CALLBACK handler before selecting that mode. `tr_port_set_filter()` installs
a filter, and `tr_port_enable_filter()` enables or disables it. Enabled filters
run before mode handlers on accepted complete ADUs. Standard broadcasts do not
reach these callbacks unless a per-interface legacy broadcast policy permits it.
Mode and filter handlers return `-ENOENT` to continue, `0` for a reply,
`TR_MODBUS_SILENT` to consume silently, or a negative errno / positive Modbus
exception. In CALLBACK mode, `-ENOENT` produces an unsupported-operation response
instead of falling through. Registrations are copied; referenced user data must
remain valid while in use.

`tr_port_control(iface, TR_PORT_GET_STATS, &stats)` reads
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
its 552 configured points (560 runtime slots). `ZBUS_MSG_SUBSCRIBER_NET_BUF_STATIC_DATA_SIZE` must cover
`sizeof(dp_point_batch)` despite isolated pools in Zephyr 4.4.2. Tables are static;
lookup is linear. Measure timing and stack margins on the target before release.

The application's reusable MV-IO model binds device instances at compile time
through `app/cfg/devices/instances.h`. The three tables stay const and collection field
arrays are shared; no runtime copy of model definitions or heap allocation is
needed. See [the model and pipeline design](../docs/collect-process-transmit.md).

NVS requires FLASH, FLASH_MAP, FLASH_PAGE_LAYOUT and NVS_DATA_CRC. Reserve at least
three equal erase sectors. The example uses four 4 KB sectors per profile.
Parameter and device events are notifications; state/result APIs are authoritative.

`transmit/compat/prepare_modbus.py` generates build-local copies of three reviewed Zephyr
4.4.2 sources for whole-PDU routing, isolated RX scheduling and one-byte RAW
exceptions. The west checkout is unchanged. Strict source hashes intentionally
stop builds after a Zephyr upgrade until this adapter has been reviewed and tested.

The Zephyr 4.4.2 `modbus_register_user_fc()` API is dispatched only for function
codes not already handled by its server switch. It cannot replace FC03/04/06/16,
change unit/broadcast filtering, or move receive work to a dedicated workqueue.
The compatibility hooks remain necessary for those contracts; they are explicitly
version-bound rather than a portable public extension API. See the
[official custom function-code contract](https://docs.zephyrproject.org/latest/doxygen/html/group__modbus.html).

Per-interface `tr_modbus_set_policy()` optionally replaces the unit filter after
CRC/RAW validation and permits legacy broadcast routing/replies. A route returns
`TR_MODBUS_SILENT` to consume a frame without replying. With no policy, standard
unit filtering and broadcast behavior remain unchanged. Only the MV-IO app
installs its 247/broadcast rules; they are not enabled by the AIDC profile.

## Reference service adapters

The public reference headers currently expose 79 of the 89 audited names.
Device/protocol table queries return const views; runtime default/range changes
use bounded sparse overrides. Signal/log notifications run on the DP jobs thread
through a bounded queue. Direct CO writes validate the whole request before I/O;
RTU response injection checks CRC, unit and mapped fields. TR activity monitoring
uses accepted ADUs and an inactivity timeout.

These services have explicit limits and differ from some original behaviors.
Use [reference-platform.md](../docs/reference-platform.md) for the exact contracts,
remaining ten names and configuration requirements. Use
[testing.md](../docs/testing.md) for dated validation and RAM measurements.
