# ERA Active Invariants

Genre: contract
Canonical for: non-negotiable ERA invariants — QMK fork hygiene, wire direction
and traffic shape, the core0/core1 division, the boot-safety cluster, and the
Stop Conditions

HID report shape, boot protocol, NKRO, and KKUK:
`era_hid_report_contract.md`. `matrix_ready`, initiator assignment,
revalidation, and session facts: `era_authority_contract.md`. Standing-exchange
grant, cadences, and due bits: `era_route_contract.md`. SRAM placement and the
PRIMASK-to-NVIC trade: `era_sram_residency_contract.md`. HOST-PEER snapshot,
seq, and cache projection: `era_host_peer_matrix_contract.md`.

## QMK fork hygiene

| Invariant | Enforcement | Location |
| --- | --- | --- |
| `quantum/matrix_common.c` MUST remain byte-identical to upstream | Source Gate diff | `quantum/matrix_common.c` |
| `quantum/matrix.c` MUST carry exactly two departures and no others: `MATRIX_SCAN_{RAW,COUNT}_DIAGNOSTICS_ENABLE` hooks (`quantum/matrix.c:24-386`: include `:24-30`, weak kb and time helpers `:326-358`, call sites `:363-388`); under `SPLIT_KEYBOARD`, `debounce(...)` and `matrix_post_scan()` as two explicit calls (`quantum/matrix.c:394-395`) then OR (`:396`). `\|` never short-circuited. Standard variant MUST be functionally identical to upstream. A third hook needs its selector and a consumer in the same change | Source Gate | `quantum/matrix.c` |
| Fork hygiene, not a running-image claim. `ERA_RP2040_MATRIX_ENABLE ?= yes` (`era_build_options.mk:104`); every RP2040 `post_rules.mk` sets `yes`. That sets `CUSTOM_MATRIX = yes` (`system/era_common_qmk_rules.mk:100-102`), so neither core matrix file is compiled in — `system/era_rp2040_matrix_core.c` supplies `matrix_*()`. Split hard-errors otherwise (`split/era_split_qmk_rules.mk:201-202`). Verify by diff | Source Gate | `split/era_split_qmk_rules.mk`; `system/era_common_qmk_rules.mk` |
| A build-variant name and its compiled five-axis tuple are one identity. `standard`, `wire`, `qwin`, `cause`, `stale` and `qwin_phase` each resolve to the immutable `wire/qwin/phase/cause/stale` tuple. A command-line option, `MAKEFLAGS`, exported environment, or `make -e` may select the name and MUST NOT mutate an axis beneath it | launcher ELF witnesses | `manuals/era_build_options.md` |

## Wire direction

Transport receive MUST NOT inject HID events and MUST NOT create remote HID
endpoints. It updates caches only. QMK matrix/debounce/key processing remains
the HID producer; report bytes are `era_hid_report_contract.md` **Split And
Storage**. `era_host_peer_matrix_link_accept_source_push_packed()` in
`split/era_host_peer_matrix_link.c` unpacks into
`era_matrix_engine_accept_peer_snapshot()` in `system/era_rp2040_matrix_core.c`
(peer cache only). HOST projection onto `composed_rows` is
`era_matrix_engine_sync_peer_projection()` in that engine file, from the HOST
`matrix_post_scan()` path. ACTIVITY apply is the same class: cache, no HID
event (`split/era_split_tap_activity.h`). `transport_master()` /
`transport_slave()` are absent from `split/era_split_transport.c`.

