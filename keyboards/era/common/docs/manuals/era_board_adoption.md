# ERA Board Adoption

Status: active
Genre: manual
Canonical for: what a new ERA board needs, whole — the copy-to-RAM policy and
what its marker changes beyond placement, the non-split capability boundary
and baseline, the storage adoption bundle and its five preconditions, and the
adoption checklist
Read when: adding a board, migrating one onto the resident image, or changing
anything a board must do to take the common layer

Per-file source ownership is `era_source_map.md`'s, the placement rules
`era_sram_residency_contract.md`'s, and the option declarations
`era_build_options.md`'s.

## Storage Adoption

**The EEPROM geometry the schema is laid out in is five preconditions, and
they are an include rather than a page to remember.** Split across a
family-named header and each board's own `config.h`, they leave a board outside
that family finding them one `_Static_assert` at a time in a file naming none
of the others. That is the same failure the copy-to-RAM bundle exists to
prevent, and it takes the same shape of fix.

`storage/era_storage_adoption_rules.mk` is the bundle; a board includes it from
`post_rules.mk`, above the ERA split fragment. It puts
`storage/era_storage_adoption.h` on `CONFIG_H`, which force-includes it into
every translation unit *after* the board's own `config.h` — the position that
lets each value refuse a conflicting board definition by name rather than
collide with it.

| # | Precondition | Supplied? | What catches it |
| --- | --- | --- | --- |
| 1 | `TOTAL_EEPROM_BYTE_COUNT == 24576` (`WEAR_LEVELING_LOGICAL_SIZE` 24 KiB, backing twice that) | yes | `era_host_peer_storage.c`'s span assert |
| 2 | `DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE` **exactly** 16 KiB | yes | an *equality* against the shared core0/core1 image, so "large enough" is not the requirement and QMK's own computed default does not satisfy it |
| 3 | `EECONFIG_SIZE == 37` — the board declares no `EECONFIG_KB_DATA_SIZE`/`EECONFIG_USER_DATA_SIZE` | no, refused | the header's own `#error`, and behind it the `ERA_EEPROM_CONFIG_ADDR == 37U` assert, which is the one that still fires when a *keymap* `config.h` sets it |
| 4 | `VIA_EEPROM_MAGIC_ADDR == ERA_EEPROM_CONFIG_END`, and the non-VIA build's `DYNAMIC_KEYMAP_EEPROM_ADDR` equivalent | yes | the layout-options-at-296 and keymap-at-297 asserts |
| 5 | the board's own config struct fits `ERA_EEPROM_KEYBOARD_CONFIG_SIZE` (8 bytes) | no, named | a `_Static_assert` beside the struct — `sirind/common/tomak_era_keyboard_config.h` is the shape to copy |

**An include and not variables**, for the residency bundle's reason: the parts
fail as a set, and one of them cannot be a `-D` at all — `VIA_EEPROM_MAGIC_ADDR`
takes a macro from `era_eeprom_layout.h` for its value, and the QMK units that
read it include no ERA header. What make owns is the **refusal**:
`era_split_qmk_rules.mk` declines `ERA_SPLIT_EEPROM_SYNC_ENABLE = yes` on a
board that has not taken the bundle, before any compile and naming both files.

**It is deliberately not gated on the sync selector.** The stored layout is a
fact about the board, not about whether its halves synchronise it; gating it
would give a board's non-VIA keymap — which has no sync engine at all — a
different EEPROM geometry from its VIA keymap.

## Non-Split Capability

The engine builds for a non-split board as well as a split one. **The guard
runs through one boundary per file with the peer half wholly inside it** — the
shape that fails is row helpers inside `#ifdef SPLIT_KEYBOARD` while the peer
functions that call them sit outside, which compiles on every board that has a
second hand and fails on the first board with none.
`era_matrix_engine_local_changed()` is the only engine entry point a non-split
build sees; everything else in `system/era_matrix_engine.h` is the split
relation's view of the same engine, and `peer_rows` is split-only storage
rather than a field a non-split board carries empty. A function's absence from
that header means it has no caller outside the engine, never that the engine
does not do it.

