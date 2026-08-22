# ERA Identifier Map

Status: active
Genre: map
Canonical for: the identifier space no header owns — modes, routes and route
reasons, storage service causes, payload and status ids, VIA sync value ids,
the USB-session thresholds — and every retired number that stays reserved
un-reused
Read when: naming new code, diagnostics, routes, payloads or docs, or resolving
an identifier a capture carries

**A retired identifier's number is never recycled.** Captured console lines are
dated by what their numbers meant when they were taken, so a re-use would make
an old reading silently wrong instead of merely historical. Every "retired,
stays allocated" statement below is that one rule applied.

**Decoding a console line is `era_capture_reading.md`'s**, and the split is by
what the name denotes: an identifier here names a thing in the source, a field
there names a column in a `WIRE_DIAG` capture. The rule above serves both, and
it is why a capture from an older image still decodes against this file.

## Modes

`era_split_mode_planner.h` names the five relation modes with their values.
Values 5 and 6 were `HOST_HOST_LEFT`/`HOST_HOST_RIGHT`, retired un-reused.

## Routes And Route Reasons

`era_split_wire_router.h` names both enums, each with an explicit `_NONE = 0`.
What each reason means, and which wire operation it runs on, is canonical in
`era_route_contract.md`.

Three values used to sit here allocated but unproduced -- the
`RUNTIME_PUSH` kind and the `HOST_SOURCE_RESPONSE_POLL` and
`HOST_PEER_LIVENESS` reasons -- kept so that a capture predating R2 still
decoded. They went with the captures that needed them, so **every value in both
enums has a producer**.

**Read `rr`, not `rk`, to tell two runtime routes apart.**
`ERA_SPLIT_ROUTE_HOST_PEER_HEARTBEAT` is live and keeps its value, and core1's
standing service stamps that kind into the transaction timing for both live
runtime reasons — so a HOST-PEER capture reads the heartbeat kind for traffic
that is not the heartbeat route. `rk` alone does not distinguish a
core0-selected route from a granted exchange, and in a serviced relation it is
always the latter.

## Storage Service Causes

`HOST_PEER_STORAGE`, `HOST_PEER_STORAGE_IDLE_PROOF`, and
`HOST_PEER_STORAGE_ACTIVE` are contract-level service causes only. Storage is
a dedicated cold-task lane, not an owner route kind
(`era_route_contract.md`), so **no C enumerator of any of those names exists**,
and the probes `IDLE_PROOF` describes are not a periodic patrol. **Do not add
them to `era_split_wire_router.h`** — that would create the general owner-route
kind the route contract refuses.

**Every probe is queued from a comparison of both halves' facts**: the summary
cell — the mandatory relation-open audit sweep and every later
`SUMMARY_STATUS` result — and the conflict cell when this half loses. Those are
the only two writers of `probe_pending_mask`. A peer advertisement reaches
`era_host_peer_storage_note_host_news()`, which arms a summary refresh and
touches that mask nowhere; the mechanism is canonical in
`era_host_peer_storage_contract.md`.

## Payloads

`era_split_wire_protocol.h` names the payload classes, the HOST-PEER op ids,
the HSRSP section mask bits, the visual-resync reasons, and the RGB, mask, and
time-anchor field widths — each with its value. `era_split_eeprom_sync.h` names
the storage ops (`0xE0`..`0xEE`: the pull set, the sync-status pair, and the
push set, with `0xEF` reserved), the push phases, and the seven portable
domains.

**Two payload kind values are retired and stay allocated un-reused**, so the
`rsp` kind numbering in a captured log keeps its meaning: `6` was `DUAL_HOST`
and `4` was `ERROR_NACK` (class `0x40`). Every op byte the split layer writes
is `0x10`, `0x20`, `0x21` or `0xE0..0xEF`, and the `0x40` class falls to the
`default` reject.

The status ids below stay because the cause timeline decodes against them
(`era_capture_reading.md`); the names above resolve in a header, so they do
not.