| Invariant | Enforcement | Location |
| --- | --- | --- |
| Peer-unknown discovery MUST be Left-only `SESSION_STATUS` probe. Peer-unknown Right MUST NOT independently send. Confirmed HOST-PEER: PEER initiates; HOST is responder only. Confirmed DUAL-HOST: Left initiator, Right responder. HOST and Right independent send MUST remain closed | mode planner | `split/era_split_mode_planner.c`; assignment table: `era_authority_contract.md` **Initiator Authority** |
| Confirmed HOST-PEER normal traffic MUST be PEER-initiated source-push or heartbeat; HOST may return only the admitted response slot. Storage-lane shape (PEER-initiated; HOST data or ACK only in the admitted slot; arbitration picks content, never who initiates) | contract | `era_host_peer_storage_contract.md` **Relation Admission** |
| Confirmed DUAL-HOST traffic MUST be exactly `SESSION_STATUS`, the replacement-storage lane, and the runtime section exchange. Storage is change-driven and has no cadence of its own (`era_route_contract.md`); a window with no settled config change MUST carry no storage transaction | contract | `era_host_peer_storage_contract.md` **Relation Admission**; `era_route_contract.md` **DUAL-HOST** |
| DUAL-HOST MUST NOT open matrix, digest, or mirror routes. Key input stays off that wire — not idle silence: the initiator polls at a constant period once `SESSION_STATUS` confirms the relation. Period: `era_route_contract.md`. Margin at it: `era_performance_gates.md` | `_Static_assert` | `split/era_split_wire_protocol.h:598-599` |
| **render-state clause.** Runtime render and resolution state is not key input, and neither is the relation's own revalidation. The visual baseline is render state even though its content is pressed positions: it crosses for the receiving half's render engine alone, writes no matrix state, and reaches no action processing. The MATRIX section stays closed beside it | `_Static_assert` | `era_wire_contract.md`; `split/era_split_wire_protocol.h:598-599` |
| Every runtime section MUST be latest-state and edge-armed: the current value, never a transition record, advertised while it differs from what the wire last confirmed and retired when the wire confirms it. Idle and typing-with-no-layer-change both carry zero runtime sections while the poll runs, net of the relation time anchor, which crosses once per `ERA_SPLIT_TIME_ANCHOR_REFRESH_MS` 60000. A capture reads that cadence as the anchor's own (`anc`, `sec=0x80`), never as a silence failure | contract | `era_capture_reading.md` |
| `HOST_PEER_HEARTBEAT` and `HOST_PEER_ACK_STATUS` are route-context names over one-byte compact control payloads. They MUST NOT get class/op ids. Split op bytes in force are `0x10`, `0x20`, `0x21`, and `0xE0..0xEF` | contract | `split/era_split_wire_protocol.h:118-134`; `split/era_split_wire_payload.c` |
| `matrix_ready` and live-relation `SESSION_STATUS` role: `era_authority_contract.md` **Matrix Ready**, **Revalidation Authority**. Do not restate them here | contract | `era_authority_contract.md` |

## Core0 / core1