A board is eligible for `ERA_RP2040_MATRIX_ENABLE = yes` when it is RP2040,
`COL2ROW`, and carries no matrix source of its own — `era_rp2040_matrix_pio.c`
rejects the other two at compile time rather than degrading. Setting it implies
`CUSTOM_MATRIX = yes`, replaces the stock scan, and brings the ERA debounce
runtime with it.

**Every board under `keyboards/era` runs the engine**, in its `default` keymap
build as much as its `via` one, and it is the manifest default
(`ERA_RP2040_MATRIX_ENABLE ?= yes`) rather than a per-board adoption.

**The engine does not carry the split with it, and that is measured rather than
argued.** A non-split board with the engine on links zero peer, host-peer,
transport, router or transaction-backend symbols, and links no stock QMK matrix
either — `CUSTOM_MATRIX` replaces the scan outright. The dependency runs one
way and `era_split_qmk_rules.mk` states it: split transport *requires* the
engine, the engine requires nothing of split.

What stays per-board is the evidence. A board that has never scanned a key
through this engine owes device verification before it ships one, and a build
result does not substitute for that on the scan path. **The settle is the
specific debt**: the 128-cycle `ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY` is
tuning measured on TOMAK79H — on the CPU engine, and carried unchanged by the
sampler that replaced it — and every other board carries that number without a
measurement of its own. A matrix that settles more
slowly — longer traces, a weaker pull-up, more capacitance — produces a missed
or ghosted key, not a build failure, so nothing before the device says anything
about it: the reading is the board's own to take.

## Copy-To-RAM Policy

**Every ERA RP2040 board runs the image from SRAM. XIP is retired** (owner
decision, on the ground that performance is hard to control on it). Residency
is not a capability a board adopts: it is the default, and a board that is not
on it is an exception that owes a reason. **All twenty-two RP2040 boards under
`keyboards/era` are on it**, and there is no ERA XIP board. `sirind/brick65` is
atmega32u4, where a copy-to-RAM image is not a configuration a part with 2.5 KB
of SRAM can hold; it is a permanent exception rather than a debt, and it takes
none of the ERA layer.

`system/era_sram_resident_rules.mk` is the bundle — linker script, marker, map
emission, pre-copy window object and vector defaults — and a board becomes
copy-to-RAM by including it. It is common-layer rather than split: residency
only looks split-specific while every copy-to-RAM board is a split one. A
non-split board links and lays out correctly under it — `.text`, `.vectors` and
`.bss` all move to SRAM, the `.flash_startup` carve-out keeps only boot2 and
the startup trio, and no new flash→SRAM veneer appears.

Every board and keymap is measured by `common/tools/era_residency_gate.sh`,
which is board-agnostic and is the same code the TOMAK79H launcher runs — free
ram0 against its floor, the `.vectors` table, and the no-allocator check. The
gate and its pass bands are canonical in `era_performance_gates.md`.

**What the marker changes beyond placement is the larger half.**
`ERA_SRAM_RESIDENT_IMAGE` removes the interrupt mask around the flash
program/erase and takes the per-sector sliced erase with its keyboard-pass
yield. On a board with no flash-resident code the mask guards nothing, which is
why it is a gate and not a deletion; the slicing is then live, and a non-split
board gets the pass and the counters without the `stall_ms` bracket, which is
wired from whoever owns the EEPROM commit hooks and today is the split
scheduler only.

**The pre-copy arm is not separable from the bundle**, and the reason is the
checks rather than the code. `era_boot_core1_halt.c` would compile and link on
an XIP board — it carries no residency guard and no section attribute of its
own — but two things that make it safe live in the linker script: the
`early_hardware_init_pre` `ASSERT`, the only proof that ERA's definition won
over QMK's weak stub, and the `.era_bootloader_magic` claim with its size
assertions. Without the first, a wrong `SRC` path leaves the stub linked with
`PRE_COPY_ARM` still defined, and `__late_init()` then never arms and never
re-arms: **double tap stops working permanently and silently**, on a board
whose only firmware-independent recovery is that window. That is why the bundle
is an include and not a variable — the linker script is what catches a partial
adoption.

