# ERA Active Invariants

Status: active
Genre: contract
Canonical for: non-negotiable ERA invariants — QMK fork hygiene, wire direction
and traffic shape, the core0/core1 division, the boot-safety cluster, and the
Stop Conditions
Read when: every ERA split implementation or review session

## Hard Invariants

- `quantum/matrix_common.c` MUST remain byte-identical to upstream.
- `quantum/matrix.c` MUST carry exactly two departures from upstream and no
  others:
  - compile-time-gated, weak, no-op-by-default scan diagnostics hooks behind
    `MATRIX_SCAN_{RAW,COUNT}_DIAGNOSTICS_ENABLE`. **Two, and a third is not a
    small addition**: a hook here is permitted only with the selector that
    emits it and a consumer that reads it, in the same change. A third one
    stood in this file with neither, tested at eleven places and reachable by
    no configuration, until it was deleted;
  - under `SPLIT_KEYBOARD`, `debounce(...) | matrix_post_scan()` split into two
    explicit calls before the OR. Behavior is unchanged — `|` never
    short-circuited, so both always ran — and the split stands on its own
    reason: C leaves the operand evaluation order of `|` unspecified, so the
    single expression did not say which of the two ran first.
  The release profile MUST be functionally identical to upstream. No other QMK
  core matrix edit is permitted.

  This is a fork-hygiene invariant, not a statement about the running image.
  Every ERA board sets `ERA_RP2040_MATRIX_ENABLE = yes`
  (`era_split_qmk_rules.mk` hard-errors otherwise), which sets
  `CUSTOM_MATRIX = yes`, so neither file is compiled into any ERA firmware —
  `system/era_rp2040_matrix_core.c` supplies the `matrix_*()` surface. The
  invariant still binds: it keeps the fork mergeable with upstream and keeps
  both files available to a future board that does not take the ERA matrix
  engine. Verify by diff, per the Source Gate, not by measuring behavior.
- Transport receive paths MUST NOT inject HID events directly.
- Transport receive paths MUST update caches/state only; QMK matrix/debounce/key
  processing remains the HID producer.
- Local HID output authority MUST remain local. Transport MUST NOT create remote
  HID endpoints or inject HID events. HOST-PEER peer matrix projection is
  allowed only through the local QMK matrix path and local HOST report path.
- Peer-unknown discovery MUST be Left-only `SESSION_STATUS` probe.
- Peer-unknown Right MUST NOT independently send.
- Confirmed HOST-PEER normal traffic MUST be PEER-initiated source-push or
  heartbeat; HOST is responder only and may return only the admitted response
  slot opened by the active wire contract. The replacement-storage lane is
  the same shape by enumeration: every storage request — the compact
  operations, the arbitration sync-status exchanges, and the one bulk push
  chunk — is PEER-initiated, and HOST storage data or acknowledgement moves
  only in the admitted slot. Arbitration decides which half's *content*
  wins, never which half initiates.
- HOST-PEER HOST independent send MUST remain closed.
- DUAL-HOST active-channel ownership MUST be Left initiator, Right
  responder.
- Confirmed DUAL-HOST traffic MUST be exactly `SESSION_STATUS`, the
  replacement-storage lane, and the runtime section exchange, and nothing
  else. Every storage request — the compact operations, the arbitration
  sync-status exchanges, and the one bulk push chunk — is Left-initiated;
  Right storage data or acknowledgement moves only in the admitted slot, and
  Right independent send MUST remain closed. The storage lane is change-driven
  and has no cadence of its own (`era_route_contract.md`), so a DUAL-HOST
  window with no settled config change MUST carry no storage transaction at
  all.

  **The property is that key input stays off the wire, and it is not idle
  silence.** The initiator polls at a constant period once `SESSION_STATUS`
  confirms the relation, so an idle window carries a runtime transaction every
  poll period. What the mode protects is the HID path — no key crosses this
  wire, by the matrix/digest/mirror closure below — and a cadence spends only
  wire margin, which is a budget question that does not touch the property. The
  closure holds at any period; `era_route_contract.md` owns the period and
  `era_performance_gates.md` the margin at it, which is measured, not argued.

  Runtime render and resolution state is not key input, and neither is the
  relation's own revalidation: neither enters the HID path, so carrying them
  does not reopen what this rule closes. **The visual baseline is render state
  even though its content is pressed positions** — it crosses for the receiving
  half's render engine alone, the diff-replay feeds the hit tracker, writes no
  matrix state, and reaches no action processing — and the MATRIX section stays
  closed beside it, asserted (`era_wire_contract.md`).

  Every runtime section MUST be latest-state and edge-armed: the current value
  of a state, never a record of a transition, advertised while it differs from
  what the wire last confirmed and retired when the wire confirms it. **That
  rule is what keeps the property measurable.** A poll carries a runtime
  section only when one differs, so an idle window and a typing window with no
  layer transition both carry zero runtime *sections* while the poll runs — net
  of the relation time anchor, whose fresh reading always differs and so
  crosses once per 60 s refresh in any window. A capture reads that cadence as
  the anchor's own (`anc`, `sec=0x80`), never as a silence failure, and reads
  the section counter rather than the frame counter for exactly this reason.
