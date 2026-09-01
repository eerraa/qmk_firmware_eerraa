# ERA Source Map

Genre: map
Canonical for: per-file and per-unit source ownership and edit boundaries;
source editing rules; stored-data compatibility

Paths under `split/`, `storage/`, `system/`, and `features/` are relative to
`keyboards/era/common/`. Behaviour that a contract already owns is named
there, not restated here.

## Relation, Policy, And Routing

| Source | Owner |
| --- | --- |
| `split/era_split_authority_reducer.[ch]` | local USB authority snapshot, `SPLIT_HAND_PIN` side latch, and the `is_keyboard_master()` / `is_keyboard_master_impl()` projections QMK reads |
| `split/era_split_sync_policy.[ch]` | persisted relation-independent requested sync bits (EEPROM / INPUT / RGB) |
| `split/era_split_link.[ch]` | stored, pending, and active link level, level-to-baud map, and Reconciliation (boot Low, winner's agreed raise, Low fallback). The header is the rule. Does not own PIO, poll-period derivation, or the two-phase agreement (`split/era_split_restart_agreement.[ch]`) |
| `split/era_split_sync_storage.h` | sync-policy EEPROM offset, signature, storage version, and counter-byte layout constants |
| `split/era_split_mode_planner.[ch]` | relation/mode decision and invalidation requests |
| `split/era_split_scheduler_session.[ch]` | local/peer session caches and both carriers that write them: the `SESSION_STATUS` frame and the AUTHORITY wire section |
| `split/era_split_scheduler_events.h` | lightweight dirty/due producer API |
| `split/era_split_wire_router.[ch]` | core0 owner-route choice from cached due facts |
| `split/era_split_transport_scheduler.[ch]` | core0 orchestration, cold storage context, owner transfer, immutable publication/result apply, flash guard, relation rotation, divider-change backend rebuild, and the boot boundary: `era_split_transport_scheduler_init()` is policy-only and `era_split_transport_scheduler_start_communication_core()` opens the wire at Low (`era_invariants.md`) |
| `split/scheduler/era_split_transport_scheduler_internal.h` | private scheduler state and the single definition site for scheduler cadence macros |
| `split/scheduler/era_split_transport_scheduler_timing.[ch]` | authority cadence, route deadlines, liveness, stale detection, storage service bound, and the next-deadline raw-microsecond stamp (`era_route_contract.md`) |
| `split/scheduler/era_split_transport_scheduler_routes.[ch]` | Core1-only general request publication, result drain, and recovery cancellation |
| `split/scheduler/era_split_transport_scheduler_responder.c` | core0 live-fact snapshot publication and responder-result apply |
| `split/scheduler/era_split_transport_scheduler_diagnostics.c` | cached scheduler diagnostic snapshot and baseline reset. Compiled only under `ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE` in `split/era_split_qmk_rules.mk`. Declarations in `split/era_split_transport_scheduler.h` stay ungated |

Route grant, due, and producer/consumer split: `era_route_contract.md`.

## Wire And Backend

| Source | Owner |
| --- | --- |
| `split/era_split_wire_protocol.h` | frame marker/length/control-byte constants, payload class and op ids, wire-framing windows, response-section eligibility table, and little-endian get/put for every multi-byte wire field. Scheduler cadences belong to `split/scheduler/era_split_transport_scheduler_internal.h` |
| `split/era_split_wire_frame.[ch]` | frame CRC8/CRC32, sequence and control-byte construction, compact and bulk-page encode/decode. The CRC32 here is the tree's only reflected CRC32 and keeps external linkage |
| `split/era_split_matrix_frame.[ch]` | half-matrix bit packing/unpacking and reserved-bit validation |
| `split/era_split_wire_payload.[ch]` | compact payload classify/validate/encode/decode |
| `split/era_split_transaction_types.h` | shared result/failure/diagnostic value types |
| `split/era_split_transaction_backend.h`, `split/era_split_transaction_backend_rp2040.c` | owner-gated RP2040 PIO0 role lifecycle, send, RX windows, IRQ wake/error, core1 timer alarm, and `era_split_transaction_backend_park_until()` — the only place core1 parks inside a transaction |
| `split/era_split_transaction_io.[ch]` | compact frame and backend primitive bridge; responder idle receive over a caller-supplied buffer |
| `split/era_split_transaction_engine.[ch]` | Core1 sequence/control state, transaction orchestration, response validation, diagnostics mirror |
| `split/era_split_responder_projection.[ch]` | core0 read model of core1's responder: counters folded from published Core1 responder results. The initiator-silence stale watch in `split/scheduler/era_split_transport_scheduler_timing.c` reads the request-rx count as a liveness fact |

Closed core0 wire entry: `era_closed_surface_contract.md`.

## Communication Core

| Source | Owner |
| --- | --- |
| `split/communication_core/era_split_communication_core_lifecycle.h`, `split/communication_core/era_split_communication_core_lifecycle_rp2040.c` | Core1 launch/quiesce/wake, stack, role-directed dispatch, bare IRQ vector. `era_split_communication_core_request_quiesce()` parks core1; the hardware halt is `system/era_boot_core1_halt.c` |
| `split/communication_core/era_split_communication_core_launch_signal.[ch]` | one-shot user-visible report that core1 never launched. Board paints; `keyboards/era/sirind/common/tomak_common.c` is the wired caller |
| `split/communication_core/era_split_communication_core_owner.[ch]` | backend owner role/epoch, revoke/cancel/reset, release/ready handoff |
| `split/communication_core/era_split_communication_core_initiator.h` | Core1 initiator lane ids (`SESSION_STATUS`, `SOURCE_PUSH`) and the immutable initiator request/result records shared with the scheduler |
| `split/communication_core/era_split_communication_core_queue.c` | bounded SPSC general request/result rings |
| `split/communication_core/era_split_communication_core_host_peer_lanes.c` | SESSION_STATUS and source-push initiator encode/execute. Per-lane writes go through the lane-indexed record in `split/communication_core/era_split_communication_core_internal.h` |
| `split/communication_core/era_split_communication_core_responder.[ch]` | core0 immutable responder snapshot/result publication and drain |
| `split/communication_core/era_split_communication_core_standing.[ch]` | standing exchange in every serviced relation: both published records, core0 publish/read, and the Core1 service. Relation-neutral; both sides live in one unit |
| `split/communication_core/era_split_communication_core_responder_internal.h` | private bounded responder storage and the shared ring-wrap rule both result rings turn over on |
| `split/communication_core/era_split_communication_core_responder_service.c`, `split/communication_core/era_split_communication_core_responder_result_policy.h` | Core1 responder admission, reserve-before-response, RX/admitted TX, section-bearing HEARTBEAT coalescing policy, and the free-running accepted-frame / undecodable-arrival counters. Host proof: `tests/era_split_responder_result_policy/` |
| `split/communication_core/era_split_communication_core_storage.[ch]` | storage semantic records, capacity, codec, publication, source-bound replay, lane diagnostics, and Core0's nonterminal ready-result discard when the semantic owner has disappeared |
| `split/communication_core/era_split_communication_core_storage_service.[ch]` | cold Core1 storage request/response executor |
| `split/communication_core/era_split_communication_core_diagnostics.[ch]` | cached lifecycle/owner/initiator/responder diagnostics. Lane identity core0 reads is `core1_initiator_pending_lane` / `_generation` in `split/scheduler/era_split_transport_scheduler_internal.h` |
| `split/communication_core/era_split_communication_core_internal.h` | private runtime state shared by communication-core units |

Core1 storage may read only immutable semantic records, the pinned published
image, and dedicated scratch (`era_invariants.md`).

## Replacement Storage

The `host_peer` in these file and symbol names is historical: the lane is
admitted for DUAL-HOST as well. What the names cover is
`era_host_peer_storage_contract.md`.

| Source | Owner |
| --- | --- |
| `split/era_split_eeprom_sync.[ch]` | replacement domain/op/status ids, the shared request-to-response operation map, and the core0 status/dirty/reload facade |
| `split/era_host_peer_storage.[ch]` | replacement-storage runtime: schema binding, pull/push machines, Apply, pending/recency/dirty composition, recovery, diagnostics, and the cached request-pending route-admission fact. Domains, Apply, CLEAN, arbitration: `era_host_peer_storage_contract.md` |
| `split/era_host_peer_storage_recency_policy.h` | host-testable recency publish/retire predicates. Proof: `tests/era_host_peer_storage_recency_policy/` |
| `split/era_host_peer_storage_indicator_policy.h` | host-testable EEPROM SYNC indicator continuity and wire-confirmed sent-pending level. Proof: `tests/era_host_peer_storage_indicator_policy/` |
| `split/era_host_peer_storage_standing_policy.h` | host-testable standing-cadence suppress composition (route exclusivity ∪ initiator push-Apply wait). Proof: `tests/era_host_peer_storage_standing_policy/` |
| `storage/era_nvm.[ch]`, `storage/era_nvm_format.h` | production ERA NVM engine and physical format. Proof: `tests/era_nvm/` |
| `storage/era_nvm_rp2040.[ch]` | ERA NVM RP2040 NOR binding for the linker-reserved 128-KiB region |
| `storage/era_eeprom_driver.[ch]` | QMK `EEPROM_DRIVER=custom` adapter. Proof: `tests/era_nvm_qmk_driver/` |
| `storage/era_eeprom_layout.h` | logical ERA EEPROM range ownership |
| `storage/era_storage_layout.h` | ERA-owned logical address formulas for QMK/VIA portable storage domains |
| `storage/era_eeprom_config_io.[ch]` | bounded core0 NVM bridge for ERA config |
| `storage/era_eeprom_storage.h` | header-only storage facade |
| `storage/era_storage_adoption_rules.mk`, `storage/era_storage_adoption.h` | storage adoption bundle: custom EEPROM, logical size, macro domain, layout force-include, sync eligibility (`era_board_adoption.md`) |
| `keyboards/era/sirind/common/tomak_common.[ch]`, `keyboards/era/sirind/common/tomak_era_keyboard_config.h` | tomak family board content for `tomak`, `tomak79h`, and `tomak79s`, including the syncable RGB idle-timeout storage. The RGB Sleep master is common QMK keymap config, not this record. The header preamble is the family boundary. None of the three boards keeps a `.c` |

Exact portable domains, exclusions, ranges, sizes, reload actions, Apply, and
CLEAN are canonical only in `era_host_peer_storage_contract.md`. Publication
retire proof: `tests/era_split_storage_publication_retire/`.

QMK producers of portable bytes (Locate; rules stay in the storage contract):
`quantum/dynamic_keymap.c` and `quantum/nvm/eeprom/nvm_dynamic_keymap.c` own
the public dynamic-keymap/macro API; QMK eeconfig owns RGB Matrix, keymap
config, and default layer; QMK VIA owns layout options; ERA feature/config
modules own the portable ERA config subranges.

Matrix scan may consume only a cached scalar storage-active fact
(`era_sram_residency_contract.md`).

### Storage Adoption

The bundle a board includes to become eligible for EEPROM sync is canonical in
`era_board_adoption.md`. `split/era_split_qmk_rules.mk` declines
`ERA_SPLIT_EEPROM_SYNC_ENABLE = yes` on a board that has not taken the bundle,
before any compile.

## HOST-PEER Matrix And Response State

Matrix snapshot, seq, source-push, and projection: `era_host_peer_matrix_contract.md`.

| Source | Owner |
| --- | --- |
| `split/era_host_peer_matrix_link.[ch]` | packed source-push TX/ACK counters, matrix-result apply, and the PEER key-path span instrument |
| `split/era_host_peer_transaction.h` | shared semantic request/result types; API declarations for the two `.c` units below |
| `split/era_host_peer_responder.c` | core0 response snapshot planning and sent-state commit for the responder's whole section set. Membership: eligibility table in `split/era_split_wire_protocol.h` |
| `split/era_host_peer_response.c` | HSRSP encode/decode, per-section appliers, and the time-anchor apply watch |
| `split/era_host_peer_source_snapshot.[ch]` | core0 visual/RGB source staging from live local rows |
| `split/era_split_tap_activity.[ch]` | cross-half tap-hold family, core0 side. QMK seam: `quantum/action_tapping.c` (`era_qmk_fork_ledger.md`) |
| `split/era_split_peer_layer.[ch]` | DUAL-HOST peer layer contribution; `quantum/action_layer.c` composes it. Holds the one-byte width assert for the INPUT layer wire section |
| `split/era_split_rgb_sleep_policy.h` | host-testable three-reason local sleep and stock-preset projection policy. Proof: `tests/era_split_rgb_sleep_policy/` |
| `system/era_matrix_debounce_config.[ch]` | EEPROM/VIA debounce control to runtime bridge |
| `system/era_matrix_debounce_runtime.[ch]` | scan-bound debounce runtime |
| `system/era_matrix_engine.h` | split relation's view of the matrix engine: declarations with a caller outside `system/era_rp2040_matrix_core.c`. Peer-row bookkeeping is declared in that unit |
| `system/era_rp2040_matrix.h` | raw backend contract the engine reads rows through. One implementation; why there is no selector: `era_build_options.md` |
| `system/era_rp2040_matrix_pio.c` | PIO1+DMA raw backend and sampler diagnostics |
| `system/era_rp2040_matrix_pio_frame.h` | hardware-free sampler half. Host proof: `tests/era_rp2040_matrix_pio/` |
| `system/era_rp2040_matrix_core.c` | full matrix engine and QMK-compatible `matrix_*()` surface. Defines none of QMK's three matrix delay hooks — under `CUSTOM_MATRIX` nothing compiled calls them, and the one caller in the tree (`quantum/split_common/split_util.c`, behind `SPLIT_HAND_MATRIX_GRID`) would fail the link by name rather than silently |

The matrix engine owns raw scan, debounce, local/composed rows, accepted peer
cache/projection, and changed-state publication. Scheduler and wire code own
relation, admission, freshness, and route priority.

### Board Adoption

Copy-to-RAM policy, non-split capability boundary, and the adoption checklist
are canonical in `era_board_adoption.md`.

## Board Integration And Entry Points

| Source | Owner |
| --- | --- |
| `split/era_split_qmk_rules.mk` | split SRC list, option `OPT_DEFS`, feature gates, EEPROM-sync adoption refusal, and the include of the two rows below plus `system/era_sram_resident_rules.mk` |
| `split/era_split_usb_sleep_rules.mk` | maps `RP_USB_SYNC_SUSPEND_AFTER_REMOTE_WAKEUP_SET` into `OPT_DEFS` |
| `system/era_common_qmk_rules.mk` | common post_rules option layer: phase check, variant include, class-skeleton SRC, feature SRC/refusals, VIA/NKRO wiring, matrix enable |
| `features/era_rgb_sleep.[ch]` | common persisted RGB Sleep master over QMK `keymap_config.era_rgb_sleep_disabled`, plus SYSTEM channel 9 / value 12 VIA GET/SET. Compiled on every RGB-capable ERA image; default/CLEAN zero means enabled |
| `system/era_board_hooks.[ch]` | board-facing extension contract both class skeletons call, and its weak defaults. Class-only hooks live in `system/era_nonsplit_board.h` and `split/era_split_board.h`. Also the keyboard-channel quiet persistence gate, `era_board_persistence_flush_pending()`, and the one writer of `id_unhandled` on the ERA VIA surface |
| `split/era_split_board.[ch]` | split class skeleton over `split/era_split_keyboard.c`, including weak board accessors for the optional RGB idle timeout. Does not own the init trio (`matrix_init_kb` / `eeconfig_init_kb` / `via_init_kb`) |
| `system/era_nonsplit_board.[ch]` | non-split class skeleton over `system/era_common_features.c` and `system/era_common_via.c`. May reach `era_common_*` and nothing under `split/` |
| `split/era_split_transport.c` | QMK `split_common` transport hook adapter. Live surface is `transport_master_init` / `transport_slave_init` → `era_split_transport_scheduler_init()`. Scan-path step is `era_split_transport_scheduler_transport_step()` from `matrix_post_scan()` |
| `split/era_split_keyboard.[ch]` | ERA split facade: pre/post init, task, feature reload, USB device-state change, master-gated three-reason local lighting sleep plus HOST-PEER ownership/reconcile, TOMAK SYSTEM sleep preset/exact VIA adapter, process-record, and the agreed-restart act table / dispatch (`split/era_split_restart_agreement.h` declares both). A split board runs QMK's wake path and none of its stock sleep loop (`NO_USB_STARTUP_CHECK`) |
| `split/era_split_via_link.[ch]` | VIA command handling for the split link value ids; USB re-enumeration schedule after owner Apply |
| `split/era_split_restart_agreement.[ch]` | agreed-restart mechanism. Knows neither user; act rules and `era_split_restart_prepare_local()` are defined in `split/era_split_keyboard.c`. Proof: `tests/era_split_restart_agreement/` |
| `split/era_split_via_sync.[ch]` | VIA command handling for the sync-policy value ids on `ERA_VIA_SYSTEM_CHANNEL`, behind `system/era_via_system.c` |
| `split/era_split_usb_identity.[ch]` | side-dependent USB identity at init. Product id is `ERA_SPLIT_USB_IDENTITY_PID_LEFT` / `_RIGHT` in the board `config.h`, not `keyboard.json`'s `usb.pid` |
| `system/era_boot_core1_halt.c` | pre-copy window: core1 hardware halt and double-tap bootloader arm, pinned by `.flash_startup`. Carve-out rules: `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld` |
| `system/era_vector_defaults.c` | strong SRAM definitions for ChibiOS weak vector slots. Linker consumes it. Answer a driver change with the driver's own condition, never by deleting the entry |
| `system/era_usb_session.[ch]`, `system/era_usb_session_policy.h` | local USB session facts as ERA reads them, including the frame-loss detector and, non-split only, the master-gated suspend apply pair; the pure remote-wake ISR-ownership verdict has host proof in `tests/era_usb_session_policy/` |
| `system/era_firmware_version.[ch]` | canonical compile-time `ERA_FIRMWARE_VERSION`, stable `era_firmware_version[]` ELF symbol, and read-only custom-VIA GET on channel 8, value id 1. It owns no runtime or stored state |
| `system/era_via_system.[ch]` | ERA system VIA router and task: deferred Jump-to-BOOT lifecycle (SET -> SAVE -> State Sync -> RAW IN drain, with bounded missing-phase fallback), EEPROM CLEAN confirm mask, the one restart-quiet policy, `era_via_system_restart_quiet_ok()`, and `era_via_system_eeprom_invalidate()`. Split clean hand-off: `split/era_split_keyboard.c` |
| `keyboards/era/newone/common/odessey_common.[ch]` | odessey family board content for `odessey60h` and `odessey60s`. Neither board keeps a `.c` |
| `keyboards/era/comm/riley/riley_common.[ch]` | Riley-only three-slot RGBLight lock-indicator policy, ten-byte existing-seat EEPROM record, GP25 Caps LED preservation, persistent RGB-off refusal/boot repair, Velocikey and keyboard-channel ids `13..23`. Uses native `RGBLIGHT_LAYERS`, not the RGB Matrix common indicator unit |

## Diagnostics

| Source | Owner |
| --- | --- |
| `split/era_split_transport_scheduler_diagnostics.h` | diagnostic snapshot value type |
| `split/diagnostics/era_split_transport_scheduler_role_diagnostics.[ch]` | explicit role-era baselines |
| `split/diagnostics/era_split_wire_diagnostics.[ch]`, `split/diagnostics/era_split_wire_diagnostics_counter.c` | paced wire output and optional scan/raw hooks |
| `split/diagnostics/era_via_macro_diagnostics.[ch]` | cause-variant-only decomposition of a dynamic-macro RAW-HID exchange. No release image compiles the unit |
| `split/diagnostics/era_split_qwin_diagnostics.[ch]` | silent qwin counter window and compact result; printer for the pass-phase instrument |
| `system/era_pass_phase_diagnostics.[ch]` | twelve contiguous segments tiling one `keyboard_task()` iteration. Decode: `era_capture_reading.md` **The `qwin_phase` rung** |

Formatting and snapshot construction remain outside matrix scan. Only
compile-time count/raw hooks may be scan-bound.

## Build Variants And Host Tooling

| Source | Owner |
| --- | --- |
| `keyboards/era/era_build_identity_options.mk`, `system/era_build_variant_rules.mk`, `keyboards/era/common/build_variants/*.mk`, `keyboards/era/sirind/brick65/post_rules.mk` | firmware-inert identity declarations, board-independent validation, five-axis diagnostic combinations, compiled witnesses, and compatibility refusals |
| `system/era_show_options.mk` | derived ERA option printer (`ERA_SHOW_OPTIONS=yes`); included last from post_rules |
| `system/era_rgb_matrix_rules.mk` | per-board RGB Matrix render-policy `OPT_DEFS` and sub-option refusals. RGB Matrix only |
| `tools/era_qmk_build.sh` | automation-only explicit-target QMK clean build, identity checks, copy-to-RAM gate, and labelled artifact capture |
| `tests/era_build_variant_rules/` | make-time proof of canonical variant tuples, overrides, non-split `standard`, diagnostic refusal, and retired profile rejection |
| `tests/era_nvm/` | fault-injection proof of the production A/B format and NVM engine |
| `tests/era_nvm_qmk_driver/` | stock-QMK-facing adapter integration |
| `tests/era_rp2040_matrix_pio/` | host proof of `system/era_rp2040_matrix_pio_frame.h` |
| `tests/era_split_responder_result_policy/` | host proof of responder-result HEARTBEAT coalescing limits |
| `tests/era_split_restart_agreement/` | host proof of agreed-restart phases, quarantine, and act carriers |
| `tests/era_host_peer_storage_recency_policy/` | host proof of recency publish/retire predicates |
| `tests/era_host_peer_storage_indicator_policy/` | host proof of EEPROM SYNC indicator continuity |
| `tests/era_host_peer_storage_standing_policy/` | host proof of standing-cadence suppress composition |
| `tests/era_split_storage_publication_retire/` | host proof of Core1 storage publication retire |
| `tests/era_usb_session_policy/` | host proof of remote-wake SOF ISR-ownership frame-loss classification |
| `tests/era_split_rgb_sleep_policy/` | host proof of local RGB sleep reasons and stock preset projection |
| `tests/era_via_exact_ms/` | host proof of exact-ms tapping/tapdance round-trip, State Sync `0x06` envelope mapping, and VIA system Jump-to-BOOT terminal lifecycle/fallback |
| `tests/era_firmware_version/` | host proof of the compile-time VERSION payload and GET-only common routing; definition proof of the RP2040 JSON inventory, exact VERSION binding, split L/R equality, Brick65 exclusion, the three indicator wrappers, hidden fixed SOCD/KKUK mode rows, and shared KKUK/TAPPING control order |
| `tests/era_rgb_matrix_persistence/` | host proof of deferred RGB eeconfig quiet flush |
| `tests/era_riley_rgb_indicator/` | host proof of Riley's production RGBLight-layer policy, per-slot lock/colour/brightness behavior, Indicator-Only, sleep non-wake, exact EEPROM span/defaults, GP25 Caps LED path and Velocikey |
| `tools/era_qmk_fixed_builddate_wrapper.sh` | explicit fixed-magic test-only `QMK_BUILDDATE` generation override |
| `tools/era_core1_stack_walk.py` | ELF gate's core1 stack disassembly walk. Changing its method invalidates every figure taken with it |
| `tools/era_doc_refs.py` | read-only locatability check over the agent document layer and ERA source comments. It cannot tell whether a claim is still true |

The keyboard rules own common profile defaults. Launchers select a profile but
do not duplicate compiler flags or change firmware policy.

## Load Layout

| Source | Owner |
| --- | --- |
| `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld` | common-layer SRAM-resident load image for every ERA board |
| `system/era_sram_resident_rules.mk` | residency bundle: linker script, `ERA_SRAM_RESIDENT_IMAGE` marker, ELF-gate map, pre-copy window object, vector defaults (`era_board_adoption.md` **Copy-To-RAM Policy**) |

There is no QMK placement-bridge layer: `quantum`, `platforms`, and
`lib/lib8tion` files carry no ERA placement machinery. Physical placement is
canonical in `era_sram_residency_contract.md`. The QMK core matrix boundary is
canonical in `era_invariants.md`.

## QMK Core Modifications

Retired from this document to `era_qmk_fork_ledger.md`, which is
canonical for the whole core-edit set, its gates, and the re-derivation
against pristine upstream. The rule is unchanged and binds here: **any
ERA edit to a QMK core file must appear in that ledger in the same
change.** A fork edit nobody records is a fork edit nobody can retire.

## Board Feature Owners

- `features/era_{kkuk,socd,tapping,tapdance}.[ch]`: runtime and feature-local
  persistent config; `features/era_tapdance_introspection.c` supplies the QMK
  tap-dance introspection surface. `features/era_tapdance_rules.mk` derives
  `ERA_TAP_DANCE_ENABLE` from QMK `TAP_DANCE_ENABLE` and compiles those units.
- `features/era_mousekey.[ch]`: persisted six-control adapter into QMK's
  default accelerated mouse engine. No runtime state of its own
  (`quantum/mousekey.c`). The two engine macros that become variables under
  `ERA_MOUSEKEY_RUNTIME_DELTA`: `era_qmk_fork_ledger.md`.
- `features/era_backlight.[ch]` + `features/era_backlight_pulse_policy.h`: PWM
  backlight effect layer (Steady, Breathing, four keypress-reactive Pulse
  modes). The pure policy owns overlapping-key Hold state; the ChibiOS unit
  owns the one-shot and PWM writes. The matrix-pass fast path reads no clock.
- `features/era_backlight_lock.[ch]`: indicator-supply policy — repairs a
  stored disabled/zero-level block and owns QMK backlight keycodes that would
  persist the rail below level 1. It composes with the effect unit: transient
  PWM zero remains presentation, not a disabled subsystem.
- `features/era_rgb_indicator.[ch]`: RGB Matrix lock-indicator slots. Takes
  QMK's weak render hooks strongly under `ERA_RGB_INDICATOR_ENABLE`. Not the
  tomak or odessey indicator.
- `features/era_{backlight,debounce,kkuk,mousekey,nkro,rgb_indicator,socd,tapping,tapdance}_via.[ch]`:
  VIA adapters. `features/era_nkro_via.[ch]` is the NKRO toggle on keyboard
  channel value 5; it owns no runtime state.
- `system/era_common_features.[ch]`: feature init/reload/task facade plus the
  class-neutral opportunistic NVM-maintenance owner.
- `system/era_common_via.[ch]`: VIA command router.
- `system/era_state_sync.[ch]`: RAM KEYMAP/MACRO/CONFIG revisions and the
  `GET_KEYBOARD_VALUE` `0x06` envelope. Envelope and H7S adoption boundary:
  `era_host_peer_storage_contract.md`.

## Source Editing Rules

- The discipline every owner above works inside is canonical in
  `era_sram_residency_contract.md`: what a scan path may consume, what must
  stay at a task boundary, and the per-function placement machinery that
  stays closed because the load layout owns placement.
- Inspect actual call graphs and disassembly before changing source/link order.
- **What earns a structural change** (owner decision). Three
  defects, and unreachability is only the first of them:
  - **Unreachable code**, including code reachable only through a preprocessor
    arm the build system makes impossible.
  - **Over-split structure** — a unit that exists as its own translation unit,
    or its own header, without carrying a distinct owner or a distinct reason
    to be separately compiled; a declaration split from its only user; a
    header whose whole content is one unit's private business.
  - **Under-split structure** — a unit carrying two owners, two eras, or two
    vocabularies; a file whose arms no longer share a shape; duplicated logic
    across units that already share a header.

**Length is not a fourth defect**, and that caveat qualifies how a case is
argued rather than whether it may be opened. This repository's comments
deliberately carry retired reasoning, so a file being long is not the finding —
what the length is made of is. Cite the composition, not the total.

**Do not reinstate the bar this replaced** — that a structural change is
justified only by a current ownership or measured executed-work problem, and
files are never split for being long. Applied honestly it refuses almost every
structural change: a structure that is merely wrong is not yet an ownership
*problem*, and nothing measures the executed work of a file boundary.

Finding these three is a scan, and the scan does not live here:

> **REFUSED:** commit the structural detectors that find them — duplicate-window,
> dead-static and struct-shape scanners — beside `era_doc_refs.py`.
> **WHY:** they emit candidates, not violations, so nothing can arm them, and an
> unarmed committed script is a surface to maintain with no failure it can
> report; the half of that work that *does* emit violations — an unresolvable
> reference — was folded into `era_doc_refs.py`, which the pre-commit check already
> runs.
> **REOPENS:** a detector whose every finding is a fact about the tree, which is
> the same bar `era_doc_refs.py` meets and the reason it is committed.

Two structural changes are refused standing, each for a reason that is not
"it is fine as it is":

> **REFUSED:** split the two large storage units, which really are under-split.
> **WHY:** every cut promotes shared helpers to external linkage and hands the
> compiler a fresh inlining decision on the storage path. Measured over both
> units' call graphs: the widest private subtree in
> `era_host_peer_storage.c` is `..._runtime_task`'s, and lifting it takes 18
> helpers and both file-scope statics with it, while
> `communication_core/era_split_communication_core_storage.c` shares 16 of its
> 17 statics between two or more exports. Precedent for the price:
> `plan_from_snapshot`'s measured `noinline`, 3 → 2 callers costing +216 B of
> image against a 182 B body.
> **REOPENS:** a cut that moves no shared helper across a unit boundary — which
> the figures above say does not exist today rather than has not been looked
> for. Re-derive them from the call graph, not by reading.

> **REFUSED:** delete the defensive `default:` arm on the lane switch in
> `split/communication_core/era_split_communication_core_host_peer_lanes.c` —
> the only one of that unit's four (`git grep -c "default:" --` that path) that
> is defensive.
> **WHY:** the lane crosses the core boundary as a `uint8_t` in an immutable
> record, so what the switch reads is data rather than a type and no compiler
> exhaustiveness check can stand in for the arm.
> **REOPENS:** nothing — permanent while core0 publishes the lane as a byte,
> which the cross-core record requires.

## Build Selectors And Their Dependencies

Retired from this document to `era_build_options.md`, which is canonical
for the declaration rule, every selector, the dependency edges and the
rejected arrangements. What stays here is the source side: which unit
reads an option is a per-file ownership question and belongs to the
tables above.

## Stored-Data Compatibility

**There is none** (owner decision). No ERA code exists to read,
accept, or convert a format some earlier firmware stored. A firmware upload
already clears the keymap by design, the shipped guidance states that as a
fact and points at VIA's SAVE and LOAD in both user guides
(`user/readme.txt`, `user/readme_split.txt`), and an existing
owner's stored data is explicitly not a consideration. Every path that survives
only to accept an older stored layout is a deletion candidate — and the
deletion, not the finding, is where the difficulty is.

`ERA_FIRMWARE_VERSION` in `system/era_firmware_version.h` is compile-time
identity only. Changing it adds no EEPROM bytes and by itself moves neither
`ERA_EEPROM_RESET_KEY`, `ERA_NVM_FORMAT_VERSION`, the sync-policy storage
version, nor any State Sync or storage source revision.

**Deleting an old-format acceptance is safe only when the code that runs
instead treats the old block as invalid and writes a fresh one.** Trace the
fallback before deleting and classify it:

- **A. Clean reset** — the old block fails a signature, magic, or prefix check,
  the code writes defaults over it, and the owner sees factory settings. Safe.
- **B. Misread** — the old block passes the surviving validity check and is
  then interpreted under the new layout. Not safe, and not a deletion either:
  it is silent corruption of a configuration, which is a worse outcome than the
  compatibility being retired. It needs a guard bump, not a deletion.
- **C. Undefined** — the fallback cannot be traced. Report it; do not guess.

**Worked example, the link block's 2026-08-19 change** (`split/era_split_link.c`).
One of its three reserved bytes became a flags byte carrying the agreed/unagreed
mark. Traced both directions: an older block reads with the byte at zero, which
is *agreed* — the level itself is read identically and the conservative mark
means a level that fails is dropped to High after one boot rather than
re-raised, so **class A**, and the only cost is that a pre-upgrade pair set
half-by-half converges to High instead of to the chosen level, one Apply away.
A newer block read by an older image fails that image's reserved-zero test and
reads as High, also class A and owed to nobody. **No key bump**, because
nothing is misread under either layout — which is the whole of what the
classification asks.

**Do not skip the trace on the reasoning that nobody can still be holding an
older block — that claim is short, plausible, and wrong.** It runs: a deployed
owner's reset key is older than the compiled one, so the guard fails, the
strict reset zeroes the ERA block, and no real block is ever read under a newer
layout. What it misses is that **the reset key and the block version move
independently**. `ERA_EEPROM_RESET_KEY` changes rarely and the sync-policy
block has been through five versions; every build made between a key change and
a later version bump carries the *current* key over an *older* block, so the
guard passes and the old-version path is live for whoever is holding one. A
firmware upload does not close it either: the build-date magic resets VIA's
region, not the ERA config block. That is why each deletion is classified by
its fallback rather than waived; where the fallback is A the loss is accepted
and bounded — the whole-block rewrite costs three sync toggles (EEPROM, INPUT,
RGB) and the seven divergence counters.

A version bump that changes only a default is not a layout change and is not
a compatibility fact. Canonical in `era_authority_contract.md` **Persisted Sync Policy**.

**The detectors are what make a fallback clean, and this decision does not
reach them.** `ERA_EEPROM_RESET_KEY`, the reset-guard record, the board files'
strict reset, and every signature/magic/prefix validity check stay exactly as
they are. Retiring compatibility while weakening the thing that catches a stale
block is precisely how outcome B is produced.

## Closed Boundaries

Which wire, route, and runtime surfaces are closed is canonical in
`era_closed_surface_contract.md` and `era_invariants.md`. One boundary is this
document's own, because it constrains source shape rather than a surface:

- No compatibility boundary around the former master-scheduler and sync-lane
  layers. The replacement lane deleted them outright rather than wrapping
  them; reintroducing a wrapper restores the ownership ambiguity the deletion
  removed.

What earns a structural change is in **Source Editing Rules** above.