**What the double-tap window becomes on a resident board** is the one
user-visible behaviour the bundle changes. The boot stall goes: QMK's stock
blocking `__late_init()` (`platforms/chibios/bootloaders/rp2040.c`) arms the magic and busy-waits the whole timeout before
clearing it, where non-blocking the timeout is only a guaranteed minimum width
and nothing waits for it. Only an eligible physical RUN arms, so POR, recovery
and software resets — `QK_REBOOT`, the VIA reset, bootmagic — arrive CLEAR and
are not silently re-armed, which is why a power-on does not land in the
bootloader and why `bootloader_jump()` still reaches BOOTSEL with no window at
all. And the first tap is held clear through the ROSC stable-release guard,
15–102 ms.

**Three conditions in the shared layer decide whether a board outside the tomak
family can take the bundle at all**, and each is mechanical rather than
something an adopting session must know:

- **`RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM` without
  `..._NONBLOCKING` does not compile** — it assigns `double_tap_armed`, which
  is declared only under the second.
- **Setting both without a window closer bricks the board**, permanently: the
  only caller of the window-closing task is `era_common_features.c`, and with
  no closer the magic is never cleared and every physical RUN enters BOOTSEL.
- Both are one `$(error)` in `system/era_sram_resident_rules.mk`, which refuses
  a board that has not included `era_common_qmk_rules.mk` and names both files
  before any compile — what the bundle depends on is the whole common layer,
  not one emission. The window *closer* used to be the other half of the second
  condition, catchable by nothing. The class skeleton owns
  `housekeeping_task_kb` and supplies it (**The Adoption Checklist**), so the
  loss is possible only for a board that opted out of the skeleton by name.
- **A board with no `RP2040_BOOTLOADER_DOUBLE_TAP_RESET` has no
  `magic_location`**, and two linker `ASSERT`s fail. Every ERA board defines it
  at 1000U.

**Five vector slots are conditional, and each condition is the driver's own.**
The failure class has one shape: an unconditional statement in
`era_vector_defaults.c` about what an ERA image installs, true of every ERA
board when written and false at the first board with a different driver set.
Answer such a finding with the driver's condition, never by deleting the entry.

| Slot | Condition | Why the unconditional form is wrong |
| --- | --- | --- |
| `Vector5C`/`Vector64` (PIO0_IRQ_0, PIO1_IRQ_0) | whichever slot the ERA wire backend leaves, chosen by `SERIAL_PIO_USE_PIO1`; both when no backend is linked | *a* backend claims one PIO slot, not *the* backend — omitting `Vector5C` assumes the wrong one, and an unconditional `Vector64` duplicates the moment a board sets the selector |
| `Vector50` (PWM_IRQ_WRAP) | `HAL_USE_PWM` with any `RP_PWM_USE_PWMn` | the ChibiOS PWM LLD installs it under exactly that, so claiming it unconditionally is a duplicate symbol on a board with QMK `backlight` on hardware PWM |
| `Vector6C`/`Vector70` (DMA_IRQ_0/1) | `RP_DMA_REQUIRED`, which `RP2040.mk` defines for `WS2812_DRIVER=vendor` and `hal_spi_lld.h` for a board using SPI | omitting them assumes every ERA image installs them for RGB, which is a statement about RGB — a board with no addressable LED leaves both slots in the flash carve-out |

Reading those slots needs `hal.h`, which the file includes for exactly this.
Getting a condition wrong fails the link or the gate and never the boot. Run
the `.vectors` gate when adopting a board — `era_residency_gate.sh` takes the
ELF and nothing else, it is the only thing that reads what was actually linked,
and it has caught this class three times.

## Non-Split Board Baseline