Storage status ids are `MATCH=0`, `TRANSFER=1`, `APPLY_READY=2`,
`COMPLETE=3`, `ABORTED=4`, `POLICY_CLOSED=5`, `UNSUPPORTED_DOMAIN=6`,
`UNSUPPORTED_SCHEMA=7`, `SIZE_MISMATCH=8`, `STALE=9`, `BUSY=10`,
`INTEGRITY_FAIL=11`, `RESULT_FULL=12`, `TIMEOUT=13`, `ROLE_CHANGED=14`, and
`SOURCE_CHANGED=15`. Exact payload shapes and response meaning are canonical
in `era_host_peer_storage_contract.md`.

**`RESULT_FULL` has no producer and is kept anyway**, which is the one place
these ids differ from the route enums above. It is a wire status id in a
numbered sequence, so deleting it would renumber `TIMEOUT`, `ROLE_CHANGED` and
`SOURCE_CHANGED` -- three live values -- to retire one label. Do not go
looking for the responder that returns it.

Control-only semantic names:

- `HOST_PEER_HEARTBEAT`
- `HOST_PEER_ACK_STATUS`

These are not class/op identifiers.

## VIA Keyboard-Channel Value IDs

Channel `id_custom_channel` (0) carries five claimants and the split between
them is by band, not by owner, because the router runs the common ones before
the board's (`system/era_board_hooks.c`).

| Band | Owner |
| --- | --- |
| `0..3` | the PWM backlight effect layer — brightness, effect, breathing period, blink speed (`features/era_backlight_via.h`), only where `ERA_BACKLIGHT_EFFECT_ENABLE` is on |
| `0..4` | otherwise a board's own, through the weak `era_board_via_get_value`/`_set_value` pair: the tomak family's badge lighting and lock indicator, odessey's indicator trio and Velocikey |
| `5` | the NKRO toggle (`features/era_nkro_via.h`) |
| `6..12` | the RGB Matrix lock-indicator slots — a master role switch, then a source, a brightness and a colour per slot (`features/era_rgb_indicator_via.h`), only where `ERA_RGB_INDICATOR_ENABLE` is on. A one-slot board answers `6..9` and declines the rest |
| `32..71` | the eight tap-dance slots, five ids each (`features/era_tapdance_via.c`) |
| `72..79` | exact-ms tap-dance terms for TD0–TD7, additive, 2-byte big-endian milliseconds (`features/era_tapdance_via.c`) |

**The `0..3` overlap is real and is held apart by the selector, not by the
numbers.** The backlight ids are not chosen: they are what the shipped
definitions of the backlight boards address, carried over from the vendor's
published v3 files, so the firmware has to answer there or answer nowhere. No
board has both a PWM backlight and a keyboard-channel handler of its own, and a
board that grew both would have to give one of them up.

**`6..12` overlaps nothing, and that is the one thing about it worth
recording** (owner decision 2026-08-18). The indicator feature was drafted to
extend odessey's `1..3` with a second slot at `4..6`, which would have put a
brightness slider on `5` — the NKRO toggle, answered by the common router
*before* the board hook, so the slider would have toggled NKRO instead. Since
these ids were being written for the first time rather than inherited from a
shipped definition, they were free to start above the contested band, and a
board that later grows both its own handler and this feature gives up neither.
**A new claimant on this channel checks the whole table above, not the band it
was aiming at.**

`72..79` is the G1-approved exact-ms term band. It does not reuse the legacy
term field ids `36,41,46,51,56,61,66,71`. Channel 15 value id `5` is the matching
exact global tapping term (`features/era_tapping_via.c`); ids `1..4` on that
channel stay the legacy 1-byte/10 ms global term and the three booleans.

## VIA GET_KEYBOARD_VALUE selectors

Command `0x02`. VIA uses `0x01..0x04` on GET and `0x05` on SET. ERA's
polling-first revision query is selector **`0x06`**
(`system/era_state_sync.c`). It is not a custom-channel value id.