- DUAL-HOST normal mode MUST NOT open matrix, digest, or mirror routes.
- `SESSION_STATUS.matrix_ready` is a hard gate for HOST-PEER matrix payload
  admission.
- `HOST_PEER_HEARTBEAT` and `HOST_PEER_ACK_STATUS` are route-context names over
  one-byte compact control payloads. They MUST NOT get class/op ids.
- Every HOST-PEER frame the wire transaction engine accepts (any traffic
  class including storage, before lane admission) refreshes relation
  liveness. Liveness MUST NOT depend on traffic class, and the
  responder-silence stale watch has no exemptions: the durable apply keeps
  the wire alive from whichever core is still running, which is core1's
  liveness beat rather than a core0 keepalive (`era_route_contract.md`).
- **A relation's liveness MUST NOT depend on the half that can stop.** A
  responder that refuses service because it is busy is indistinguishable on the
  wire from a dead one, and the initiator's answer to a dead wire is to tear the
  relation down — one config edit on a busy responder is what collapsed a pair
  for tens of seconds. Both serviced relations therefore carry liveness in
  core1's standing exchange, and core0 originates no periodic frame in either.
- **One runtime architecture — the standing exchange — in every serviced
  relation.** What may differ between HOST-PEER and DUAL-HOST is the section
  set, the matrix lane, and the period; never the carrier. A change that gives
  one relation a second runtime architecture, or one section set a second
  carrier, reopens this clause (`era_route_contract.md`).
- `SESSION_STATUS` remains the bootstrap, discovery and recovery revalidation
  frame. **It is not the liveness probe and not the policy-generation carrier
  of a live relation**: both ride each relation's own lane in the AUTHORITY
  wire section, and the frame has no post-relation cadence. What survives is
  the frame's authority, not its periodicity — every path that re-decides a
  relation still returns through it, and no other frame may take that role.
- `CORE1_FULL` MUST remain the default and only selectable ERA split stage.
- Every active initiator and responder backend RX/TX operation MUST execute on
  core1. Core0 remains route/snapshot/result-apply owner only; a core0 backend
  lease, wire executor, responder wait adapter, per-route owner switch, or
  synchronous transaction fallback is forbidden.

  **Route authority and exchange timing are separate, and core1 may hold the
  second under a bounded standing grant.** Which half may initiate, which
  sections may cross, and what a responder may answer stay core0's, expressed
  where they already are — the mode planner, the eligibility table, and the
  responder snapshot. *When* a granted exchange runs may be core1's, through an
  explicit core0 publication and nothing else. Core1 MUST stop the standing
  exchange the moment that grant stops matching, and MUST NOT resume one it
  stopped on a failure — failure returns the wire to core0's ordinary
  `SESSION_STATUS` revalidation, which outranks it. This clause fixes only that
  the grant exists, that it is per relation and per route kind, that it is
  bounded, and that it never extends to authority; its identity terms and stop
  semantics are canonical in `era_route_contract.md`.
- While core1 owns a transaction, core1 MUST also own control-byte construction
  and transaction-engine tx/ack sequence state. Core0 may publish only semantic
  immutable request data and MUST NOT preallocate wire sequence values.
- A bare RP2040 core1 actor MUST NOT call ChibiOS thread suspend/resume or
  scheduler-lock APIs. PIO IRQ or event wake is a hint; FIFO/error/deadline/
  cancel/epoch predicates are the receive authority.