What a non-split ERA board gets: the whole ERA VIA custom UI, the copy-to-RAM
image, the pre-copy double-tap arm, and the matrix engine. **No shared-code gap
remains** — adoption is per-board content and device verification, not
common-layer work.

The engine's raw backend is common-layer and not a per-board choice: every
RP2040 board samples through the PIO+DMA backend, which is the engine's only
one — why there is no second is `era_build_options.md`'s — with the row and
column pins derived from that
board's `keyboard.json` exactly as the CPU engine derived them, the frame
padded to the next power of two of
the board's row count, and the decode tables sized to its `matrix_row_t`. The
sampler claims PIO1 whole and two DMA channels from the ChibiOS allocator; a
board that puts anything of its own on PIO1 is the one thing this default
cannot accommodate, and no ERA board does.

The VIA surface needs no split-specific change: `era_common_qmk_rules.mk` adds
every feature's VIA unit for any keymap that has VIA, so a board that includes
it gets SOCD, KKUK, debounce, tapping, all eight tapdance slots and the EEPROM
CLEAN confirm pair, under the `VIA_ENABLE` gate in **The VIA surface and its
gate** below. Only `era_split_via_sync.c` is split-gated.

Three exceptions are structural rather than gaps.

- **SYNC is split-only** and correctly absent. A non-split board has no relation
  to synchronize.