| Invariant | Enforcement | Location |
| --- | --- | --- |
| `CORE1_FULL` MUST remain the default and only selectable ERA split stage | build resolver | `split/era_split_qmk_rules.mk:300-302` |
| Every active initiator and responder backend RX/TX operation MUST execute on core1. Core0 remains route/snapshot/result-apply owner only; a core0 backend lease, wire executor, responder wait adapter, per-route owner switch, or synchronous transaction fallback is forbidden | Source Gate retired-path | core1 backends |
| Route authority and exchange timing are separate. Which half may initiate, which sections may cross, and what a responder may answer stay core0's (mode planner, eligibility table, responder snapshot). *When* a granted exchange runs may be core1's, through an explicit core0 publication and nothing else. The grant is per relation and per route kind, bounded, and never extends to authority. Core1 MUST stop the standing exchange the moment that grant stops matching, and MUST NOT resume one it stopped on a failure — failure returns the wire to core0's `SESSION_STATUS` revalidation | contract | identity terms and stop semantics: `era_route_contract.md` **Runtime Execution Owner** |
| One runtime architecture — the standing exchange — in every serviced relation. What may differ is the section set, the matrix lane, and the period; never the carrier | contract | `era_route_contract.md` |
| Every HOST-PEER frame the wire transaction engine accepts (any class, including storage, before lane admission) refreshes relation liveness. Liveness MUST NOT depend on traffic class. `era_split_communication_core_responder_service_once()` in `split/communication_core/era_split_communication_core_responder_service.c` increments `responder_accepted_rx_count` on every accepted frame before lane dispatch. The responder-silence stale watch has no exemptions: the durable apply is core1's liveness beat, not a core0 keepalive | contract | `split/communication_core/era_split_communication_core_responder_service.c`; period in `era_route_contract.md` |
| A relation's liveness MUST NOT depend on the half that can stop. Core0 originates no periodic frame in either serviced relation. A busy responder that refuses service is indistinguishable on the wire from a dead one; one config edit on a busy responder collapsed a pair for tens of seconds | contract | standing exchange on core1 |
| While core1 owns a transaction, core1 MUST also own control-byte construction and transaction-engine tx/ack sequence state. Core0 may publish only semantic immutable request data and MUST NOT preallocate wire sequence values | contract | core1 transaction engine |
| A bare RP2040 core1 actor MUST NOT call ChibiOS thread suspend/resume or scheduler-lock APIs. PIO IRQ or event wake is a hint; FIFO/error/deadline/cancel/epoch predicates are the receive authority | contract | `split/era_split_transaction_backend_rp2040.c` |
| The core1 launcher MUST mask the core0 SIO FIFO IRQ across the boot handshake and restore it afterwards. `era_split_communication_core_launch()` in `split/communication_core/era_split_communication_core_lifecycle_rp2040.c` disables `RP_SIO_IRQ_PROC0` around `era_split_communication_core_launch_sequence()` and re-enables it if it was enabled. ChibiOS SMP installs a core0 SIO FIFO handler that drains FIFO messages after startup, so an unmasked runtime handshake fails | contract | `split/communication_core/era_split_communication_core_lifecycle_rp2040.c:360-374` |
| Replacement Apply has one durable/public boundary: ERA NVM commit. Before ADMIT every fallible authority/precondition may abort with no candidate NVM transaction. After ADMIT, `era_nvm_replace()` owns the outcome (`storage/era_nvm.c`): ordinary EEPROM readers MUST observe the complete pre-Apply domain until its durable commit succeeds, then the complete candidate immediately afterwards. No relation/policy/source change after ADMIT authorizes rewriting the old bytes back. An NVM failure leaves the public image old; a later runtime/manifest/Core1 publication failure repairs forward from canonical NVM | contract | order: `era_host_peer_storage_contract.md` |
| ERA NVM flash work MUST NOT recurse into the keyboard or wire. Program and erase are synchronous Core0 operations over an SRAM-resident image and MUST NOT call `keyboard_task()`, `matrix_task()`, scheduler work, or transport work (`quantum/keyboard.c`). Inactive-bank maintenance erases at most one 4-KiB sector per `era_common_features_maintenance_task()` (`system/era_common_features.c`) and returns to the main loop before considering the next. A mandatory rotation may synchronously finish the finite remaining sectors; its width is measured on device | contract | `storage/era_nvm.c`; `system/era_common_features.c` |
| An open macro transaction blocks only durable operations touching the macro domain. A normal EEPROM write outside that range MUST remain durable even while the macro marker is nonzero. A write or result-bearing replacement that overlaps the macro domain remains refused until the macro closes | contract | transcript: `era_host_peer_storage_contract.md` |
| Core1 MUST reserve bounded responder-result capacity before sending any success response **that publishes a result**. Source-push capacity MUST remain reserved from session/heartbeat traffic, and no accepted matrix may be ACKed before it is stored in that capacity. A section-less ACK to a bare poll publishes nothing and skips a new reservation. A successful section-bearing HEARTBEAT whose exact owner epoch, relation generation, responder snapshot generation and actual section mask already have one successful result pending in the general ring may also skip — its only Core0 work is the same idempotent sent-shadow commit. Session, runtime/source-push, failed/unsent replies and any changed identity/mask remain one-result-per-arrival. Rings: `ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_GENERAL_RESULT_SLOTS` 4, `ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_SOURCE_RESULT_SLOTS` 2 | static slot constants | `split/communication_core/era_split_communication_core_responder_internal.h` |

