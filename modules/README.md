# Application-owned modules

[Documentation index](../docs/README.md) · [Architecture](../docs/collect-process-transmit.md)

Three reusable modules provide data processing, equipment acquisition and
protocol forwarding; `quicklz` provides standalone decoding and `iap` provides
legacy firmware upload, verification and MCUboot staging. The
`gd32e505` hardware module supplies the out-of-tree SoC port. All have Zephyr
module metadata for integration with Zephyr 4.4.2. See
[architecture and application integration](../docs/collect-process-transmit.md).
No legacy platform library is linked.

Large runtime work buffers use Zephyr `k_malloc()` / `k_free()`. See
[memory ownership and heap budgets](../docs/memory-management.md) for lifetimes,
failure handling and application sizing; const point tables remain in Flash.

Business-facing headers keep the reference names `data_process.h`,
`collect_lib.h`, `transmit_lib.h` and `platform_callback.h`. They use standard C
and Zephyr types directly; no ARC layer is implemented. Original and native
extension APIs have distinct names where their contracts differ. Read the
[reverse-engineering evidence and coverage](../docs/reference-platform.md)
before migrating callers; these headers do not promise full library or ABI
compatibility.

| Module | Public headers | Responsibility |
| --- | --- | --- |
| `analog_input` | `analog_input/analog_input.h` | Configured ADC channel groups, software averaging and ordered raw samples |
| `gpio` | `gpio/digital_io.h` | Configured digital input groups |
| [`input_sampler`](input_sampler/README.md) | `input_sampler/input_sampler.h` | Caller-configured analog/digital sampling with caller-owned buffers |
| `watchdog` | `watchdog/watchdog.h` | Periodic GPIO feeding and terminal stop for external watchdogs |
| [`vport`](vport/README.md) | `vport/vport.h` | Generic complete-message callbacks, ownership isolation and per-port send serialization |
| `dataprocess` | `data_process.h`, `platform_callback.h`, `dataprocess/{points,process,params}.h` | Shared data contracts/store, `dp_*` processing, optional NVS |
| `collect` | `collect_lib.h`, `collect/{collect,port,simulator,model_simulator}.h` | `co_*` acquisition, bus scheduling, client initialization and optional simulation |
| `transmit` | `transmit_lib.h`, `transmit/{transmit,message,modbus,modbus_adapter,server,registers}.h` | `tr_*` mappings, server lifecycle, register services, protocol handlers and optional vport adaptation |
| `quicklz` | `quicklz/quicklz.h` | `qlz_decode_level3()` bounded QuickLZ level 3 decoding in plain C |
| [`iap`](iap/README.md) | `iap/{iap,sgz}.h` | IAPv3 reception, SGZ/AES, signed images, flash staging and recovery; devicetree/Kconfig product policy |
| [`gd32e505`](gd32e505/README.md) | Zephyr `soc_root`, `dts_root`, `adc_*` | E505 SoC, devicetree clock startup and synchronous ADC0 driver |

Product point-table configuration lives in [`app/protocol/`](../app/protocol/README.md),
outside these modules. `protocol.c` binds devices to registered table IDs; `inc/` holds public headers only;
`tables/` holds collection, processing and forwarding tables, including default
values and ranges in `process.c`. The application maintains one IDU product scheme;
IAPv3 is an optional upgrade module and does not select a different set of tables.
The three application tables use the reference `DM_SIG_CFG_S`,
`MODBUS_RTU_TABLE_S` and `MB_TABLE_CELL_T`. Generic UPP model layouts remain
available in the modules and are covered by independent tests.
`DM_SIG_CFG_S` extends the original layout with inline `data` (`DM_SIG_DATA_S`) and
range/restoration flags inside `sig_flag`. DP reads the const data directly; IDU
has no parallel parameter array or default/range lookup callback. Omitted values
are zero. `FLAG_RANGE_CHECK` enables both configured bounds; with zero bounds it
accepts only zero. Without it, inline bounds are inactive and type limits apply.
`FLAG_NO_RESTORE` opts a persistent point out of explicit default restoration
without changing its saved value. Large arrays use static const defaults, with NULL meaning zero.
[`point_parameters.h`](dataprocess/include/dataprocess/point_parameters.h) provides
read-only country queries and an optional startup-only country override. COMMON remains
the product default. Known-country misses fall back to row data; unknown countries fail.
Successful DP initialization freezes country selection. Queries do not alter live values,
NVS or RAM overrides. Legacy default/range callbacks remain supported for other consumers
and cannot be combined with country overrides.
Signal macros and function pointers bind business handlers directly; there is no
runtime callback-name registry or duplicate model metadata.