- **The USB suspend loop is QMK's on a non-split board and ERA's on a split
  one**, and this is the boundary a session is most likely to get backwards.
  ERA split boards must set `NO_USB_STARTUP_CHECK = yes` —
  `era_split_qmk_rules.mk` refuses anything else, because a half in the PEER
  role has no local USB and `keyboard_task()` has to keep running — and that
  switch **deletes QMK's own suspend loop**
  (`tmk_core/protocol/chibios/chibios.c`). Everything in
  `era_split_keyboard.c` around the sleep predicate, the unconfigured
  SUSPEND-to-INIT remap and the composed-input-edge remote-wake requester
  exists to replace what that switch removed. A non-split board sets neither
  and keeps the stock loop: `USB_DRIVER.state == USB_SUSPENDED` drives
  `suspend_power_down_quantum()` — RGB Matrix, RGBLIGHT, backlight and the
  indicator LEDs — and stock remote wake. Verified in cflags rather than
  inferred: `era/linx3/n86:via` carries neither `NO_USB_STARTUP_CHECK` nor
  `RP_USB_SYNC_SUSPEND_AFTER_REMOTE_WAKEUP_SET`, and links `suspend_power_down`
  and `rgb_matrix_set_suspend_state`.

  **The sleep decision takes two detectors on every ERA board** (owner
  decision): where a board enables lighting sleep, the host's explicit USB
  suspend **and** the loss of USB frames each mean sleep.
  `system/era_usb_session.c` is common rather than split and reaches every ERA
  board — the split layer ORs `era_usb_session_frames_lost()` into its own
  predicate, a non-split board gets the arm applied by that unit beside QMK's
  loop rather than instead of it, and both drive the same two
  `suspend_*_quantum()` entry points so a board's lighting cannot end up
  half-suspended. Routing the split apply through
  `suspend_power_down_quantum()` is **rejected on evidence**: it would credit
  the render-policy flush for a frame it paints black.

  **The two arms are one physical event seen twice, not two USB signals.** A
  host suspends by stopping SOF and the controller latches SUSPEND after 3 ms
  of idle bus; the first arm reads the ChibiOS state machine's conclusion, the
  second reads the frame-counter register, and what the second buys is a
  detector that does not depend on that state machine. They diverge exactly
  where the machine has been taken back out of `USB_SUSPENDED` by control
  traffic while the bus is still dead, and where the ERA remap rewrites the
  state — the ghost-power case and the remote-wake case both. A session reading
  them as two signals looks for a packet that does not exist.

  **The frame arm counts only observed absence**: the age
  verdict requires the last register read to be fresh, and a sample landing
  after an unobserved span treats an equal counter as unknown rather than
  as stillness — an unobserved span is not evidence of absence, the same
  doctrine that already governed the counter compare. Without the clause, a
  flash window starved the sampler while the change-stamp froze, and the
  first post-window resolve read the frozen age past the threshold and
  pulsed lighting sleep for one frame — the EEPROM SYNC lamp's mid-era
  blink, convicted by the breaker latch with `slp` == `brkms` to the
  millisecond on consecutive operations. One fresh sample re-validates either
  verdict, so a genuinely dead bus still sleeps within one threshold of the
  sampler resuming.

  **`USB->SOFRD` is read-with-side-effect, and that binds any reader of the
  frame counter.** The RP2040 clears the `DEV_SOF` interrupt flag when the
  register is read, and that flag is load-bearing for exactly one path:
  `usb_lld_wakeup_host()` arms `USB_INTE_DEV_SOF` when ERA requests a remote
  wake, because remote wakeup raises no wakeup interrupt of its own, and the
  SOF handler is where `_usb_wakeup()` runs. An unguarded poll can consume the
  interrupt that ends the suspend ERA itself asked to end. `era_usb_session.c`
  stands off entirely while that enable bit is set and rate-limits the read to
  the 1 kHz the signal has; both guards are the correct reading, not cost
  dodges.

  **`rgb_matrix_set_suspend_state()` has one owner on a split board, against
  five reachable writers.** The rule is canonical in
  `era_authority_contract.md`'s **Lighting Sleep Ownership** and is not restated
  here; what this row owns is where it lives. The resolver in
  `era_split_keyboard.c` is the sleep decision's one writer: it reads the
  relation from `era_split_transport_scheduler_lighting_sleep_owner_is_wire()`,
  takes its answer from this half's own USB session or from the wire, and writes
  the gate only on an edge of its own value. `era_host_peer_response.c`
  publishes the wire's sleep fact into it rather than writing the gate, and
  `era_host_peer_source_snapshot.c` captures the resolver's decision rather than
  the gate.

  What remains reachable is two board-side **render overrides** — the
  Caps-indicator resume and the launch-signal resume — plus `quantum.c`'s own
  wake on a non-split board. They are not owners of the decision: each forces a
  frame visible while it runs and re-asserts for as long as it does, and the
  resolver's edge-only write cannot fight them. The paragraph this replaces
  argued the defect was survivable because those two are conditional and
  self-limiting; that argument was correct about them and said nothing about the
  two writers that actually collided, which is the shape a "no owner" note tends
  to have.

  **Only one of the two ChibiOS deltas is gated.** The SETUP-packet wake in
  `hal_usb_lld.c` — RP2040 can present a SETUP packet as the first host
  activity after suspend, with no `DEV_RESUME_FROM_HOST` and no SOF, so without
  it the board stays in `USB_SUSPENDED` after the host resumes — is
  **unconditional**, and every board in this fork gets it. The re-suspend delta
  is not: `RP_USB_SYNC_SUSPEND_AFTER_REMOTE_WAKEUP_SET` is emitted only by
  `split/era_split_usb_sleep_rules.mk` and repairs that sequence inside
  ChibiOS, for split boards. A non-split board reaches the same outcome from
  outside ChibiOS — QMK's loop exits with the bus still dead and the lighting
  back on, and one pass later the frame-loss arm puts it out again.

  **All of this is a source reading.** Whether the RP2040 USB LLD reaches
  `USB_SUSPENDED` at all when the host dies behind a powered hub is unmeasured
  on any board, split or not; the evidence rule in `AGENTS.md` puts
  one measurement above all of it, and the suspend area is where this project
  has twice patched from a mechanism it had not observed. That measurement is
  owed, and this paragraph is the only place it is written down.