Cross-core transport MUST use static ERA SPSC rings with explicit fences. No
ERA image links an allocator.

| Excluded | Why |
| --- | --- |
| Pico SDK queue | `calloc`/`free` of its storage |
| hardware spinlocks | scarce shared resource; blocking helpers disable interrupts while held |
| inter-core FIFO | reserved for the launch handshake; core launch and lockout-style flows already own it |

> **REFUSED:** Pico SDK queue, hardware spinlocks, or the inter-core FIFO as the cross-core transport.
> **WHY:** the first allocates, the second is a scarce lock whose helpers mask interrupts, and the third is already owned by the launch handshake.
> **REOPENS:** a static, non-allocating primitive that is not the launch FIFO and does not disable interrupts while held.

## Boot-safety

| Invariant | Enforcement | Location |
| --- | --- | --- |
| Core1 MUST be hardware-halted through PSM `FRCE_OFF PROC1` at the entry of the window that destroys the image core1 is executing (crt0 copy loops 249-286: `.sram_image` copy, `.bss` clear of `g_era_split_communication_core`, `__init_ram_areas()` zero of `.ram5_clear`), not at the reset causes that lead into it. Entry is `early_hardware_init_pre()` in `system/era_boot_core1_halt.c`, reached from crt0 `bl __early_init` at 196 — the only ungated call before the copy loops. The halt sets then clears `FRCE_OFF` | linker pins the TU + `ASSERT` | `system/era_boot_core1_halt.c:228-234`; `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld` |

`NVIC_SystemReset()` asserts AIRCR.SYSRESETREQ, which on RP2040 resets **only the core that asserts it** (datasheet 2.4.2.9; 2.4.8 misleadingly implies otherwise). The halt is also the pico-SDK precondition for the launch handshake ERA reimplements.

> **REFUSED:** a cause-side guard as the primary core1 halt.
> **WHY:** only `shutdown_quantum()` (`quantum/quantum.c`) offers a hook, so enumeration covers software-initiated resets alone; a debugger or SWD reset retains power, re-runs boot2/crt0, and is indistinguishable from a warm reset.
> **REOPENS:** a hook that every reset that re-runs crt0 reaches, including debugger and SWD.

| Invariant | Enforcement | Location |
| --- | --- | --- |
| Pre-copy MUST NOT read or write any SRAM object except `magic_location`, the double-tap bootloader magic — one word whose named linker region is outside every crt0 copy and clear range with an exact-size `ASSERT` | linker `ASSERT` | `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`; `platforms/chibios/bootloaders/rp2040.c` |
| Watchdog `REASON` and scratch0 are reset-domain MMIO, not a second production SRAM exception. RP2040 clears scratch0--3 on RUN or DVDD reset, preserves them through soft reset, and reserves only scratch4--7 for the bootrom watchdog tuple. Both first firmware boots after a UF2 copy arrived with scratch0 clear; the marker persisted across the following core-only reset. The current-reset discriminator is the observed joint tuple | device fact | `system/era_boot_core1_halt.c` |
| Every pre-copy wait MUST be bounded, including the PSM acknowledge wait (`ERA_BOOT_CORE1_HALT_ACK_SPINS` 1000) and the ROSC stable-release guard. Interrupts are masked from crt0 167 (`cpsid i`). The halt MUST clear `FRCE_OFF` before returning: the launch handshake depends on that and touches PSM nowhere | `_Static_assert` + disassembly | `system/era_boot_core1_halt.c:111-112`; `system/era_boot_core1_halt.c:157-158`; carve-outs at the selector list in `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld` |

> **REFUSED:** a second retained pre-copy SRAM object, or widening the four-byte magic region.
> **WHY:** the production exception set is exactly one word; a second object reopens this clause rather than joining the set.
> **REOPENS:** a named linker region outside every crt0 copy and clear range with an exact-size `ASSERT`, plus a contract entry.