## VIA Feature Channels

Every channel number resolves in a header — the feature ones in
`storage/era_eeprom_layout.h`, the system one in `system/era_via_system.h` —
so this section lists none of them. What it records is the one thing no header
says: **`13` was never allocated and is not a retired identifier.** Channels
`10`, `11`, `12`, `14` and `15` arrived in one commit with `13` already absent,
and no commit on any branch has ever defined it, so the mouse page taking it in
2026-08-18 recycles nothing and the rule at the top of this file is not in
play. What no git query can answer is whether a since-deleted plan document
once reserved it; what is established is that no firmware ever answered there.

VIA itself reserves `0..5` (`quantum/via.h`), ERA uses `9..15`, and `6..8` are
idle. **The channels are not renumbered to close that gap**, and the reason is
not tidiness in either direction: a 256-wide namespace with seven claimants has
no scarcity to relieve, the VIA menu order comes from the definition JSON rather
than from the channel number so a renumber is invisible to a user, and VIA
caches a definition per (vendorId, productId) — so an old definition against
new firmware would write to the wrong channels. Zero upside, real hazard.

## VIA Sync Value IDs

`era_split_via_sync.h` names value ids 5..7. The parent ids, the three child
`*_EFFECTIVE` ids and the separate HOST-PEER/DUAL-HOST EEPROM ids are retired
and their numbers stay un-reused.
`ERA_SPLIT_VIA_SYNC_EEPROM_SYNC_REQUESTED_VALUE_ID` (5) is the one
relation-independent request the storage lane consumes, in every relation it
is admitted for (there is one lane, not one family per relation — see
`era_authority_contract.md`). RGB (7) is the DUAL-HOST RGB policy bit: the
sender's arm gates capture and arming, the receiver's arm gates apply
(`era_authority_contract.md`); HOST-PEER's RGB response is ungated by it.

**All three default ON**, 5 and 6 since storage version 4 and 7 since version 5
(owner decision 2026-08-13). A capture or a bug report that predates version 5
was taken with RGB off unless someone set it, and there is no console field that
records which — see **USB Session** below.

**INPUT (6) is live**, and its scope is exactly the DUAL-HOST INPUT-class
runtime — the layer byte and the ACTIVITY body, both directions. Its default-on
was the first of the three to be argued: gating a correctness fix behind a
default-*off* toggle ships the defect, and a default-on bit does not. An off
sender substitutes the neutral value rather than withholding the section, so the
off state's steady wire cost is zero; HOST-PEER carries no INPUT-class section
in either direction, asserted, so the bit reaches nothing there. Gating
semantics are canonical in `era_authority_contract.md`.

**A requested id carries no route and no execution authority.** The header
states no such limit, so this line is the only statement of it.

## VIA Split Link Value IDs

`era_split_via_link.h` names value ids 8 and 9 on the same SYSTEM channel: the
level dropdown and the Apply toggle. They sit beside the sync ids above and are
**not** a fourth sync family — a sync policy is an owner preference each half may
hold differently, while the wire must run one level or no session opens
(`era_authority_contract.md`). Stored levels may differ until
Reconciliation (`split/era_split_link.h`) meets the pair at Low and raises
to the winner's stored level (DUAL-HOST Left, HOST-PEER HOST).

The dropdown's three values are the level numbering
`storage/era_eeprom_layout.h` fixes, `ERA_SPLIT_LINK_LEVEL_HIGH/MEDIUM/LOW` at
0/1/2 with High asserted at zero. It reads back the *stored* level rather than
the running one, so a Right half still walking the ring toward its talker
shows the owner what they chose.