- **ERA spans three lighting families and a board may be in none**, and which
  is in its own `keyboard.json`. RGB Matrix is ten boards, PWM backlight eleven,
  RGBLIGHT five, and four boards are in two at once — `sirind/klein_sd` has a
  backlight and a per-key matrix, the three `comm/classicd_*` with a strip have
  a backlight and underglow. `newone/h1` is in none: no backlight, no
  addressable LED, not even a lock indicator, which makes it the board that
  answers what the common layer costs a keyboard with nothing to light.
  Converting a board from one family to another is a user-facing lighting
  change and an owner decision, not part of adoption.

  **The counts are here rather than the member lists, and that is the trade.**
  A list of five boards was correct until the day six more backlight boards
  arrived at once; a count is wrong the same day but says less while being
  wrong, and `grep -l '"backlight"' keyboards/era/*/keyboard.json
  keyboards/era/*/*/keyboard.json` re-derives any of them in one command.
- **The ERA RGB improvements are RGB_MATRIX-only.** `RGB_MATRIX_RENDER_POLICY`,
  the independent indicators, and the deferred config flush have no
  RGBLIGHT or backlight equivalent, so a board outside that family takes none
  of them and its VIA lighting menu carries `id_qmk_rgblight_*` or
  `id_qmk_backlight_*` instead of `id_qmk_rgb_matrix_*`.
- **The first question a lighting board asks is whether it wants a surface at
  all.** Three ERA boards answer no: `divine`, `sirind/klein_hs` and
  `sirind/klein_sd` wire the PWM backlight pin to nothing but their lock LEDs,
  so its off state is a dark keyboard rather than a preference, and they ship
  no lighting menu and no lighting keycode (owner decision 2026-08-18).
  **Answering no is more than deleting the menu**: with nothing left that can
  set the rail, a stored `enable = 0` from an earlier keymap would be
  permanent, so `ERA_BACKLIGHT_ALWAYS_ON` repairs the stored block before
  `backlight_init()` reads it. A board in this shape sets its brightness in its
  own `config.h` — `BACKLIGHT_DEFAULT_LEVEL`, a product decision — rather than
  offering one.

- **A lighting surface past VIA's own channel then goes one of two ways.** VIA
  answers brightness/effect/speed/colour itself on its RGB-matrix (3), RGBLIGHT
  (2) and backlight (1) channels. Past that:
  - **A common feature unit**, when the behaviour belongs to the family rather
    than to one product. Two exist. `features/era_backlight.[ch]` is the PWM
    backlight's four effects, behind `ERA_BACKLIGHT_EFFECT_ENABLE`, on
    keyboard-channel ids `0..3`; a second backlight board turns the selector on
    and is done. `features/era_rgb_indicator.[ch]` is one or two lock-indicator
    slots on named RGB Matrix LEDs, behind `ERA_RGB_INDICATOR_ENABLE`, on ids
    `6..12`; a board states which LED each slot paints in its own `config.h`
    and turns the selector on.
  - **The board hooks**, when it is one product's: the weak
    `era_board_via_get_value()` / `era_board_via_set_value()` pair in
    `system/era_board_hooks.c`. Two boards override them —
    `newone/common/odessey_common.c` for the RGBLIGHT indicator trio plus
    Velocikey, and `sirind/common/tomak_common.c` for badge lighting and the
    lock indicator.

  Both land on the keyboard channel and **the common router runs first**, so
  `0..3` is the backlight feature's wherever its selector is on and the board's
  everywhere else. The backlight ids are a choice in neither case — they are
  what the shipped definitions already address — and the indicator's are the
  one band on this channel that was chosen, which is why they were chosen to
  overlap nothing (`era_identifier_map.md`).

  **A continuous lighting control's persistence is deferred by the gate that
  owns its save event, and a new one is covered by joining a list rather than
  by growing a timer.** The rule, in four parts: the number is
  `ERA_STORAGE_QUIET_DEFER_MS` and there is no second one; the arm is the VIA
  save and never the set, which is what the `_noeeprom` discipline on every
  setter is for; there is no maximum-age flush beside the quiet timer, because
  a user holding a slider produces a continuous stream of approvals and a
  max-age flush would write flash during exactly the drag the gate removes; and
  **one persistence range has one gate**. Three exist, one per save event —
  VIA's RGB-matrix channel (`quantum/eeconfig.h`'s helper, instantiated in
  `quantum/rgb_matrix/rgb_matrix.c`), VIA's RGBLIGHT channel
  (`quantum/rgblight/rgblight.c`), and the keyboard channel
  (`system/era_board_hooks.c`) — and a keyboard-channel claimant joins the
  third by being added to `era_common_via_keyboard_channel_save()`
  (`system/era_common_via.c`), which is the same list the save arm already
  requires it to be added to. A board that instead writes a timer of its own
  behind that gate has built the double defer the tomak family carried until
  2026-08-24.

  **A discrete control keeps its immediate write**, and that is a
  classification rather than an oversight: tap dance, SOCD, the debounce,
  tapping and mousekey pages and the NKRO toggle ship no `range` and no
  `color`, so a save there is one deliberate user action and can never burst.
  A discrete control that *shares* a config record with a continuous one rides
  that record's gate and gets no timer of its own.

  **Two discrete controls persist at the *set* rather than at the save, and
  both are toggles**: the NKRO bit (`features/era_nkro_via.c`, QMK's
  `eeconfig_update_keymap()`) and odessey's Velocikey
  (`newone/common/odessey_common.c`, through QMK's own
  `rgblight_velocikey_toggle()`, which is an eeprom-writing function by
  design). Both compare first and write nothing when the bit already holds the
  requested value, so neither can burst, and neither is a counter-example to
  the arm rule above — that rule is about the *continuous* controls, whose
  setters are `_noeeprom` without exception. Read as a property of the whole
  surface it would be false, which is why it is written here with its two
  exceptions rather than as "no ERA setter reaches EEPROM".

  **A common lighting unit takes QMK's weak render hooks strongly**, which the
  board-hook route does not: `era_rgb_indicator.c` defines
  `rgb_matrix_indicators_kb`, `rgb_matrix_indicators_advanced_kb`,
  `led_update_kb` and `rgb_matrix_render_policy_kb`, so a board with the
  selector on may define none of them. That is the same shape as the backlight
  feature's exclusivity against a board's own `0..4` handler, and it is stated
  at the selector's declaration rather than left to the link error.