| Invariant | Enforcement | Location |
| --- | --- | --- |
| After crt0, no code reachable on any execution path may live at a flash VMA. `.flash_startup` is pre-copy only. Every `.vectors` entry except the reset slot MUST hold an SRAM address; the reset slot MUST stay in flash (boot2 fetches the reset PC from the vectors LMA before any copy). The `.vectors` gate exists because this was believed true while false in dozens of slots | `.vectors` gate + linker `ASSERT` | `era_performance_gates.md`. Flash program/erase with core0 interrupts enabled, and the PRIMASK-to-NVIC trade, are `era_sram_residency_contract.md` |
| The boot sequence MUST have zero conditional branches on reset cause. Every reset runs the same path. USB attach is part of it: `protocol_pre_init()` (`tmk_core/protocol/chibios/chibios.c`) calls `init_usb_driver()`, which asserts the D+ pull-up through `usbConnectBus()` (`tmk_core/protocol/chibios/usb_main.c:346`) unconditionally, so an EEPROM CLEAN recovery boot is the ordinary boot | contract | nothing branches on cause |
| A time stamped before `keyboard_init()` (`quantum/keyboard.c`) is not comparable after it. `keyboard_init()` calls `timer_init()`, which calls `timer_clear()` (`platforms/chibios/timer.c`), so an unsigned elapsed diff across that boundary reads about 2^32 | contract | `quantum/keyboard.c` |
| The physical-RUN classifier is the one bounded exception. It branches only the retained double-tap state transition. Physical RUN and every other reset class still execute the same crt0 copy, core1 halt, initialization, and USB attach. It may never gate the halt or the rest of boot | contract | `system/era_boot_core1_halt.c` |
| Stable-release policy is fixed: 184,320 ROSC ticks = `ERA_BOOT_GUARD_ROSC_CHUNK_COUNT` 768 × `ERA_BOOT_GUARD_ROSC_CHUNK_TICKS` 240; each chunk has a `ERA_BOOT_GUARD_ROSC_POLL_LIMIT` 256 software poll-loop bound; failure leaves the word clear and ordinary boot continues. Only all chunks completing may write ARMED. No ROSC control, XOSC, divider, frequency or clock-tree register may be changed in this window. Datasheet 1.8--12 MHz → about 15.36--102.4 ms. The 20 ms lower edge is an electrical-noise rejection objective. `RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT` 1000 owns the upper edge from the first keyboard task; an observed total up to 2000 ms is permitted | `_Static_assert` + disassembly | `system/era_boot_core1_halt.c:100-112`; `era_performance_gates.md` |
| The post-init closer MUST represent “deadline has started” separately from the timestamp. A timer reading of zero is valid and MUST NOT double as an unset sentinel. Disarming MUST clear the started bit and timestamp as well as the magic word | contract | `platforms/chibios/bootloaders/rp2040.c` |

> **REFUSED:** a reset-cause marker that changes the boot path, or a deferred USB-attach gate whose failsafe is evaluated only from the housekeeping loop.
> **WHY:** a failsafe that depends on the loop it protects is not fail-safe — a hang before that loop left the device invisible on USB with no bound.
> **REOPENS:** a failsafe that does not depend on the loop it protects, plus its own contract entry and device evidence.