Application rows initialize every member explicitly. Optional-field defaults belong
to the module that interprets the table: `DP_POINT_DEFAULT_*` in
[`reference_tables.h`](dataprocess/include/dataprocess/reference_tables.h),
`CO_TABLE_DEFAULT_*` in [`collect_lib.h`](collect/include/collect_lib.h), and
`TR_TABLE_DEFAULT_BYTE_ORDER` in [`transmit_lib.h`](transmit/include/transmit_lib.h).
These are compile-time field constants, not whole-row macros or runtime templates.
Required addresses, IDs, widths and protocol choices remain explicit product values.
Modbus acquisition rows set `uint16_t period_ms` directly in milliseconds; no cycle
encoding or conversion callback is registered. Initialization reads explicitly set
`FLAG_INITIALIZE`; zero alone disables periodic acquisition. Unmarked zero-period
`FLAG_COLLECT` destinations are command-only. UPP model cycle
conversion remains a separate API.

The same convention applies to device bindings and startup plans (`DP_DEVICE_DEFAULT_*`,
`DP_BINDING_*`, `DP_PROCESS_DEFAULT_*`), acquisition configuration (`CO_BUS_DEFAULT_*`,
`CO_PORT_DEFAULT_*`), forwarding configuration (`TR_REGISTER_DEFAULT_*`,
`TR_SERVER_DEFAULT_*`) and optional vport I/O fields (`VPORT_IO_DEFAULT_*`,
`VPORT_SOURCE_DEFAULT_PEER`). Each set is declared with its owning public structure.
These defaults do not select hardware, allocate future resources or start services.
Only the active union members of a Zephyr Modbus configuration are initialized.
Runtime timers, locks and service state continue to follow their module lifecycle APIs.

Register-to-signal mappings belong
to the tables. Protocol windows and command bindings
live in [`app/protocol/`](../app/protocol); execution belongs to `transmit`, while
application options live in [`app/Kconfig`](../app/Kconfig). Business callbacks
remain in `app/process/`; product input bindings are in the optional `gpio` profile
and protocol adaptation in `transmit`. `dataprocess` has no
Modbus or collector dependency. `collect` and `transmit` depend on its data contracts;
downstream forwarding is optional when `collect` is absent.

Application startup lives in `app/main.c`; sample mapping and common callbacks
live in `app/process/points.c`, with engineering conversion in
`app/process/engineering.c`. `app/process/points.h` exposes snapshot
publication, sample-period access and business callbacks. Status and trial-image
readiness belong to `app/status/`. There is no separate generic equipment module
or legacy `idu_equipment_*` application API.

## Protocol implementation layout

Both acquisition and forwarding separate protocol adapters from their core:

```text
collect/                              transmit/
├── src/                              ├── src/
│   ├── collect.c / message.c          │   └── transmit.c / message.c
│   └── model_simulator.c              └── protocols/modbus/
└── protocols/modbus/                      ├── modbus_adapter.c
    ├── client.c / client.h                ├── server.c
    ├── port.c                            ├── registers.c
    ├── simulator.c / raw.c               ├── raw.c
    ├── CMakeLists.txt                     ├── compat/
    └── Kconfig                           ├── CMakeLists.txt
                                          └── Kconfig
```

Public headers remain under each module's `include/`; existing `co_*` / `tr_*`
callers do not change include paths. `CO_MODBUS_ADAPTER` enables the built-in
Zephyr client, port initialization and reconfiguration without requiring transmit.
With it disabled, register `transfer` and/or `read_model` callbacks; `co_port_open()`
and attempts to register a bus without a backend return `-ENOTSUP`.
Scheduling, initialization, queues, statuses and callback-based register operations
continue to work. `TR_MODBUS_ADAPTER` independently enables whole-PDU server
adaptation and isolated workers, with its existing service/transport sub-options.

`CO_SIMULATOR` deliberately models a remote server and still uses the transmit
adapter; it is optional and is not a dependency of the acquisition client.
Each protocol directory owns its RAW-to-RTU-message adapter. Generic message entry
points live in each module's src/message.c. The old ADU handoff has been removed.
Shared build-local Zephyr fixes remain in transmit/protocols/modbus/compat and are
also loaded by the collector without enabling any transmit service or worker.