- **A migrated definition can arrive without the firmware behind it, and one
  did.** `linx3/n8x`'s `Lighting > Backlight` menu — brightness `0..10` against
  its ten declared levels, and a dropdown of None / Breathing / Blink-Out on
  Keypress / Blink-In on Keypress with a period and a speed — came over with the
  rest of that definition from the vendor's published v3 file. The
  implementation never did: neither the board's upstream QMK submission nor any
  board unit this repository has held for it ever had a backlight arm, so all
  four controls answered `id_unhandled` until the common unit above was written
  to the menu's own contract. **Check a migrated definition against a handler,
  not against its siblings** — this one was consistent with every other ERA
  definition and still addressed nothing.

  **The same gap points the other way as often, and it is the harder one to
  see.** A definition addressing nothing is at least visible in the definition;
  hardware with no surface is visible only in `keyboard.json`, which is why the
  2026-08-18 board sweep found four boards in that state and only the one above
  in the first. `divine` declared a five-level PWM backlight — which on that
  board is the supply the three lock indicators run on — with no lighting menu
  and no lighting keycode anywhere in its definition; `sirind/brick65s`
  declared its whole two-LED panel and offered no way to reach it;
  `linx3/n86` and `linx3/n87` had the LEDs and no indicator at all. **So the
  check runs in both directions: every declared piece of lighting hardware owes
  a surface or a decision that it has none, and every menu entry owes a
  handler.** Neither half is answerable by reading the definitions alone, and
  the two halves of the first one are not the same finding: three of those four
  boards got a surface and `divine` got the decision, which is why the answer
  above is a question a board *answers* rather than a gap it fills.

## The Adoption Checklist

What a new ERA board needs, whole:

- a `keyboard.json` — pins, matrix, LED positions, layouts, the lighting
  engine;