- Cross-core transport MUST use static ERA SPSC rings with explicit fences.
  Pico SDK `queue.c` is excluded because it `calloc()`s and `free()`s its
  storage; hardware spinlocks are excluded because they are a scarce shared
  resource whose blocking helpers disable interrupts while held; the inter-core
  FIFO is reserved for the launch handshake because core launch and
  lockout-style flows already own it.
- The core1 launcher MUST mask the core0 SIO FIFO IRQ across the boot
  handshake and restore it afterwards. ChibiOS SMP installs a core0 SIO FIFO
  handler that drains FIFO messages after startup, so an unmasked runtime
  handshake fails.
- Core1 MUST be hardware-halted through PSM `FRCE_OFF PROC1` at the entry of
  the window that destroys the image core1 is executing, not at the reset
  causes that lead into it. That window is `crt0_v6m.S:249-286`: the
  `.sram_image` copy overwrites core1's code and data, the `.bss` clear zeroes
  `g_era_split_communication_core`, and `__init_ram_areas()` zeroes
  `.ram5_clear`, which holds core1's live stack and live vector table. A core1
  still running across it writes unbounded to shared SRAM and peripherals,
  returns into zeroed memory, faults to a zeroed vector, and locks up. The
  window exists because `NVIC_SystemReset()` asserts AIRCR.SYSRESETREQ, which
  on RP2040 resets **only the core that asserts it** (datasheet 2.4.2.9; 2.4.8
  misleadingly implies otherwise). The halt is also the precondition the
  pico-SDK states for the launch handshake ERA reimplements: "core 1 must
  previously have been reset either as a result of a system reset or by calling
  `multicore_reset_core1`".

  It therefore runs from `early_hardware_init_pre()` in
  `system/era_boot_core1_halt.c`, reached from `crt0_v6m.S:196` — the only
  ungated call before the copy loops, so every reset that re-runs crt0 reaches
  it whatever caused it.

  A cause-side guard MUST NOT return as the primary mechanism: only
  `shutdown_quantum()` (`quantum/quantum.c`) offers a hook, so enumeration covers software-initiated
  resets alone, and a debugger or SWD reset retains power, re-runs boot2/crt0,
  and is indistinguishable from a warm reset. Enumeration is fragile as well as
  incomplete — boot-time bootmagic runs through no hook and is safe only
  because it happens to precede the core1 launch.

- The pre-copy placement binds that implementation. It MUST NOT read or write
  any SRAM object, except an object whose sole purpose is to carry a value
  across a reset and whose named linker region is outside every crt0 copy and
  clear range with an exact-size `ASSERT`. The production exception set is
  exactly one word, `magic_location`, the double-tap bootloader magic.
  A second object, or widening the four-byte magic region, reopens this clause
  rather than joining the set.

  The exception is narrow because both halves of it are. The reason the
  prohibition exists — nothing is initialized yet — is the reason this
  reset-retained value exists, so the rule's own justification does not reach
  the magic word. It is safe to touch only because the linker script claims and
  asserts its placement outside every copy and clear range by name, not because
  it is small (mechanism: `era_sram_residency_contract.md`).

  The production pre-copy path reads watchdog `REASON` and scratch0, then
  writes one scratch0 survival marker. This is reset-domain MMIO, not a second
  production SRAM exception. RP2040 clears scratch0--3 on RUN or DVDD reset,
  preserves them through soft reset, and reserves only scratch4--7 for the
  bootrom watchdog tuple. On this device, however, both first firmware boots
  after an UF2 copy arrived with scratch0 clear; the marker persisted across
  the following core-only reset instead. The current-reset discriminator is
  therefore the observed joint tuple, not an assumption that the bootrom USB
  path preserves this marker.

  Every pre-copy wait MUST be bounded, the permanent PSM acknowledge wait and
  the fixed ROSC stable-release guard included. Interrupts are masked from
  `crt0_v6m.S:167`, no watchdog countdown is started anywhere in this image,
  and VTOR already points at the not-yet-copied SRAM vector table, so an
  unbounded spin there has no recovery but removing power. The halt MUST clear
  `FRCE_OFF` before returning: the launch handshake depends on that and touches
  PSM nowhere. The linker script pins the translation unit and asserts the
  placement, and the three carve-out rules binding any addition are written at
  that selector list.