`tests/collect_modbus` exercises real Zephyr client reads, writes, reconfiguration
and lost acknowledgements with transmit and the Modbus server disabled.
`tests/collector/adapter-off.conf` exercises the scheduler with custom callbacks
and both the client adapter and transmit disabled.

## Communication ownership

There is no application communication implementation directory. Product configuration
uses const tables in `app/protocol/`. `ports.c` also owns routing callbacks and registration:
TX receives the virtual port ID and selects the physical adapter; RX dispatches by
logical port and source link/peer plus explicit dev_id (zero means unknown).
Module code never includes product headers or interprets physical routing.

The CO backend callback is `transfer(user, dev_id, adu)`; the synchronous adapter
is `co_modbus_transfer(iface, dev_id, adu)`. Each bound RAW interface pins identity
under its transaction lock, sends it through `destination.dev_id`, validates the
reply source, and clears the transaction owner on success or failure. Direct
Zephyr client calls use the binding's default identity and cannot borrow another
thread's active device identity. TR preserves the incoming source when replying.
Do not mix direct Zephyr client calls and CO transactions concurrently on the
same bound interface: direct calls bypass the CO transaction lock.

| Service | Module API | Product input |
| --- | --- | --- |
| Network registration | App `protocol_ports_init()` calls `co_modbus_port_bind()` / `tr_modbus_port_bind()` | Logical IDs, application callbacks and physical serial endpoints |
| Forwarding lifecycle | `tr_forward_service()` | Independent forwarding identity, logical port, table IDs and broadcast policy; runtime unit |
| Register dispatch | `tr_register_route()` / `tr_register_write()` | Optional `routes` address segments; ordinary values go through DP tables |
| Point status and commands | `DM_SIG_CFG_S.pf_sig_proc` | SET callback; `FLAG_READ_CALLBACK` enables GET; `dp_defer_para_action()` for validated single-point actions |
| Protocol segments | `MB_TABLE_SEG_T` selected by `tr_forward_cfg.protocol_segments` | Start/end wire address and complete-ADU callback, e.g. `iap_route()` |
| Acquisition port | `co_port_open()` | RTU client configuration or simulator instance |
| Simulated device | `co_simulator_*()` | Register addresses, widths, limits and initial values |
| Simulated model | `co_model_simulator_read()` | Model registry, device range and default-value callback |

Enable these independently with `TR_SERVER_SERVICE`, `TR_REGISTER_SERVICE` (requires the
Modbus adapter, independently of DP parameter storage), `CO_SIMULATOR` and `CO_MODEL_SIMULATOR`. The product Kconfig
selects defaults; standalone module consumers opt in. vport never interprets registers
or commands. Product declarations use `TR_FORWARD_CONFIG` to resolve their own mapping and protocol segments; `tr_register_service.device` remains an optional legacy consumer interface.
These match the request start address before standard PDU validation; the handler
validates function, exact length, payload bounds and session ownership. This supports
extended IAP FC16 packets without relaxing ordinary Modbus limits. A matched handler’s
`-ENOENT` becomes an address error instead of falling through. Duplicate segment groups,
invalid ranges and overlapping protocol segments fail attachment. Unmatched requests
continue through standard `tr_segment` routes and DP tables. Device views, tables and
service configs must remain valid and immutable after attachment. Transmit has no
dependency on IAP or parameter storage.

Each server config owns one interface. The service keeps separate active/pending
settings, applies changes asynchronously after admitted requests finish, and services
recovery. Call periodically from the application thread, not a receive/configuration
callback. Config objects must remain valid and immutable. Do not mix this owner with
external `modbus_disable()` / reconfiguration on the same interface.

Simulators use caller-owned, zero-initialized state and immutable register layouts.
Register values are independent across instances. Writes must cover whole 1/2-word
values; all requested values are checked before committing. Lost acknowledgements
never trigger an automatic write retry. Register banks are bounded by
`CO_SIMULATOR_WORDS`; no heap or product-specific register is required. Startup is
serialized, and each instance exclusively owns its client/server RAW interfaces.
The model simulator only supplies configured defaults; it is not a UPP transport.

## Input and watchdog services