**That is bounded to the live raise, and the bound is what keeps
the control honest.** The two used to be able to disagree for ever — the
control naming Medium while the wire ran High, with no surface saying so and no
way for the owner to learn it. Reconciliation (`split/era_split_link.h`) stores
the level the pair settled on — the winner's — once the raise is live,
and writes the dropdown's seed with it, so after a refresh the page names what
the wire runs. The dropdown still reads the *stored* level during the Low
meet, which is the target, not the boot rate. A read-only "running level" control was weighed against that and
not added: VIA re-reads only on refresh, so it would answer an owner who
already suspected something rather than tell one who did not, and after the
bound there is nothing left for it to report.

**9 is a toggle-as-action and reads back 0**, like the DFU and clean-confirm
toggles it shares the channel with. It is also the one control the
hide-what-is-inert rule cannot reach: VIA compares value ids on one page and the
running level is not one of them, so an apply whose pending level already
matches does nothing in the firmware instead of being hidden. USB re-enumerates
only after that request *commits* (`split/era_split_via_link.c`); an inert,
refused, or expired apply does not bounce, so Enable staying on is how the
owner sees that nothing ran. The MCU does not reset.

**Ids 2, 3, 4 and 9 all reach the same mechanism**, and reading them as two
unrelated features is the mistake this paragraph exists to stop. The three
clean-confirm toggles and the link Apply both raise an act on
`split/era_split_restart_agreement.[ch]`, which holds **one** pending fact: a
second act raised while one is in flight is refused, so the two controls cannot
interleave. What the owner sees when that happens is a control that did nothing,
which is the right answer for a board already about to reset for the first thing
they asked for.

## USB Session

Two USB frame-age thresholds exist, they answer different questions, and only
one of them resolves in a header.

- `ERA_SPLIT_AUTHORITY_SOF_FRESH_MS` (10) is declared in
  `era_split_authority_reducer.h`: the window inside which the authority
  reducer treats the bus as live.
- **`ERA_USB_SESSION_SOF_STALE_MS` (300) is declared in `era_usb_session.c`
  and in no header**, which is why it is named here. It is the frame-loss arm
  of the sleep decision: frames absent for that long on a port that has
  enumerated at least once since power-on mean the host is gone. The value is
  long rather than short deliberately — this arm is the backstop for the
  controller's own 3 ms suspend detector, not a race with it.

**No `WIRE_DIAG` field carries the frame sampler, the frame-loss arm, the sleep
decision or its owner.** There is no counter to read here: a sleep reading is by
eye, or from the peer session fields on `wire sess`. Unit ownership is canonical
in `era_source_map.md`, the ownership rule in `era_authority_contract.md`.

**One local USB fact the auth line does carry: `wire auth … idle=a/b`.** `a` is
the host's last `SET_IDLE` duration in 4 ms units (`usb_device_state.c`), `b`
is 1 while any IN report's idle rate is nonzero — computed the way
`usb_idle_task()` (`tmk_core/protocol/chibios/usb_report_handling.c`) computes
it, so it is the condition under which that task does periodic-resend work at
all. Whether it runs is the host's decision, not the firmware's: `idle=0/0` on a
sitting means the host asked for report-on-change only. On the ERA image the
task's walk is paced to once per millisecond (`era_qmk_fork_ledger.md`), so a
nonzero `b` is a record of what the host asked for, not a scan-rate finding. `a`
alone would not do — it is only the last request, and a host may set different
rates per interface.

**There is likewise no field for the INPUT or RGB requested policy bits**, and
this is the one place that says so, because it has been looked for. `eeprom pol
req=` is the EEPROM bit alone. `df=` is a change log, not a value. **`sync` on
`wire sess` is not the local policy view and no such field exists in any
profile** — that line prints the peer session facts and the session counters,
and nothing about local policy. A sitting that
needs to know whether the three bits are set reads them back in VIA, and the
consequence of not doing so is on record: the S1 sitting of 2026-08-13 measured
a DUAL-HOST RGB family that was never armed and had no console reading that
could have told it. From storage version 5 all three default on, so the check is
"has anyone turned one off" rather than "did anyone turn them on"
(`era_authority_contract.md`).