| Invariant | Enforcement | Location |
| --- | --- | --- |
| Every deliberate software route out of the running image MUST leave the double-tap magic clear. Only a physical reset may be counted as a tap. Magic has three states — clear, armed, bootloader-requested — and clear is what the software disarm writes | contract | `platforms/chibios/bootloaders/rp2040.c` |
| Counting and the bootrom jump MUST stay split across crt0's copy loops: `early_hardware_init_pre()` (`system/era_boot_core1_halt.c`) arms and counts before the copy; `__late_init()` (`platforms/chibios/bootloaders/rp2040.c`) performs the jump after it. `reset_usb_boot()` lives in `.sram_image`, so a pre-copy call links through a veneer into SRAM nothing has written yet. An arm that waits for `__late_init()` opens the window tens of milliseconds after reset; a tweezer double tap completes inside that gap and the measured hit rate was about one in ten | contract + linker carve-out | `system/era_boot_core1_halt.c`; `platforms/chibios/bootloaders/rp2040.c` |
| `__late_init()` MUST NOT silently re-arm a software reset the pre-copy classifier left clear; only a later exact physical RUN may pass the guard and become a first tap | contract | `platforms/chibios/bootloaders/rp2040.c` |
| Both `mcu_reset()` and `bootloader_jump()` MUST disarm, independently. `bootmagic()` runs from `quantum_init()` before the first loop pass, so with only `mcu_reset()` disarming the window was still armed when `bootloader_jump()` reached BOOTSEL and the board bounced into the bootloader until a replug | contract | `platforms/chibios/bootloaders/rp2040.c` |

> **REFUSED:** raising `RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT` as a substitute for the pre-copy / post-copy split (2000U tried on device).
> **WHY:** 2000U moved the tweezer hit rate not at all — the loss is at the window's start, not its end.
> **REOPENS:** evidence that the miss is at the window's end rather than its start.

| Invariant | Enforcement | Location |
| --- | --- | --- |
| The first core1 launch of a boot MUST be a single named step: `era_split_keyboard_post_init()` (`split/era_split_keyboard.c`) calling `era_split_transport_scheduler_start_communication_core()`, which returns `bool` (`split/era_split_transport_scheduler.c`). `era_split_transport_scheduler_init()` and the `transport_master_init`/`transport_slave_init` hooks it runs from (`split/era_split_transport.c`) plan the relation and the wire role and stop there — policy MUST NOT open the wire | contract | `split/era_split_keyboard.c` |
| Remaining `ensure_core1()` calls on the scan path and the host-peer lanes are re-arms of an already-launched core and MUST stay unreachable before that step, which they are because no matrix scan and no housekeeping pass runs inside `keyboard_init()` (`quantum/keyboard.c`) | contract | `split/era_split_transport_scheduler.c` |
| `era_split_communication_core_request_quiesce()` (`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`) raises `stop_requested` and waits for `running` to clear; core1 parks in its idle loop and a later `start()` wakes it without a relaunch. That reversibility is load-bearing for live recovery. It is NOT a substitute for the halt | contract | `split/communication_core/era_split_communication_core_lifecycle_rp2040.c` |

> **REFUSED:** turning `era_split_communication_core_request_quiesce()` into a substitute for the hardware halt.
> **WHY:** it only parks core1 in its idle loop so a later start wakes it without a relaunch, and the next boot's `.bss` wipe would drop the flag and resume full wire service (`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`).
> **REOPENS:** quiesce hardware-halts PROC1 and the next boot cannot resume it from a `.bss` wipe.

## Stop Conditions

Stop and report before editing if an implementation requires any of the following without a contract update that explicitly opens it:

| Tripwire | Contract that could open it |
| --- | --- |
| QMK core matrix changes | this file (fork-hygiene rows) |
| HOST source response outside an admitted HOST-PEER response slot, or a `HOST_PEER_HOST_SOURCE_RSP` section `era_wire_contract.md` does not list as open. A section on the initiator's core0-lane answer trips the same wire: that answer is the bare control ACK and carries no section at all | `era_route_contract.md` **One carrier for the response section set** |
| An RGB or INPUT section in HOST-PEER source-push | `era_wire_contract.md` |
| `EEPROM_SYNC` shape, direction, domain, or authority outside the exact admitted surface. The class is in force and its execution is ordinary, so the tripwire is the surface, not the class | `era_closed_surface_contract.md`; `era_host_peer_storage_contract.md` |
| DUAL-HOST matrix/digest/mirror payloads | this file (matrix-closure row) |
| Row-array matrix payloads | `era_host_peer_matrix_contract.md` |
| Direct HID injection from split transport | this file (HID-producer rows) |