Input/health modules can be enabled with `ANALOG_INPUT` (requires ADC),
`DIGITAL_IO` and `WATCHDOG_MONITOR` (require GPIO). `gpio` contains the generic
`digital_io_*` APIs. The independent `INPUT_SAMPLER` module combines GPIO and
analog groups supplied by the caller. Generic GPIO use does not require an ADC
or a product devicetree node.
None of these services depend on point tables, Modbus or DP, or allocate heap memory.
Each input group uses a caller-owned immutable configuration; each watchdog owns
its timer and state. Physical device/channel configuration must be serialized by
the caller when several groups share hardware.

The external watchdog service uses `WATCHDOG_MONITOR` to avoid conflicting with
Zephyr's existing `WATCHDOG` driver option.

`analog_input_init()` sets up ADC channels, and `analog_input_read()` returns
unsigned raw values in configured order. It supports single-ended resolutions
up to 16 bits, averaging 1..65535 samples with integer truncation. Buffer capacity
is checked before conversion; a failed batch may contain partial results and
must be treated as invalid. Product calibration and signal conversion stay in
the application. The ADC driver determines supported gains, references and
hardware oversampling.

`digital_io_init()` configures 1..32 inputs; `digital_io_read()` returns logical
active bits in LSB-first or MSB-first order, leaving the caller's result unchanged
on failure. Pins are sampled sequentially. The implementation now lives in
`gpio`, retaining its existing public GPIO API.

`watchdog_init()` starts periodic feeding on a configured on-chip GPIO using a
kernel timer. GPIO toggles must be nonblocking and ISR-safe. `watchdog_error()`
queries the current error without changing the feed cadence; `watchdog_stop()`
permanently stops feeding and keeps the pin driven at its last level. GPIO errors
also latch and stop feeding. Initialization is serialized, query/stop synchronize
with timer callbacks. Task deadline policy belongs to application status, not to
this physical backend. No particular external watchdog IC is assumed.

`input_sampler_init()` initializes configured input groups and
`input_sampler_read()` fills caller-owned buffers. It supports optional analog
input and any configured number of digital groups; output is valid only when the
whole read succeeds. Capacity errors occur before sampling. No product node,
channel count, voltage reference or address offset is embedded in the module.
`app/acquisition/config.c` constructs descriptors from the product devicetree,
checks the current conversion's 12-bit/3.3 V contract and interprets address bits.
Its `acquisition_inputs_sample()` returns the application snapshot declared in
`app/acquisition/sample.h`. `app/process/points.c` maps raw values into DP points.

Application state, fault latching and state-to-LED policy live in `app/status/`.
LED output directly uses Zephyr's LED API (`LED` / `LED_GPIO`); no application-owned
LED module or generic blink worker is retained.
It also binds the external watchdog configuration and reports completed cycles.
With MCUboot enabled, healthy progress invokes `status/image_confirm/image_confirm.c` to
qualify and confirm the trial image. The confirmation state belongs to
`application_status`; acquisition only orchestrates sampling, publication and communication.
Current policy is steady on during startup/fault and one toggle per successful
sample; specific fault blink codes are intentionally left for product definition.
Fast sampling/publication phases only update state.
LED failures stop healthy progress and cannot override the original fault.

## Integration

`VPORT` independently enables the [generic callback port core](vport/README.md).
CO/TR expose message registration/input/send interfaces and use it internally.
`CO_MODBUS_RAW_PORT` / `TR_MODBUS_RAW_PORT` enable their respective protocol adapters.
The application selects actual hardware in callbacks, while build-local Zephyr
serial I/O retains framing, CRC, DE and transmit completion. No vport hardware
backends or receive workers are allocated. See [communication ports](../docs/communication-ports.md).