- After crt0, no code reachable on any execution path may live at a flash VMA.
  The `.flash_startup` carve-out is pre-copy only. Every `.vectors` entry
  except the reset slot MUST hold an SRAM address; the reset slot MUST stay
  in flash, because boot2 fetches the reset PC from the vectors LMA before
  any copy loop has run.

  This is checked and not reviewed, because it was believed true for the whole
  life of the SRAM-resident image while being false in dozens of vector slots:
  the `.vectors` gate and its complementary linker `ASSERT` are canonical in
  `era_performance_gates.md`. It is also the precondition for running a flash
  program/erase with core0 interrupts enabled; that mechanism and the
  PRIMASK-to-NVIC trade hanging on it are canonical in
  `era_sram_residency_contract.md`.

- The boot sequence MUST have zero conditional branches on reset cause. Every
  reset runs the same path, and USB attach is part of it: `protocol_pre_init()` (`tmk_core/protocol/chibios/chibios.c`)
  asserts the D+ pull-up through `usbConnectBus()` unconditionally, so an
  EEPROM CLEAN recovery boot is the ordinary boot with nothing added to it.
  A reset-cause marker that changes the boot path MUST NOT be reintroduced
  without its own contract entry and device evidence. The deferred-attach gate
  that used to sit here is what that rule is made of: its own failsafe was
  evaluated only from the housekeeping loop, so a hang before that loop left
  the device invisible on USB with no bound at all. **A failsafe that depends
  on the loop it protects is not fail-safe.**

  **A time stamped before `keyboard_init()` (`quantum/keyboard.c`) is not comparable after it.**
  `main()` runs `protocol_pre_init()` and then `keyboard_init()`, whose
  `timer_init()`/`timer_clear()` rebases the timer to zero, so an unsigned
  elapsed diff taken across that boundary reads about 2^32 and any deadline
  built on it expires on its first evaluation. That is what made the retired
  gate's failsafe fire spuriously on the one boot it existed for, and it binds
  any future code that stamps a time before `keyboard_init()`.

  The physical-RUN classifier is the one bounded exception. It branches only
  the retained double-tap state transition: physical RUN and every other reset
  class still execute the same crt0 copy, core1 halt, initialization, and USB
  attach. It may never gate the halt or the rest of boot.

  The stable-release policy is fixed, not a measured minimum and not a
  calibrated timebase. On the first exact physical RUN entry it keeps the magic
  clear for 184,320 ROSC ticks, expressed as 768 bounded chunks of 240 through
  the RP2040 ROSC `COUNT` register. Every chunk also has a 256-iteration
  software poll-loop bound; failure leaves the word clear and ordinary boot
  continues. Only all chunks completing may write ARMED. No ROSC control, XOSC,
  divider, frequency or clock-tree register may be changed in this window.

  The datasheet's 1.8--12 MHz startup range makes the counter portion
  approximately 15.36--102.4 ms. The nominal 20 ms lower edge is an
  electrical-noise rejection objective, not a precision promise, and no new
  precision counter is required; human double taps must still pass on device.
  The 1000 ms post-init timeout owns the upper edge, and because it starts at
  the first keyboard task, the physical window is guard plus boot-to-task
  latency plus that timeout — an observed total up to 2000 ms is permitted.

  The post-init closer MUST represent “deadline has started” separately from
  the timestamp value. A timer reading of zero is valid and MUST NOT double as
  an unset sentinel. Substituting one for a zero reading makes another task in
  the same millisecond evaluate unsigned `0 - 1` and close the window
  immediately. Disarming MUST clear the started bit and timestamp as well as
  the magic word.

  Clean disassembly MUST prove the fixed tick count, both finite bounds, and
  the hardware exclusions above.