- a `config.h` — the board facts of `era_build_options.md`'s rule 3: the
  tap-dance keycode base, the double-tap reset settings, any USB identity;
- a `post_rules.mk` — the feature set written out explicitly, with the ERA
  includes under it, and for a split board that wants EEPROM sync the storage
  adoption bundle among them (**Storage Adoption** above);
- a `via` keymap;
- the owner-authored `*-VIA.json` definition, which is firmware-local
  release/manual-upload product content and is not part of the firmware
  (below);
- **optionally** a board `.c`, for genuine product behaviour only, and
  **optionally** a board `.h` naming the tap-dance slots for a source keymap;
- the residency gate on both keymap builds, and the device first-run.

**A board `.c` is not required, and what that buys is a safety property rather
than a line count.** The QMK hooks every board of a class would otherwise wire
identically are owned by its class skeleton, six each and not the same six:
`system/era_nonsplit_board.c` takes `housekeeping_task_kb`, `matrix_init_kb`,
`eeconfig_init_kb`, `process_record_kb`, `via_init_kb` and the one
`via_custom_value_command_kb`; `split/era_split_board.c` takes
`housekeeping_task_kb`, `keyboard_pre_init_kb`, `keyboard_post_init_kb`,
`suspend_wakeup_init_kb`, `process_record_kb` and the one
`via_custom_value_command_kb`. A board extends them through the weak hook set
in `system/era_board_hooks.h`. **No non-split board keeps a `.c` at all**, and
the nineteen of them are the check rather than the claim. The last two to keep
one, `linx3/fave65s` and `sirind/brick65s`, each held a single
`rgb_matrix_indicators_kb()` painting a fixed colour on a fixed LED; that
became the common indicator feature above on 2026-08-18, and what each board
kept is the LED index, in its `config.h`, where it is geometry. The seven
adopted the same day arrived with two more — `newone/a1` and `comm/et_tkl` each
carried the legacy backlight effect layer their branch wrote before this one
existed — and neither needed a line of it kept.

**That is what retires the one hazard nothing mechanical caught.**
`era_common_features_task()` is the only caller of
`rp2040_bootloader_double_tap_reset_task()` in the tree, so a board that took
the residency bundle without running it never closed the window armed before
crt0's copy loops and entered BOOTSEL on every reset for the rest of its life.
`era_common_qmk_rules.mk` and the bundle's `$(error)` catch a missing
*include*; **nothing** caught a board `.c` that stopped calling the task, which
is why all fifteen carried that reason as their only per-board comment. With
the call in the common layer the failure class does not exist for a board that
takes the skeleton. A board that sets `ERA_BOARD_COMMON_ENABLE = no` writes its
class's six hooks itself and re-acquires the hazard, which is why the reason
now lives at that selector's declaration.

**What a `via` keymap does not bring is the VIA definition JSON.** That file is
firmware-local product content — key positions and the custom menu layout — and
is owner-authored; the firmware compile does not consume it. The release/user
workflow may supply it for manual **Load Draft Definition** as described in
`docs/user/readme_split.txt`, but its presence does not make it an application
lookup source or an official VIA definition. The app's bundled ERA source is
`the-via-eerraa/era-definitions/custom/v3`; official ownership is
`the-via/keyboards/v3`. QMK-local `*-VIA.json` files are the original
usevia.app-compatible Draft Definitions: do not add Custom VIA-only controls or
reshape them for the Custom app. Custom VIA JSON is maintained in the app
repository and is not copied into QMK. **Every board in the tree has a local
copy** — a split
board has two, one per half — and they sit beside the `via` keymap as
`<BOARD>-VIA.json`, so a count against the boards is the check. This is a
release/adoption requirement on a *new* board rather than an open firmware gap.

Re-proving the non-split path is one command on any eligible board, and is
worth running after any change to the guard boundary:

    qmk compile -kb era/newone/odessey60s -km via

Two things to check in the result rather than just its exit code: the decode
loop matches the split build's, and `arm-none-eabi-nm` links no peer/host-peer
symbol.