Include `modules.cmake` before `find_package(Zephyr ...)` to register these modules,
or append individual module directories to `ZEPHYR_EXTRA_MODULES`.
`hardware.cmake` registers only the SoC module and is also used by Sysbuild before
image discovery. IDU board GPIO policy and boot partitions remain outside it;
the SoC sources are compiled only when `gd32e505` is selected.
Enable `DATAPROCESS`; enable `COLLECT` and its `CO_MODBUS_ADAPTER` with the Modbus
client role, and `TRANSMIT` with the server role as needed. Callback-only acquisition
can disable `CO_MODBUS_ADAPTER` and `MODBUS_CLIENT`; the existing ADU data types still
require `MODBUS`. `DP_PROCESS` enables business
transactions, sample/read/init handlers, asynchronous jobs and timers independently
of flash. `DP_PARAMS` adds parameter points, durable transactions and NVS recovery;
parameter tables are rejected without it. With `DP_PROCESS=n`, pure point
transforms and basic forwarding remain available; tables declaring business
handlers or read callbacks are rejected instead of silently ignoring them.
`TR_MODBUS_ADAPTER` enables whole-PDU routing and isolated forwarding workers,
and can be selected without `DP_PARAMS`. The full IDU-pre application selects it.
Supply static `dp_equip_cfg` tables. IDU's `app/main.c` explicitly calls
`protocol_register_point_tables()` then `dp_init()`, creates
the application acquisition interface, registers all buses
through `co_init()` / `co_init_bus()`, then initializes and recovers parameters.
The application decides whether a storage failure permits degraded operation.
It calls `dp_process_init()` for the per-point plan and `dp_reference_init()`
for registered global INIT/TIMER tables before `co_polling_init()` starts collection.
This order is application orchestration, not `SYS_INIT` registration.
Once the first sampled address is available, `tr_forward_service()` resolves the registered logical port, internally attaches and
starts the northbound interface with an independent forwarding workqueue, then
maintains its settings and recovery on subsequent application cycles.
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
SGZ parsing, image checks and configurable vector validation belong to
[`iap`](iap/README.md). Its configured startup reads partition and memory settings
from devicetree, keys/size limits from Kconfig, and optionally flushes DP parameters.
No product adapter C file is needed. QuickLZ remains independent of these services.

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
reach these callbacks unless a per-interface broadcast routing policy permits it.
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

`tr_modbus_init_server()` checks interface ownership before changing the writer.
An existing client or server returns `-EALREADY` and keeps its callback and queue;
failed startup releases a newly reserved forwarding slot. On isolated ports,
RX notifications received during startup are held until the worker is ready.
Use this serialized startup API consistently; do not race it with direct Zephyr
`modbus_init_*()` calls. Use `tr_modbus_reconfigure()` for an active port.

The register service takes one parameter-status snapshot per FC03 response,
so flags, error and both sequence words describe the same transaction.
This does not promise a simultaneous snapshot of unrelated point values.

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

Defaults: 64 points, 512 total value words, 16 polling blocks, 1 bus, 16 devices,
12 points per sample, 16 words per point, 24 transaction entries, 32 registers per
collection/write and 1 isolated forwarding port. Kconfig bounds all storage.
Each bus queues at most 4 requests plus one in flight by default. The business
thread stack is controlled by `DP_PROCESS_STACK_SIZE` (default 6144 bytes).
DP allocates one value buffer from the Zephyr system heap at startup using the
actual sum of point widths; the 512-word setting remains a validation limit.
`HEAP_MEM_POOL_ADD_SIZE_DATAPROCESS` supplies a shared heap budget (module default
1024 bytes, current product 256 bytes). Allocation failure is reported before DP
becomes ready, and sampling does not allocate memory.
Transaction entries include captured dependencies as well as emitted outputs.
The current IAPv3 product explicitly configures one bus and one isolated forwarding
port in `prj.conf`. Firmware profiles do not override these capacities; changes
are made manually in `.conf`. Full-application CMake checks reject missing
required services; standalone module users keep their independent feature selection.

Worker resources are configured in the module that owns them:

| Module | Kconfig option | Module default | Meaning |
| --- | --- | ---: | --- |
| collect | `CO_MAX_BUSES` | 1 | Reserved bus workers, stacks and queues |
| collect | `CO_STACK_SIZE` | 4096 | Bytes per bus worker stack |
| transmit | `TR_PORTS` | 1 | Reserved isolated forwarding workers |
| transmit | `TR_STACK_SIZE` | 4096 | Bytes per forwarding worker stack |
| transmit | `TR_CONFIG_STACK_SIZE` | 2048 | One shared reconfiguration/recovery stack |

These stacks are reserved statically even when a worker has not started.
Architecture alignment/guard space can increase their allocated size.
The Modbus transport binding count is independent of `TR_PORTS`.
`CO_MAX_BUSES` replaces `DP_MAX_BUSES`; update custom configuration fragments
and regenerate old build directories with `west build -p always`.
`co_init_bus()` validates all device bus IDs against its capacity before
installing any callbacks. DP validates the point-table structure without
depending on the collector's worker pool.
Tests with multiple buses or forwarding workers explicitly declare their fixture
capacity in their own `.conf`; they do not supply product defaults.