- Every deliberate software route out of the running image MUST leave the
  double-tap magic clear. Only a physical reset may be counted as a tap.

  The counting and the bootrom jump are split across crt0's copy loops and
  MUST stay split: `early_hardware_init_pre()` (`system/era_boot_core1_halt.c`) arms the window and counts the
  reset before the copy, `__late_init()` performs the jump after it, and
  neither half may move to the other side. `reset_usb_boot()` lives in
  `.sram_image`, so a pre-copy call links silently through a veneer into SRAM
  nothing has written yet; and an arm that waits for `__late_init()` opens the
  window tens of milliseconds after reset on this image, which is the defect
  rather than a margin question — a tweezer double tap completes inside that
  gap, the second reset only re-arms, and the measured hit rate was about one
  in ten. Raising `RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT` does not
  substitute: 2000U was tried on device and moved the rate not at all, which is
  what proved the loss is at the window's start rather than its end.

  The magic therefore carries three states — clear, armed, bootloader-requested
  — and clear is what the software disarm writes. The pre-copy classifier
  leaves a software reset clear, and `__late_init()` (`platforms/chibios/bootloaders/rp2040.c`) MUST NOT silently re-arm
  that state; only a later exact physical RUN may pass the guard and become a
  first tap.

  **Both `mcu_reset()` (`platforms/chibios/bootloaders/rp2040.c`) and `bootloader_jump()` MUST disarm**, independently.
  `bootmagic()` runs from `quantum_init()` before the first loop pass, so with
  only `mcu_reset()` disarming, the window was still armed when
  `bootloader_jump()` reached BOOTSEL: the bootrom reset into the freshly
  written image, `__late_init()` read the magic the previous image had left,
  and the board bounced back into the bootloader until a replug. That
  `bootloader_jump()` does not route through `mcu_reset()` is the reason to
  disarm there too, not a reason to leave it.

- The first core1 launch of a boot MUST be a single named step:
  `era_split_keyboard_post_init()` (`split/era_split_keyboard.c`) calling
  `era_split_transport_scheduler_start_communication_core()`. Policy
  computation MUST NOT open the wire — `era_split_transport_scheduler_init()`
  and the `transport_master_init`/`transport_slave_init` hooks it runs from
  plan the relation and the wire role and stop there. The remaining
  `ensure_core1()` calls on the scan path and the host-peer lanes are re-arms
  of an already-launched core and MUST stay unreachable before that step,
  which they are because no matrix scan and no housekeeping pass runs inside
  `keyboard_init()`. A single named step is also what gives launch failure
  somewhere to be handled: the step returns `bool`.

- `era_split_communication_core_request_quiesce()` (`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`) is NOT a substitute for the
  halt and MUST NOT be turned into one. It raises `stop_requested` and waits for
  `running` to clear; core1 parks in its idle loop and a later `start()` wakes
  it without a relaunch. That reversibility is load-bearing for the live
  recovery paths, and the next boot's `.bss` clear would un-raise the flag and
  resume full wire service anyway.
- The flash commit-window hooks MUST stay immediately around
  `wear_leveling_erase()`/`wear_leveling_write()`, not at the NVM wrapper.
  `nvm_eeprom_write_begin_kb()` returns `void` and cannot abort, and
  `nvm_eeconfig_erase()`/`eeprom_write_block()` bypass the wrapper entirely.

  > **REFUSED:** move the EEPROM commit-window hooks out of the scheduler.
  > **WHY:** they are the scheduler's own responder-park mechanism wearing a
  > QMK hook signature; the only foreign thing in them is one
  > `era_flash_slice` call per side, so the move is a redesign.
  > **REOPENS:** the park moving out of the scheduler on its own merits.
- **A gap inside a sliced flash erase MUST run the keyboard pass and MUST NOT
  run the wire.** The backing store is partly erased for the width of that gap,
  so what it may run is defined rather than inherited.

  The pass is `matrix_task()` (`quantum/keyboard.c`) — scan, difference, `action_exec()` — and it is
  the whole pass rather than the scan because the scan alone bounds `scan_hz`
  and leaves the outage a typist has exactly where it was. The transport step
  is skipped, and skipping it is sound for the reason the gap exists: the
  relation's liveness is core1's standing exchange in both serviced relations,
  so core0 owing the wire nothing for the width of an EEPROM write is the
  designed case. Re-entering the storage state machine from inside its own
  durable apply is not a case at all.

  While that gap is open the wear-leveling cache is the only writable copy of
  logical data: a write arriving from the gap updates the cache and MUST NOT
  reach the backing store, and a nested erase MUST be refused. That is not a
  dropped write — the consolidation the erase belongs to writes the whole cache
  next, and the CLEAN erase clears it — and any future widening of what the gap
  runs inherits that rule rather than replacing it.

  A reset-class key press reached by that nested pass MUST NOT prevent the
  caller's next step. The two class process-record seams
  (`system/era_nonsplit_board.c`, `split/era_split_board.c`) run the user hook
  first, then consume and latch `QK_BOOTLOADER`, `QK_REBOOT` or
  `QK_CLEAR_EEPROM` while `era_flash_slice_in_yield()` is true. Their
  top-level `housekeeping_task_kb()` drains the first action only after the
  main loop's current keyboard, protocol, raw-HID and deferred-exec calls have
  returned, by replaying the original keycode through `process_quantum()`
  (`system/era_flash_slice.c`). The whole QK action is deferred — CLEAN may not
  disable EEPROM in the gap and defer only its reset — and the latch is cleared
  before dispatch so one press produces one action even on a reset primitive
  that returns in a host test.

  **Two guards hold the reachable paths and a check holds the rest.** The
  wear-leveling interlock takes every writer arriving through
  `wear_leveling_write()` (`quantum/wear_leveling/wear_leveling.c`) and the transport skip keeps the wire out; both are
  code. The third part — that nothing else `action_exec` reaches from a gap
  arrives at the backing store — is a set that grows silently as the action
  layer grows, so every backing-store commit entry point asks
  `backing_store_commit_blocked_kb()` and refuses while a gap is open, and the
  refusals are published as `cross` (`era_capture_reading.md`). It is
  structurally zero. **Refusing is the safe side there and is not the safe side
  at `wear_leveling_write()`**, where a refusal would drop a user's edit and the
  cache-only path exists instead; past that interlock the caller is about to
  commit into a half-erased store, so there is no lossless option left.

  **Slicing without a gap that runs something is not this mechanism**, and
  three rules keep the distinction measurable. `sl`/`sy` and `stall_ms` exist
  so a build with the loop and no pass reads as what it is, and `max_ms` MUST
  NOT be re-bracketed to the shorter span — it would fall to one sector's width
  whether or not the gap runs, which is the shape of every metric this project
  has improved without the thing it names.

  **What the gap bounds is the unbroken span, not the scan rate**, and the two
  MUST NOT be confused when this clause is extended: the gap interval is one
  sector erase, so the keyboard is sampled at that hardware cadence and not at
  scan rate, and only returning core0 to the loop shortens it. The same
  boundary is why the wire sees no benefit — core0 publishes no responder
  snapshot for the width of the window whatever the slicing does.

  **A commit reached from inside a gap is the caller's work, not a second
  operation**, so any instrument bracketing the outer one MUST count depth
  rather than close on the first end. Closing on the first end cost a device
  capture its `max_ms`, and the impossibility that produced — an inner span
  wider than the operation containing it — is the only reason it was found.
  Counter semantics are canonical in `era_capture_reading.md`, the measured
  spans and their bands in `era_performance_gates.md`.
- Core1 MUST reserve bounded responder-result capacity before sending any
  success response **that publishes a result**. Source-push capacity MUST
  remain reserved from session/heartbeat traffic, and no accepted matrix may be
  ACKed before it is stored in that capacity.

  The qualifier is a sharpening, not a relaxation: the rule exists so a
  response that hands core0 work cannot be sent with nowhere to put the work.
  A section-less ACK to a bare poll hands core0 nothing and publishes nothing,
  and reserving a slot for it only to discard it was how a blocked core0 made
  an otherwise willing responder mute — three slots fill in 150 ms against
  measured core0 stalls in the hundreds. What may skip a reservation is exactly
  the response that would have released it unused.

## Stop Conditions

Stop and report before editing if an implementation requires any of the
following without a contract update that explicitly opens it:

- QMK core matrix changes.
- HOST source response outside an admitted HOST-PEER response slot, or a
  `HOST_PEER_HOST_SOURCE_RSP` section `era_wire_contract.md` does not list as
  open. A section on the initiator's core0-lane answer trips the same wire:
  that answer is the bare control ACK and carries no section at all
  (`era_route_contract.md`, **One carrier for the response section set**).
- An RGB or INPUT section in HOST-PEER source-push.
- `EEPROM_SYNC` shape, direction, domain, or authority outside the exact
  admitted surface (`era_closed_surface_contract.md`, and the contract it
  points at in `era_host_peer_storage_contract.md`). The class is in force and
  its execution is ordinary, so the tripwire is the surface, not the class.
- DUAL-HOST matrix/digest/mirror payloads.
- Row-array matrix payloads.
- Direct HID injection from split transport.