`ZBUS_MSG_SUBSCRIBER_NET_BUF_STATIC_DATA_SIZE` must cover
`sizeof(dp_point_batch)` despite isolated pools in Zephyr 4.4.2. Tables are static;
reference key lookup searches the device list and then only that device's rows,
without reordering rows or allocating a RAM index. Native tables use a linear
scan. Measure timing and stack margins on the target before release.

The application registers const tables through Zephyr iterable sections. Device bindings
in `app/protocol/protocol.c` select process/optional collect table IDs through `data_device_cfg`; DP validates and assembles a small RAM
DEV_TABLE_S pointer view. Process and acquisition rows remain shared const arrays;
execution views use caller scratch storage. TR owns a small immutable mapping/lifecycle view per `tr_forward_cfg`, referencing const rows and the same DP keys. There is no full-table RAM copy; DP point values still use the existing exact-size heap allocation. See [the model and pipeline design](../docs/collect-process-transmit.md).

NVS requires FLASH, FLASH_MAP, FLASH_PAGE_LAYOUT and NVS_DATA_CRC. Reserve at least
three equal erase sectors. IDU uses four 4 KB sectors for its parameter partition.
Parameters use independent versioned records keyed by `(dev_id, sig_id)`; only
values and identity/type/length metadata are persisted. Defaults and bounds stay
in the process table. Temporary commit journals preserve batch recovery. An
incompatible format clears only the configured settings partition, logs a warning
and loads defaults (`DP_PARAM_STORAGE_RESET`); I/O errors fail without reformatting.
Old aggregate snapshots are discarded, not migrated. See [storage rules](../docs/point-tables.md).
Parameter and device events are notifications; state/result APIs are authoritative.

`transmit/protocols/modbus/compat/prepare_modbus.py` generates build-local copies of four reviewed Zephyr
4.4.2 sources for whole-PDU routing, isolated RX scheduling and one-byte RAW
exceptions, a quiet interface-availability check, plus optional vport handoff
and TX-completion hooks. The west checkout is unchanged. Strict source hashes intentionally
stop builds after a Zephyr upgrade until this adapter has been reviewed and tested.

The Zephyr 4.4.2 `modbus_register_user_fc()` API is dispatched only for function
codes not already handled by its server switch. It cannot replace FC03/04/06/16,
change unit/broadcast filtering, or move receive work to a dedicated workqueue.
The compatibility hooks remain necessary for those contracts; they are explicitly
version-bound rather than a portable public extension API. See the
[official custom function-code contract](https://docs.zephyrproject.org/latest/doxygen/html/group__modbus.html).

Per-interface `tr_modbus_set_policy()` optionally replaces the unit filter after
CRC/RAW validation. `route_broadcast` explicitly delegates broadcast handling and
reply policy to the router. `tr_register_attach()` enables this only when the service explicitly
sets `route_broadcast`; ordinary parameter broadcasts are always ignored without
replying. A route returns `TR_MODBUS_SILENT` to consume a frame silently. The IDU
product has no special unit 247 filter or ordered MV-IO window handler.

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

### Independent forwarding configuration

`TR_FORWARD_CONFIG` requires the reference-table registry, register dispatch and server lifecycle.
Use `dp_data_device_bind()` for process/optional collect declarations, then `dp_init()`.
Register physical I/O through CO/TR message APIs before `tr_forward_service(config, sampled_unit)`.
For an already managed Modbus interface, `tr_forward_attach(iface, config)` installs the same routing.
All declarations and tables must outlive the services and remain immutable; attachment is a startup operation.
Each forward identity, logical port and actual interface can have one active declaration.
No virtual forwarding entry is added to the DP point pool or NVS keys.

The native `tr_bind_mapping(port, view)` API selects an immutable `dp_equip_cfg` mapping view.
It validates before publishing. Once any explicit mapping exists, unbound interfaces fail instead of
using the legacy global mapping. The context-free `tr_read_register()` rejects ambiguous use with
`-ENOTUNIQ`; callbacks use the active request interface. Product writes resolve addresses in TR
and pass keys into DP. `dp_params_write_registers(mapping, address, words, count)` now requires an explicitly selected immutable mapping view; it cannot borrow a TR entry. Legacy consumers pass their own view. No NVS format or stable data key changes are involved.
