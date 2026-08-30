# ERA Capture Reading

Genre: manual
Canonical for: decoding a `WIRE_DIAG` console line — every field's semantics,
line pacing and sample skew, which counters are totals against deltas against
rates, and the reading traps that decide whether a reading is valid at all

The identifiers a capture carries — mode, route, payload, status and VIA value
numbers, and every retired number that stays reserved — are
`era_identifier_map.md`'s. This document is the other half: what a printed
field means and how it may be read. Which figure a reading is compared against
is `era_performance_gates.md`'s.

**Nearly every rule below exists because a reading was got wrong once.** The
rule is kept and the incident is not: what outranks the obvious reading is the
rule, and it is not derivable from source.

## How A Capture Is Sampled

### Line pacing and sample skew

One `WIRE_DIAG` press captures a frozen snapshot per family and then emits one
line every 150 ms (`ERA_SPLIT_WIRE_DIAGNOSTICS_LINE_INTERVAL_MS`). The
scheduler, authority, eeprom and storage lines all print from that single
frozen capture, so every value in them shares one instant.

The six communication-core lines are the exception: `ccore`, `cqueue`,
`cown`, `csess`, `csp` and `crsp` re-sample the communication-core snapshot at
print time, once per line. Each is therefore taken `line index × 150 ms` after
the frozen capture. With `ERA_SPLIT_WIRE_DIAGNOSTICS_SCHEDULER_LINES = N` the
block occupies line indices `N..N+5`. The six-line count is fixed in every
profile; `N` is not.

**Take `N` from the enum and do the arithmetic. Do not read a number out of
this paragraph** — a number written here goes stale the next time a line is
appended, which is why the rule is a derivation. Every new scheduler line is
appended *last* precisely so that every other line keeps its index: the only
thing an append moves is this offset. The cheaper check is nearly free — count
the `wire ` lines before `ccore` in the capture you hold; the current scheduler
printer emits one line per paced slot.

Read rule: a communication-core counter and a scheduler or storage counter from
the same press are not simultaneous. Movement visible only on a
communication-core line may be work that happened during the print itself.
**Never take a delta across the two classes.**

### Cumulative counters are totals, not rates

`csp sub`, `crsp pub`, `csess sub`, `sess`, `hkwork`, `owner` and `txrx` are
free-running from boot. A rate is therefore *always* the difference between two
captures divided by the elapsed time between them, and never a single capture's
counter divided by its uptime — that form averages every relation the pair has
held since power-on, so a defect that started late in the boot is diluted by
everything before it.

**The elapsed time is `last` on the `wire pc` line, in milliseconds, and it is
NOT `raw_us`.** `raw_us` is `raw_matrix_read_us_total`: accumulated *raw matrix
read* time, which is only the front part of each scan — roughly a third of the
wall clock on this board. A rate divided by `raw_us` is inflated by whatever
fraction of the scan the raw read happens to be, and that fraction is not a
constant. **Divide by the `last` delta.** The check that the base is right is
free and should be run every time: `raw` delta divided by the elapsed seconds
must reproduce `scan_hz`.

`raw_us` remains usable as the reboot detector, because it is still monotonic
from boot: a capture whose `raw_us` is *smaller* than the previous one crossed a
reboot and no delta spans the two. `core_free` identifies which image produced
the line — it is the manifest's `ram0_free_bytes` minus a small alignment
constant that is a property of the image generation and **not a fixed number**
(recorded pairs have shown 8 and 4). **Record the pair per image set**; a value
matching no manifest is a build nobody can name.

### Fields that do not answer the question asked of them

Each of these was asked for by a written leg or by a review before anyone
checked what the console prints.

- **A dump taken with `storage active=1`, or with `open` ahead of `close`, is
  not an end-of-operation dump, and its `fall` belongs to the *previous* span.**
  `spans` has already risen for the operation in flight, so subtracting gives a
  span that is silently wrong and can come out negative. Take that operation's
  span from the other half, or dump again after close.
- **The HOST-PEER exchange rate belongs to the initiator, and the initiator is
  the PEER, which has no console.** Read it on the HOST as the increase rate of
  `crsp prep` (or `quiet` + `resv`). A step that asks for "the exchange rate" in
  HOST-PEER without saying this cannot be run.
- **`eeprom pol req=` reports the EEPROM bit only** — no INPUT field and no RGB
  field. A step that "verifies the policy bits" from it verifies one of three;
  read the other two from VIA's pane and confirm them behaviourally.
- **A per-half policy bit cannot be set or read on a half while it is the
  PEER.** Any arm needing a specific bit on both halves must be staged from
  DUAL-HOST, where both are enumerated, before the rig changes.
- **`wire hp peer_tavg` has no maximum field** — `n`, timeouts, means and the
  last sample only.
- **Ask whether the reading is a counter or an eye.** The visual baseline and
  the lock indicator have no counter on either end, so a step that observes them
  says "look at the LEDs" and takes no capture — a capture is itself a wire
  interruption.

**Name a rig by its relation — HOST-PEER or DUAL-HOST — and never by a
shorthand.** A symbol is readable only where something defines it, and no
document in this set is written to define notation.

### The capture is taken by pressing a key

**`WIRE_DIAG` is a keycode, and the shipped TOMAK79H keymaps bind none of the
three.** `WIRE_DIAG`, `WIRE_DIAG_2` and `WIRE_QWIN` are declared in
`keyboards/era/sirind/common/tomak_common.h` and reachable, but the `default`
and `via` keymaps carry ordinary keys in those positions, because a shipping
keyboard may not spend `KC_F6`/`KC_F7` on an instrument that does nothing in a
release build. **A measuring session binds one temporarily and restores the
keymap afterwards**; that edit is part of taking a capture, not a change to the
firmware.

A leg whose subject is "what happens while the half is idle" then has to be
designed around the press itself, and the latch order is what rescues it: the
snapshot is taken inside `process_record_kb`, i.e. inside `keyboard_task()`
(`quantum/keyboard.c`), which the main loop runs *before* `housekeeping_task()`.
So the latched figures cannot contain any effect of their own press that needs a
housekeeping pass, a core1 exchange or a wire round trip — **the first press
after an enforced silence is a valid pre-trigger reading of that silence.**

**What the press does not escape is the storage lane: the capture keycode is
itself a settled-capture trigger, so a storage episode opens behind every
capture.** The episode lands after the snapshot, so it is counted in the *next*
capture — `open` moves in every bracketed delta even when nothing but the two
presses happened. For any arm whose premise is "no key was touched", `xfer` is
the discriminator, not `open`.

### Role-era lines are deltas, not totals

`hp peer_era`, `hp host_era` and `dh era` are not cumulative counters, and
reading them as if they were manufactures a phantom defect. Three properties,
none obvious from the lines themselves:

- **They reset on every capture.** `era_split_wire_diagnostics.c` calls
  `era_split_transport_scheduler_reset_diagnostics_era_baselines()` at the end
  of the snapshot capture, immediately before the print begins. Each block is
  therefore *what happened since you last looked*, already differenced.
  Differencing two captures' `_era` lines subtracts twice and is wrong.
- **They are role-scoped.** The block accumulates only while the half is in the
  mode that owns it, so a half that was PEER and is now HOST shows the HOST-era
  traffic alone. `sess`, `txrx` and `csess` are the cumulative view and do mix
  roles.
- **They can fail to measure, and `meas=` is where they say so.** Each era block
  subtracts two reads of the published transaction-engine mirror, and that
  reader falls back to its last proven snapshot when it cannot prove a stable
  publication. `meas=1` means both boundary reads were proven.

  **`meas=0` shows as an exact zero, never as an undercount**, which is why the
  field exists rather than a tolerance: every caller of the mirror is an era
  boundary or a print, so a collision at the capture returns precisely the bytes
  the activate stored and the subtraction is identically zero. Without the field
  that is indistinguishable from the healthy quiet reading.

  **The initiator is the exposed half by construction, and under the constant
  DUAL-HOST poll `meas=0` there is ordinary rather than notable**: its core1
  republishes the mirror on every transaction, so it collides in proportion to
  how often it transacts, while the responder's publisher is comparatively idle.
  `bem`'s fallback count is the corroborating witness on the same capture. Read
  the initiator's `io` as absent and take the wire count from the **responder's**
  `dh era io`, which must agree; `csess` and `crsp prep` corroborate it from two
  more directions. **Judge every poll-driven counter against the current poll
  periods**, which `era_route_contract.md` is canonical for: the rate a counter
  should show is that period's reciprocal, and no document carries a second
  table of it to drift against.

  `stor` and `rt` are unaffected on both halves, which is what keeps the silence
  legs readable — they are read from their own state and never through the
  mirror.

`mode=` on these lines is `last_mode`: the mode of the last era this block
recorded, not necessarily the current one. When the relation is a mode that owns
no block, the line reads with the previous label and all counters zero — every
field individually true, and the line as a whole easy to misread as "this
relation, no traffic" when it means "not this relation at all". **Read `rel=` on
the `wire pc` line for the current mode**; that is the authority. DUAL-HOST owns
an era block; `LOCAL_NO_LINK` (0) owns none.

### The live CLEAN phase line

`wire clean ev=… t=… i=… l=… lp=… pp=… pa=… ap=… dl=… cq=… hs=…` is the
diagnostic-build-only transition signature from
`split/era_split_restart_agreement.c`. Unlike a `WIRE_DIAG` snapshot, it prints
at the transition so the controlled reset cannot erase the evidence first.
`t` is the local timer, `i` is local-initiator, `l` is local-Left, `lp` is this
half's reboot-durable PREPARED fact, `pp` is an exact peer AUTHORITY/PREPARED match,
`pa` is the cached peer phase, `ap` is the current RESTART_ARM phase (`0` idle,
`1` PREPARE, `2` COMMIT), and `dl` is the candidate or adopted shared deadline.
`cq=claim/tx/rx/publish/fail/request-claim/result-claim/result-ready` is the
dedicated Core1 initiator-lane signature; `hs=xfer/apply/complete/abort/timeout/active`
is the storage-runtime signature. These are boot-cumulative values and need not
start at zero. On each half, all eight `cq` values and all six `hs` values must
remain unchanged from its event 4 through its last observable pre-reset event;
the three generation fields and `active` must already be zero at event 4.

The event ids are `1` REQUEST advertised, `2` initiator selection/PREPARE,
`3` responder PREPARE receipt, `4` reboot-durable local PREPARED, `5` prepare
failure, `6` initiator COMMIT publication, `7` responder COMMIT adoption,
`8` initiator COMMIT_ARMED echo adoption, `9` unanswered-COMMIT disarm, and
`10` reset commit. Event 10 may remain buffered at reset and is corroboration,
not a required witness; events 4/6/7/8 occur before the deadline and carry the
ordering proof. A healthy serviced CLEAN has event 4 on both halves before any
event 6; the initiator then sees 6 followed by 8, while the responder sees 7.
Event 5 is a terminal failure for that boot: no event 6–10 may follow, and
storage remains quarantined.

## The Relation's Own Counters

`wire dh era valid=%u mode=%u io=tx/rx/miss/bad/fail stor=open/close/xfer
rt=tx/rx meas=%u`. `mode=` distinguishes `DUAL_HOST_LEFT` (3) from
`DUAL_HOST_RIGHT` (4); both share one block, because a half holds exactly one of
them. `io` is the era's compact wire traffic; `stor` is the era's storage
episode counts, which must not move at all across a window with no settled
config change; and **`rt` is the runtime section counts** — `tx` is sections
this half sent, `rx` is sections it accepted from the wire. `rx` counts the
arrival, **before any policy gate**: a body the receiver refuses to apply still
counts, which is what makes "sent but not applied" readable beside the apply
counters (`app=rgb`, `aap`).

**There is no periodic `SESSION_STATUS` in a serviced relation**, in any storage
configuration: nothing timer-driven emits one. **That is a fact about the design
and not a prediction that `csess` sits still.**

**A standing exchange that stops costs one session revalidation, by design.**
Any non-OK transaction result stops the exchange
(`communication_core/era_split_communication_core_standing.c`), core0 raises the
local status pending bit the first time it observes that stop, and the
observation re-arms when the exchange resumes
(`era_split_transport_scheduler.c`). So `csess sub` counts stop edges, one
apiece — and on a DUAL-HOST pair polling every millisecond, a failure fraction
of a few thousandths of a percent still reads as a revalidation every half-minute
of typing. That is why it rises with traffic and sits still when idle. **Read it
beside the initiator's transaction-failure counters** — PIO error, send and
response timeout, partial frame, decode, response contract: equal deltas are the
designed behaviour, and a `csess` that outruns them is a finding.

**`sfg` on `wire sess` is a teardown count, and it is not `csess sub`'s
neighbour.** The two sit one line apart and answer opposite questions. A `csess
sub` step is the standing exchange stopping and the relation being re-blessed
with the peer record intact. An `sfg` step is that record being thrown away:
`era_split_scheduler_session_forget_peer_from_scheduler()`
(`split/era_split_scheduler_session.c`) zeroes the whole peer block and clears
`peer_known`, so `pk` falls with it and the relation has to re-form through
discovery. It has exactly one call site — the mode planner's
`peer_session_forget_required`, applied in
`split/era_split_transport_scheduler.c` — and the planner raises it from
`secondary_stale` alone. **A step with a cable event or a deliberate promotion
beside it is the mechanism; a step with nothing beside it is the first suspect
for a dead core1**, and this counter is where that shows.

**`io` is not a silence instrument in DUAL-HOST**, and this is the reading rule
most likely to manufacture a phantom defect on a fresh capture. The runtime lane
polls unconditionally once the relation is confirmed, so `io` rises in *every*
DUAL-HOST window, idle included, at the poll cadence and at the poll cadence
alone. **A rising `io` on an idle pair is the mode working.** What carries the
silence questions is `stor` and `rt`. The `meas=1` rule applies to `io` alone.

**`rt` counts runtime *sections*, never polls.** A section is advertised only
while its value differs from what the wire last confirmed, so an idle window and
a typing window with no layer transition both read `rt=0/0` while the poll runs.
**That reading is net of the anchor**: a time-valued section always differs at
its 60 s refresh, so a settled window longer than 60 s reads one `rx` per refresh
on each half — `sec=` carrying `0x80` and `anc` applies moving with it — while
`tx` stays 0, because the responder send path counts no anchor. Any other `rt`
movement in those windows is the failure the silence legs exist to catch. **A
frame carrying two sections counts two**, so a relation open leaves a small
nonzero `rt` behind before the window settles and **the silence legs are read
from a *settled* window, not a whole capture**.

**The two halves' `tx` and `rx` do not count the same section set, so a
cross-half subtraction is not a loss reading.** The responder's `tx` counts only
INPUT_LAYER, AUTHORITY, RGB_STATE and ACTIVITY
(`scheduler/era_split_transport_scheduler_responder.c`); the anchor, the news
byte and the visual baseline cross rx-only, so the initiator's `rx` runs ahead
of the responder's `tx` by exactly those three classes. The lock byte is not
among them and does not cross this lane at all —
`ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_RSP`
(`split/era_split_wire_protocol.h`) omits it, and the HOST-PEER response set
that carries it has no `rt=` line to read it on. The other
direction counts one set: the initiator's `tx` and the responder's `rx` both take
INPUT_LAYER, STORAGE_PENDING, AUTHORITY, RGB_STATE, ACTIVITY and VISUAL, and
those two deltas match exactly. There is deliberately no defer field: nothing
here can defer a runtime section, and a counter that is structurally zero reads
as evidence when it is not. The block carries no transaction timing — the
per-bucket timing on `hp peer_era` is a HOST-PEER instrument.

## Scheduler And Storage Lines

- `wire storage`: five explicit 150 ms-paced HOST-PEER replacement storage
  diagnostic lines; emitted only by `WIRE_DIAG`, never automatically.
- `st`: the storage runtime state, printed as the raw enumerator index of
  `era_host_peer_storage_runtime_state_t`. That enum is private to
  `era_host_peer_storage.c`, so it resolves in no header and is named here.
  Read it from the enum's own declaration order, which the comments there pin
  deliberately. The two a healthy responder idles at are both benign and are
  **not interchangeable evidence**: `9` (`HOST_CLOSED`) follows a proof/match
  close, `7` (`HOST_READY`) is where a push apply leaves it. `0` is `IDLE`, and
  which half reads it is a role fact: **an initiator idles at `0` between
  episodes** with `dom=255` (`split/era_host_peer_storage.c`) however much work
  ran, and `open`/`close` are what moved. **A responder never reads `0` in a
  confirmed relation**: a responder capture reading `0` while the relation is
  confirmed is the storage lane not running, not the expected quiet.
- `open/close/abort/restart`: storage episode lifecycle counts. `restart` is
  Core1 restart around a durable apply, not a failure. The cause timeline in
  `split/era_host_peer_storage.c` records the pull applier's `CORE1_RESTART`
  after `EEPROM_END` and before its public flip; a push responder declares
  durable, delivers `COMPLETE`, and records the later restart only then. Judge
  it against `apply`, and read `qfail` and `abort` for whether the restarts
  succeeded.
- **`close + abort == open` is an initiator invariant and does not hold on the
  responder.** A responder opens no episode for a summary-family exchange but
  does count the `ABORT_REQ` that aborts one, so a contended window reads more
  aborts than opens. Check it on the initiator; on the responder read `abort`
  against the initiator's summary count.
- `proof/match/xfer`: storage proof results. **`proof` counts `PROOF_RSP`, not
  relation-open sweeps**, and comparing it across halves or directions without
  this rule manufactures a phantom loss. In `split/era_host_peer_storage.c`, a
  pull-domain initiator counts the unpinned `BUSY` and the later proof, while
  its responder counts only the proof it answered with data: one seven-domain
  all-pull sweep therefore reads `proof=14 match=7 retry=7` on the initiator
  and `proof=7 match=7 retry=0` on the responder. A push-domain opens through
  `PUSH_CTL`/`PUSH_RSP` instead, so an all-push sweep reads `proof=0 match=7`
  on both halves and `retry=7` only on the initiator. A mixed sweep is the sum
  of those per-domain shapes. `BUSY` is not a failure — Core1 may not read
  EEPROM, so it asks core0 to capture and pin the domain. The invariant is one
  initiator retry per unpinned domain; `core cl/tx/rx/pub` proves that handoff
  stays one-shot.
- `chunk/dup/retry/timeout`: chunk and retry progress.
- `apply/complete`: successful durable ERA NVM replacement/publication and
  pair-visible completion counts. The full candidate is validated and all
  fallible preconditions are rechecked before ADMIT; a failed NVM replacement
  leaves `apply` and `complete` unchanged. After NVM success there is no
  rollback state: relation/publication recovery proceeds forward from canonical
  NVM, so an `apply` increment without a later `complete` names post-durable
  relation/publication work rather than a partially persisted candidate.
- `stale/full=i/r`: generation reject and distinct PEER initiator / HOST
  responder dedicated result-capacity failure. Since the complete-poll
  reservation carve-out (`era_host_peer_storage_contract.md`, Capacity And
  Publication) the responder arm no longer moves on a healthy push apply — the
  provisional poll answer reserves nothing — so `full`'s responder arm moving
  there names a real capacity fault. The storage line's `status=` freezes at the
  apply trigger for the width of a push apply for the same reason.
- `source`: saturated source-supersession count. It advances for terminal
  `SOURCE_CHANGED` proof/apply/abort responses and a local target-dirty abort;
  it is separate from wire `timeout`, `integrity` and relation-stale failures.
- `integrity/version/domain`: exact image/schema/domain rejects.
- `qfail`: Core1 quiesce/restart failure preventing an identity rotation or
  bounded pending-timeout recovery. It is not persistence rollback authority.
  If it occurs after `apply` advanced, the NVM content is already canonical and
  the relation must repair/re-prove from it.
- `news/probe`: this half's storage news value and the current initiator pending
  probe mask, read from cold core0 state. `news=` here is one value; `news=` on
  `csp` is a different field on a different line, a valid/value pair, and neither
  is derived from the other. `newsn` on `csp` **must read zero** — the section
  rides the standing answer only.
- **`news` is a forward-only counter, not a mask** — hex only because the field's
  width never changed. It steps once per settled capture on this half, wraps
  `1..7F`, and `00` means "nothing to claim". A value that does not come down is
  the design; it has no retirement, no boot arming and no policy re-arm, and what
  a reader wants from it is only whether it differs from the peer's `pnews`.
- **`news` and `chg` answer different questions.** `chg` is the persisted
  per-domain "does my content differ from the last agreement"; `news` is "how
  many settled captures have I made". An edit reverted to the agreed content
  still produces a settled capture, so `news` steps while `chg` reads `00` — the
  capture is news even though the content is not, and the summary that news arms
  closes it `MATCH`.
- `sec=` on `wire txrx` and `wire hp peer_txrx` is the transaction-timing
  record's `response_section_byte`
  (`split/diagnostics/era_split_wire_diagnostics.c`): **the last timed
  transaction's response section byte, whatever that transaction was.** It is a section mask for a sectioned
  response and the flags byte for a `SESSION_STATUS` one, and the line itself
  says which — only read it as flags when the same line shows `rsp=2/9`.
  Anything else makes `sec=00` mean "not a flags byte", not "no flags set". It
  prints the whole byte, so a single flag is a bit inside it and never a printed
  value; using `sec=` as an image-identity check has cost a gate run.

  **In a `SESSION_STATUS` flags byte, `0x04`, `0x08` and `0x20` are
  reserved-zero and a frame carrying any of them is refused outright.** A set
  bit is not a hint being ignored, it is a frame being thrown away, and the
  failure counters rather than this field are where it shows. The rule is this
  frame's alone: the AUTHORITY section's flags byte, which carries the same
  session facts, has had no reserved bit since the agreed restart took its last
  one on 2026-08-19, and its validator refuses values it has no fact for
  instead (`era_wire_contract.md`).

  **As a section mask it is a pure mask**: no value is packed into it. A
  lock-only response prints exactly `08`, the lock value being a body byte.

  **Bit `0x04` of a section mask is AUTHORITY**, in both directions, and bit
  `0x02` of a response section mask is the **ACTIVITY body** (in the push
  direction `0x10` is the same body). So `sec=04` is authority-only, `sec=05` is
  authority plus the DUAL-HOST layer byte, `sec=06` is authority plus activity,
  and `sec=03` is the layer byte plus activity. Section bytes and body layouts
  are canonical in `era_wire_contract.md`.
- `df` on `eeprom pol`: the sync-policy change log, in the policy FIELD bit space
  (`0x01` EEPROM, `0x02` INPUT, `0x04` RGB) — which requested-policy fields have
  changed since this half last loaded the policy block from EEPROM. The save
  happens synchronously on the same call that sets the bit, nothing consumes the
  field but this print, and the only clear is the EEPROM reload path — so a
  nonzero `df` holding for minutes is a converged pair's designed reading, not
  pending work. **Its `0x04` and a storage domain mask's `0x04` name different
  things by coincidence**: a policy field here, the dynamic-macro domain in
  `chg`, `probe`, `psh` and `cfl`.
- `pnews` on `wire sess`: the peer's storage news value, as the relation's lane
  last delivered it to this half. It reads on an initiator half in either
  relation, which is the only half that decodes a peer advertisement; a DUAL-HOST
  Right reads `00` structurally. The wire byte also carries the responder's
  pending flag in bit7, but `pnews` records the value bits only — the observer
  masks at capture, and the flag's console truth is the shim's `mir`.

  **Read it only against the peer's `news`, and only for equality.** On a settled
  pair the two match; a persistent difference is the finding, and it means one
  side's delivery is not arriving rather than that a particular domain is stuck.
  The value carries no domain identity.
- `recency ok/chg/cnt/arb/psh/cfl/prv/cfm`: the persisted recency layer,
  arbitration view and display provenance, read cold at print time from EEPROM
  plus the cached state.
  `ok` is baseline-record guard validity, `chg` is the per-domain changed mask
  (current manifest CRC differs from its persisted baseline; all-ones while
  `ok=0`, the conservative degradation), `cnt` is the seven divergence counters
  in domain id order, `arb` is the arbitration flags and the peer's last declared
  changed mask, and `psh`/`cfl` are the push and conflict cell queues. `prv` is
  `local-changed/relation-cell`: bits that still originate only in missing
  baseline knowledge and are therefore excluded from the indicator, never from
  arbitration. `cfm=1` means an actual `TRANSFER` or retry fault promoted the
  initiator round, so its remaining provisional gaps are visible. Rules are
  canonical in `era_host_peer_storage_contract.md`.
- `arb` flag bits, and **only `0x01`, `0x02` and `0x04` have a writer**: `0x01`
  summary-done, `0x02` summary-pending, `0x04` the **round** is the
  relation-open verify-all one rather than an in-session refresh — round-scoped,
  so it survives a summary being consumed and retires only when the round drains,
  which is what lets an episode abort mid-sweep and still be proven. Nothing sets
  any other bit, so `arb`'s first field can only read `0..7`.
- **`0x08`, `0x10` and `0x20` are retired and stay reserved un-reused.** Their
  writers — the hint's edge latch, the local policy-open edge detector, and the
  round-end re-read's stop condition — all retired with the carrier they
  compensated for. The arbitration enum in `era_host_peer_storage.c` carries all
  three as comments rather than names, so a name there reads as a live flag and
  only a comment reads as history.
- **`arb`'s first field prints decimal, and its second prints hex.** The format
  is `arb=%u/%02X`, so `arb=16/00` is flag `0x10` alone and `arb=21` is `0x15`,
  not `0x21`. This trap has been read wrong on a real capture.
- **`arb` is initiator state, and off-role it is history rather than state.**
  `0x01`, `0x02` and `0x04` are written only while this half is the relation's
  initiator, so on a half acting as responder they record the last relation *this
  half initiated*. **No `arb` bit is a local policy view, and no console field
  is** — the three requested bits are read back in VIA (**USB Session** in
  `era_identifier_map.md`, which is the one statement of that).
- **Two of them are released off-role rather than left as history**: `0x02`
  summary-pending and `0x04` verify-all. They describe a round this half still
  owes, and a half that cannot run one must not report one pending, so a
  responder clears both — along with `probe`, `psh`, `cfl`, the deferred slot and
  `due=`. The record bit `0x01` stays. **A responder reading `0x02` or `0x04` is
  a finding**, and a lit `due=` on a responder is not expected at all.
- **`probe`, `psh` and `cfl` are drained, not accumulated.** A domain's bit
  leaves all three queues the moment its episode starts, so a capture taken after
  the drain reads `00` however much work ran. They answer "what is still queued",
  never "what happened". Reading `cfl=00` as "no conflict exchange ran" is wrong
  and has produced a wrong call in this lane.
- **`probe=` on `wire storage` reads `00` on the HOST role** — the initiator
  queues are released every pass. It is not evidence of an absent probe.
- **The relation-open signature is role- and relation-scoped, and no document
  records an expected table for it.** What is written down is the trap: it has
  been mis-scoped once by quoting a DUAL-HOST figure as the HOST-PEER one, so a
  signature is comparable only against one taken in the same role and the same
  relation.

### The EEPROM SYNC indicator shim

- `eeprom shim ind vis/pnd/mir/gate/hold/spans/rise/fall`: the indicator's whole
  read. `vis` is the lamp as rendered (pending held to the 160 ms rise-anchored
  floor). `pnd`/`mir`/`gate` are the local pending arm, the peer's advertised
  mirror, and the cached serviceability-and-policy gate. `hold=1` means this
  half's local arm has fallen but its own zero has not yet been confirmed on its
  active carrier — `STORAGE_PENDING` for an initiator, `STORAGE_NEWS` bit7 for a
  responder — so the panel intentionally remains pending rather than getting
  ahead of the peer's mirror apply.
  `pnd` is already the display-filtered local fact: raw
  `chg`/cell bits covered by `prv` do not raise it until `cfm=1` or a transfer
  retires the active domain's provisional bit. `spans` counts rising edges of
  the pending fact; `rise`/`fall`
  are local-uptime ms of the latest span's first and last pending pass.

  **A dynamic-macro upload is visible before it is durable.** Its nonzero
  marker opens ERA NVM's staging transaction, and that transaction mode is an
  explicit display-only `pnd` arm from the first macro write. It does not move
  `chg` or the MACRO State Sync revision; those still move only at the successful
  durable zero close. Thus a capture with `pnd=1`, an open macro transaction and
  unchanged semantic revision is not premature publication — it is the
  indicator doing its job. `wire via macro` is the request-side witness for that
  staging interval.

  There is no bridge arithmetic to check: the two halves' `rise` and `fall`
  stamps compare directly within the known local-clock offset, and a responder
  outliving the initiator by more than about one poll period plus a housekeeping
  pass is a finding against the mirror path. `mir` is live on both halves — the
  responder's arm crosses as `STORAGE_NEWS` bit7. The predicate is
  `era_host_peer_storage_indicator_pending()`; the retired
  `active/on/flush/lw/chg` presenter fields, the `fst` state byte and the `off`
  bridge-expiry stamp date a pre-redesign capture.

  **The line is domain-blind by design** — the per-domain
  `st/dom/dirty/ddom/ready/chg/lw` shim line it replaced was written by one
  ERA_CONFIG-only caller and read dead for every other domain. `pnd` is the
  engine's whole local arm — any domain's dirty, changed, cell or episode term —
  so a VIA lighting edit and a layout load read on the same bit. Per-domain facts
  stay where they always were: `chg` on `wire storage recency`, transfers on
  `xfer`.
- `eeprom shim led rn/ron/roff`: the LED truth the predicate stamps cannot see.
  The board's render-policy flush hook reports every flushed frame's STATUS bit,
  and the edges stamp red-era count, the first STATUS flush of an era, and the
  first normal flush after it — the moment the red actually left the panel.
  **This is panel truth, not producer truth**: the core calls the hook on every
  PWM push, zero-flag frames included, and a non-STATUS push during a held red
  era both closes the era (`roff` stamps the black instant) and drops the board's
  held-frame proof, so the field re-renders within one frame and `rn` counts the
  re-light.

  A TOMAK STATUS-policy edge also explicitly wakes the RGB Matrix render-policy
  state machine. It does not wait for a later animation epoch: an idle state is
  restarted on the next RGB task pass, while an already-buffered frame is
  flushed first and the pending policy refresh starts immediately afterward.
  Background ERA-NVM bank maintenance yields from that request until the
  refreshed policy reaches the PWM flush boundary, so a multi-pass
  STARTING/RENDERING/FLUSHING transition cannot have a 4-KiB erase inserted
  between each pass.
  Therefore a large `ron`/`roff` lag behind the corresponding `rise`/`fall`,
  after correcting the halves' local-clock offset, is a render-path finding and
  not an accepted scheduler delay.

  Read `rn` against `spans` per operation: `rn == spans` is the clean reading;
  `rn > spans` with visuals clean means a mid-era repaint was caught and healed —
  a finding to attribute (bracket it with `roff`/`ron`), not a failure; a visual
  gap with `rn == spans` leaves only a black STATUS frame.
- `brk=count/flags/state brkms=`: the breaker latch — the identity of the last
  frame that broke a red era **while the lamp was still commanded visible**. The
  span's own end always breaks with the lamp already dark, so `brk` fires exactly
  on the healed-trigger frames the `rn`-vs-`spans` excess counts, and `count`
  must equal that excess. `flags` is the frame's raw render flags (`00` is the
  zero-flag NONE/suspend fill); `state` packs the panel facts at that instant —
  bit0 RGB enabled, bit1 suspended, bit2 the board's arbitrated status policy on.
  The discrimination: `flags=00` with enable set and suspend clear is the effect
  latch reaching NONE while enabled; enable clear names a config-write path;
  suspend set names the sleep predicate; nonzero `flags` without STATUS while
  bit2 is set means the policy's flag was stripped between arbitration and flush,
  and with bit2 clear the arbitration itself dropped mid-era. `brkms` is the same
  instant `roff` stamps for that break.
- `slp=count@ms`: rising edges of the resolved lighting-sleep decision since
  boot, with the last rise's local ms — the one mid-operation writer that can
  raise RGB suspend. Read it against `brk`: a `slp` rise at `brkms` convicts the
  sleep predicate's frame-age arm for that breaker. The field is cumulative,
  so boot or an earlier link Apply may leave an idle pair at a nonzero count;
  `delta slp=0` across a breaker-showing operation excludes the sleep path and
  leaves the config/effect writers.

**The lamp is one per operator action, not one per transfer, and it cannot be
counted to infer transfers.** The pending fact holds across settle, per-domain
grants, inter-episode gaps and the durable apply's identity rotation, so a
layout load that moves several domains shows one span rather than one per gap.
A CLEAN/fresh first audit whose seven domains all close `MATCH` shows no span;
if it decides any transfer, `cfm` rises and the actual operation shows one span
through the remaining drain. Read transfers from `xfer`/`complete` on `wire
storage`.

**The lamp follows unfinished pair work, not route exclusivity and not the
transfer alone.** An abort takes the wire for its `ABORT_REQ` and moves nothing;
a terminal refusal closes dark; a *retrying* failure keeps the lamp lit through
its backoff because re-armed work is work. Lit with `pnd=1` while `xfer`, the
cells and `chg` are all frozen across two captures is a finding against the
changed shadow, not a slow transfer.

**`excl=` reads 0 through an apply phase**, and that is the exclusivity span
ending at transfer-verified rather than a broken flag: it covers the chunk stream
(and an abort's wire priority) and clears at each role's transfer-verified
boundary, so a capture taken mid-apply reads `excl=0` with `st=` in an apply
state. To read traffic during an apply, bracket the ERA NVM call with the cause
timeline's `EEPROM_BEGIN`/`EEPROM_END` (`cause` variant), and read Core1/runtime
(`rt`/`io`) and section counters before and after it. Core0 does not return to
the keyboard loop inside that bracket, so no scan or core0-route movement should
be inferred from a missing intermediate sample. The actual NVM width is a device
measurement in `era_performance_gates.md`, not a derived sub-window.

### The storage core and its cause timeline

- `core cl/tx/rx/pub/fail`: wire-diagnostics-only Core1 storage initiator
  request-claim, wire-TX, response-RX, result-publication and failed-stage
  counts, retained to validate the corrected one-shot BUSY handoff.
- `core gen=req/result/ready`: current storage request claim, result claim and
  result-ready generations; `last=stage/result/failure/op/status` identifies the
  last Core1 initiator boundary without reading live EEPROM/QMK state. Probe
  stages are `NONE=0`, `CLAIM=1`, `BEGIN=2`, `ENCODE=3`, `TX=4`, `RX=5`,
  `CONTRACT=6`, `PUBLISH=7`.
- `flast=stage/result/failure/op/status/class/access` is the most recent failed
  boundary and, unlike `last`, survives every later success. `result` is
  `NONE/OK/MISS/BAD/FAIL=0..4`; `failure` is
  `NONE/QUEUE_EXPIRED/OWNER/EPOCH/CANCEL/RESET/PIO/SEND_TIMEOUT/RESPONSE_TIMEOUT/PARTIAL/IO/DECODE/RESPONSE_CONTRACT=0..12`.
  Request classification is
  `INVALID/READY/RESULT_FULL/QUEUE_EXPIRED/OWNER_STALE/RELATION_STALE/GENERATION_STALE/POLICY_STALE/TRANSACTION_STALE/CANCELLED/RESET=0..10`,
  backend access is `OK/OWNER/EPOCH/CANCEL/RESET=0..4`, and `255` means that
  axis did not participate in the failure. `ddu` is signed microseconds from
  the request's not-after deadline at failure detection: positive is late;
  `-2147483648` means a publish failure had no request deadline available. A
  BEGIN failure with `class=3`, `failure=1` and positive `ddu` is therefore a
  proved storage queue expiry rather than an inferred wire failure.
- `qctx=delay/window` retains actual cross-core publish-to-failure-detection
  residence and the signed publish-to-not-after window, both in microseconds.
  For a BEGIN queue expiry, `delay - window == ddu`. Both timestamps now share
  the actual storage publication boundary. With the default 20 ms standing
  response window, a settled High / Medium / Low link reads a
  25000 / 45000 / 85000 us window: the configured window at the current scale
  plus the 5 ms compact-TX/handoff margin.
  `4294967295/-2147483648` means that failure had no
  published request context.
- `psvc=valid/kind/reason/result` and `span=start/end/gap` retain the most
  recent completed timed route at the failure. `start` and `end` are signed
  route-start-minus-publish and route-end-minus-publish; `gap` is
  failure-detection-minus-route-end, in microseconds. Thus `valid=1`, positive
  `end`, and `start < qctx.delay` prove that route occupied part of the queued
  request's residence. Negative `start` means it was already in flight when
  storage published; positive `start` means it began after publication but
  before Core1 observed the dedicated storage slot. A small nonnegative `gap`
  places BEGIN immediately after it. Standing is `kind=1` with runtime reason
  `3` (section push) or `4` (response poll); result uses the same `1..4`
  mapping as `flast`. Zero `valid` and `-2147483648` spans mean there was no
  completed timing sample to correlate. The retained window is read from
  `split/era_split_transaction_engine.c`; no service-loop trace is added.
- `fid=owner/relation/transaction/request/domain/detail` identifies the failed
  storage publication. For `PUSH_CTL_REQ` (`op=EE`), detail is
  `OPEN/APPLY/COMPLETE/ABORT=0/1/2/3`. Local cross-core request generation may
  change on retry while the wire-visible transaction/domain/op identity stays
  the same.
- `wire storage cause`: selector-only causal timeline emitted when
  `ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE=yes`; absent from normal release,
  qwin and wire-diagnostics builds. `role=1/2` means PEER/HOST, `dom/gen`
  identify the transfer transaction, `n/ov` are retained event count and
  overflow, and `stale=age/limit` captures the first responder-stale decision.
  The timeline starts when a `TRANSFER` is accepted, in either lane and on both
  roles. The apply bracket lands on whichever half applies (the pull initiator,
  the push responder); the push initiator's timeline is begin-sparse by design.

  Each `ev=XX@ms` byte uses the high nibble as the ordered event id:
  `1=CHUNK_RESULT`, `2=APPLY_READY`, `3=APPLY_BEGIN`, `4=EEPROM_BEGIN`,
  `5=EEPROM_END`, `6=CORE1_RESTART`, `7=SESSION_SUBMIT`, `8=SESSION_RESULT`,
  `9=SESSION_RX`, `A=RESPONDER_STALE`, `B=SESSION_FORGET`, `C=REVALIDATED`,
  `D=COMPLETE_SUBMIT`, `E=COMPLETE_RESULT`, `F=ABORT`. The low nibble is event
  detail: APPLY_READY, COMPLETE and ABORT record the storage status id; restart
  success is `1`; session submit records prior peer-known; session result uses
  transaction result `1..4` or `5` for OK-without-valid decode; and session RX
  uses bit 3 for response-sent and bits `0..2` for result.

  `EEPROM_BEGIN..EEPROM_END` brackets the one synchronous
  `era_nvm_replace(... REMOTE_APPLY)` call. `EEPROM_END`'s detail is the NVM
  result code. Runtime reload and immutable/State-Sync publication follow a
  successful end. There is no rollback write outside the pair: once NVM
  succeeds it is authority, while an NVM failure leaves the public range old.
  The old-or-new claim remains a source/host-test gate, not a console inference.
- **`CHUNK_RESULT` is sampled and its detail is the sixteenth, not the chunk
  id.** One chunk in sixteen is recorded, so `1X` reads as chunk `X × 16`. The
  sampling exists because an unsampled stream fills the 32-entry ring before the
  apply, the rotation or the abort — the events the timeline exists for — and the
  timestamps are 16-bit milliseconds for the same reason: a byte saturates at
  `254 ms` against an episode whose bulk stream alone runs several hundred.
- `wire storage edge`: `cause`-only, interval-scoped indicator and dynamic-macro
  recorder. A `WIRE_DIAG` snapshot copies this line and immediately resets its
  interval in `split/diagnostics/era_split_wire_diagnostics.c`. In DUAL-HOST,
  where both consoles already exist, take one idle baseline snapshot on each
  half, perform exactly one macro write without an intermediate diagnostic
  press, wait for the lamps to fall, then take one result snapshot on each
  half.

  **A HOST-PEER leg needs the promotion route.** Before the leg, attach USB to
  the PEER temporarily, promote it, and take its baseline snapshot; detach that
  USB, wait until the intended HOST-PEER relation is restored and both lamps
  are dark, then take the HOST baseline. Perform the one write in HOST-PEER and
  wait for final lamp fall. Take the HOST result first while the relation is
  still HOST-PEER. Only then attach USB to the former PEER, promote it, and take
  its result snapshot. Promotion does not reset the edge record: only storage
  init and a local `WIRE_DIAG` snapshot do, in
  `split/era_host_peer_storage.c` and
  `split/diagnostics/era_split_wire_diagnostics.c`. Promotion deliberately
  appends a trailing `service leave` event; when it follows the operation's
  final indicator fall it is extraction context and not the blink cause. A
  `service leave` before an in-operation indicator fall is instead a finding.
  A storage-triggered fast revalidation is specifically **not** service leave:
  the initiator preserves the peer mirror while bootstrap recovery remains
  below its backoff threshold, so a healthy recovery must end by carrier fall
  (`event 4`) rather than by `event 7`. If the peer remains absent until that
  threshold, `event 7` is the bounded presentation retirement.
  Do not reboot the promoted half, because the record is RAM state.

  `wr` is the dynamic-macro EEPROM-change notification count;
  `at=first/last` gives their offsets from the baseline in milliseconds;
  `gap=last/max` gives the last and largest inter-notification gap; and `oq`
  counts gaps at least as large as the 1000 ms dirty-quiet boundary. `n/ov` are
  retained edge count and overflow. A receiver normally has `wr=0`. These are
  **not RAW-HID request counters**: `nvm_dynamic_keymap_update_changed_runs()`
  in `quantum/nvm/eeprom/nvm_dynamic_keymap.c` notifies only when at least one
  byte in a request differs from the cache, and the final durable marker adds a
  separate trailing notification. Consequently `gap/oq` on this line cannot by
  itself prove that VIA paused between requests.

  Each comma-separated `ev=` entry is
  `event/arms/rs/domain/dirty/changed/generation@ms`. Event ids are
  `1=advertised rise`, `2=advertised fall`, `3=mirror rise`, `4=mirror fall`,
  `5=indicator rise`, `6=indicator fall`, `7=service leave`, `8=changed-shadow
  rise`, `9=changed-shadow fall`, `10=dirty rise`, and `11=dirty fall`.
  `arms` is hexadecimal: `01=dirty`, `02=visible changed shadow`,
  `04=visible content-moving cell`, `08=visible summary`, `10=visible content
  expected`, `20=moving span`,
  `40=peer mirror`, and `80=service/policy gate`. `rs >> 5` is the storage role
  (`1=PEER`, `2=HOST`) and `rs & 1F` is the runtime state; domain is decimal,
  dirty/changed are hexadecimal domain masks, and `@ms` is interval-relative.
  The 64-event recorder lives in `split/era_host_peer_storage.c` and saturates
  timestamps at 65535 ms.

- `wire via macro`: `cause`-only timing for actual
  `id_dynamic_keymap_macro_set_buffer` RAW-HID requests. It is snapped and reset
  by the same local `WIRE_DIAG` boundary as `wire storage edge`.

  `rx/rsp/dr` are set-buffer receives, returned generic VIA responses, and RAW
  IN endpoint-drain epochs. `dr` can be smaller than `rsp` only when the host
  pipelines requests: one drain then closes the whole queued batch. `at` and
  `gap/oq` have the same shapes as the storage-edge fields but are taken at
  actual request entry, so this `oq` is the one that establishes whether the
  host/firmware exchange really crossed the 1000 ms boundary.

  `h=total/max` is request entry through the dynamic-macro handler;
  `send=total/max` is time spent inside `raw_hid_send()` admitting the response
  to QMK's RAW IN queue; `drain=total/max` is from that return until the endpoint
  first becomes idle; and `app=total/max` is idle endpoint to the next macro
  request. All four are milliseconds. `app` is recorded only when no other RAW
  command intervened; `int` counts the excluded intervals. `ovl` counts a new
  macro request observed while the preceding response was still pending, and
  `p=1` means the snapshot caught an undrained response. The cause-gated seams
  are in `quantum/via.c`; the state and endpoint-idle poll are in
  `split/diagnostics/era_via_macro_diagnostics.c`.

- `wire storage ppath`: `cause`-only boundary timing for the initiator-to-
  responder `STORAGE_PENDING` carrier. All timestamps are milliseconds from the
  same `WIRE_DIAG` interval baseline as `wire storage edge`; `65535` means that
  edge was not observed in the interval. Each `rise/fall` pair is one ownership
  boundary: `pub` is Core0 publishing a changed standing plan, `tx` is the
  initiator Core1 completing the wire exchange that carried that value, `rx` is
  the responder Core1 decoding that section, and `app` is the responder Core0
  applying it to the peer-pending mirror. The third `pub` field is the **fall**
  context mask: bit0=`plan enabled`, bit1=`SESSION_STATUS revalidation pending`,
  bit2=`storage route exclusive`.

  **The four-stage clock is the RP2040 free-running hardware timer, read
  directly on both cores.** It deliberately does not call QMK/ChibiOS
  `timer_read32()`/`timer_elapsed32()` (`platforms/chibios/timer.c`): TX and RX
  run on bare Core1, where that API's scheduler lock and Core0 timer-cache
  mutation are not admitted. Each stage also owns its own validity/level bytes
  rather than sharing a bitmask RMW with the other core. This is diagnostic
  isolation, not storage behavior.

  Read the gaps literally. A large `pub(fall) -> tx(fall)` belongs to the
  initiator's standing/revalidation path; a large `tx -> rx` belongs to the wire
  or responder admission; a large `rx -> app` belongs to responder-result/Core0
  drain. `app(fall)` should coincide with edge event `4=mirror fall`, providing
  an independent check that the probe did not change the meaning of the older
  recorder. The rise is retained as the same-path control: if rise is poll-scale
  and fall is not, the defect is transition-specific rather than general link
  latency. This line localises an observed bound violation before any storage or
  scheduler semantic change; it is compiled out of release builds.

  For a carrier-caused blink, the source's event `2` precedes the receiver's
  `4`, then the receiver's `6`. A source event `11` and event `9` before event
  `2`, followed by more macro notifications or `oq>0`, identifies a write
  stream that crossed the quiet boundary and temporarily retired every local
  lamp arm. Event `7` instead identifies a service/role exit after any fast
  recovery continuity has ended. Keep the adjacent
  `wire storage cause`, `wire storage`, and `eeprom shim ind` lines from the
  same snapshot; the edge line identifies the falling carrier while the storage
  lines identify its transfer/NVM context.

## The qwin Window

- `wire qwin`: two-press QMK matrix scan counter diagnostic. `WIRE_QWIN` starts
  the silent counter window on the first press and emits one line on the second
  press in `ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE=yes` builds.
- `wire qwin pc/ms/scan_hz/raw/start/end`: qwin print count, elapsed window,
  computed scan rate, scan-count delta, start counter and end counter.
  **`scan_hz` is core0's pass rate.** Since the PIO sampler it no longer says how
  often the keys are looked at — `smp_hz` does — and the two must not be
  confused: the pass rate is what the 18 kHz floor and the cost gates read, the
  sample rate is the sampler's own health.
- `ccore`: qwin communication-core loop/idle/wake/wake-observed deltas.
- `sp`: qwin core1 HOST-PEER source-push transaction delta. It was `hbsp`, a
  heartbeat/source-push pair, until the heartbeat lane retired; a capture
  printing two values there predates that.
- `sess`: production CORE1 qwin `SESSION_STATUS` transaction delta, separate
  from `sp`.
- `park=count/us`: core1's parks over the window and the microseconds it spent
  asleep in them — the backend's in-transaction parks
  (`era_split_transaction_backend_park_until()`,
  `split/era_split_transaction_backend_rp2040.c`) and the loop's idle park
  together. The sleep share is `us` over the window's `ms` × 1000. Diagnostic
  images only.

  **A park is credited entirely to the window it ends in**, because the counter
  is cumulative and the window reads a delta. So `us` may exceed the window
  whenever parks are long against it, and that is arithmetic rather than a
  defect: a no-cable responder half's idle first-byte window is 60 s
  (`ERA_SPLIT_RESPONDER_FIRST_BYTE_TIMEOUT_MS`,
  `split/communication_core/era_split_communication_core_responder_service.c`),
  so a park may have started before the window opened. **Take a sleep share only
  where the parks are short against the window** — every cabled reading is, at
  roughly a park per wire byte.

  Two shapes of that measurement are recorded because they are not guessable. A
  60 s responder window is served by **exactly two parks of 29,999,997 µs**, not
  one of 60 s, on every image measured — one unexplained mid-window wake per
  window, costing one iteration of the receive loop and nothing else. And an idle
  no-link responder's core1 loop does not reach its own idle park at all, so
  every park there is a backend one: `ccore` shows `loop` advancing by one or two
  per minute against `idle=0`.
- **`ccore=loop/idle/wake/observed` is also how a capture says which relation it
  was taken in**, and reading it that way is not optional — a qwin line carries
  no other statement of the connection, and a sitting's own labels have been
  wrong. Two fields settle it:

  | shape | relation |
  | --- | --- |
  | `loop` ≈ 100/s, **`idle` = 0** | **HOST-PEER**, this half the HOST |
  | `loop` ≈ 2,955/s, `idle` ≈ 1,970/s | **DUAL-HOST**, this half the initiator |
  | `loop` ≈ 6/s with `sess` = `loop`/3 | **no cable** — discovery backoff only |

  `idle` is the decisive one and it is structural rather than empirical: a
  HOST-PEER HOST is the wire **responder**, and the responder arm services on
  every core1 pass and `continue`s, so the idle park is unreachable and the field
  cannot leave zero. An initiator parks between polls, so it cannot stay at zero.
  The rates follow from the periods — 10 ms for the HOST-PEER standing exchange
  against 1 ms for DUAL-HOST, at about three loop iterations an exchange.
- `settle=ms`: the start-sample deferral in force (`ERA_SPLIT_QWIN_SETTLE_MS`,
  `0` = the start sample is taken at the first press). With a nonzero settle the
  window's `ms` counts from the deferred sample.
- `smp=frames smp_hz=`: PIO sampler frames the sample DMA moved over the window,
  and the rate. Read from the DMA transfer count, so the figure costs the scan
  path nothing; a `rearm` change inside the window makes `smp` meaningless for
  that window.
- `ovr=`: frames re-read because the DMA writer lapped them mid-copy —
  structurally zero at this board's ratio of pass to frame; a nonzero value names
  a core0 stall of three frames or more landing inside a frame copy.
- `rearm=`: DMA re-triggers after a transfer count ran out (about every
  93 minutes per channel at the eight-slot frame). Zero across a 60–80 s window;
  nonzero explains a stalled `smp`.
- `fd1=`: the sampler state machine's PIO1 FDEBUG bits since the window began,
  TXSTALL:TXOVER:RXUNDER:RXSTALL in bits 3..0. RXSTALL means the DMA fell behind
  the sampler, TXSTALL that the pattern feed did; both read `0` on a healthy
  sampler outside a re-arm gap.
- `seg=r1,r2,…`: the scan rate of each `ERA_SPLIT_QWIN_SEGMENT_MS` slice (10 s)
  of the window, in order, up to eight; `-` when the window was shorter than one
  slice.

  **A qwin window can step once, and where the step comes from decides whether
  it is a finding.** The reactive hit tracker's entries live `UINT16_MAX`
  milliseconds — **65.5 s** — after the key that made them, and a qwin window is
  *opened by a keypress*, so under a reactive effect the window's first 65 s
  carry live entries and its tail does not. **Every reactive-effect window
  therefore steps once at 65.5 s and its tail is the hit-free rate.** On a
  non-reactive effect a step is a finding: the decay walk that used to produce
  one on every effect was removed, and a non-reactive window now reads flat end
  to end.

  **A key-quiet qwin figure understates a fix that sits in the reactive path.**
  The decay-walk removal is the recorded case: qwin moved +14.1 % key-quiet while
  the `wire` variant's HOST-PEER *typing* rate moved **+21.7 %**, because a
  keyboard being typed on carried the full eight entries the walk read. Quote
  both when a change touches anything the hit tracker feeds, and only qwin when
  it does not.

### The `qwin_phase` rung

Everything in this block prints on the `qwin_phase` rung alone. **The rung's own
`scan_hz` is not a comparison point** and its segments are read net of the
instrument's price, which the sitting measures against a plain `qwin` window of
the same length — about **4 µs a pass**, spread roughly evenly across the twelve
segments. Subtract the sitting's own measurement of it, not a figure quoted
forward: the price falls with the number of marks an image stamps, so a
correction carried over from an older sitting over-corrects.

- `ph=passes us=r,d,x,s,f,a,q,g,t,l,h,e`: the pass itemised. `ph` is the passes
  charged over the window and the twelve `us` are that window's accumulated
  microseconds per segment, in execution order — **RAW** (the frame fetch and
  decode), **DEB** (the rest of the scan: hooks, debounce, local publish),
  **XPORT** (the transport step), **SCANHK** (the rest of `matrix_scan()`),
  **DIFF** (`matrix_task()`'s walk against `matrix_previous[]`), **ACT** (the
  tick event or the changed-row walk), **QTM** (`quantum_task()`), **RGB**
  (`rgb_matrix_task()`), **KTAIL** (`mousekey_task()`, `led_task()`), **LOOP**
  (`protocol_post_task`, raw-HID, console, housekeeping down to the split
  skeleton), **HK** (`era_split_keyboard_task()`), **REST** (back to the next
  `matrix_scan()`). The names and the marks are in
  `system/era_pass_phase_diagnostics.h`.

  **Divide by `ph`, never read a figure raw**, and take a window long enough to
  hold millions of passes before believing any segment. A single
  pass's segment is meaningless — the counter has one microsecond of resolution
  against sub-microsecond segments, and what makes the mean right is millions of
  passes with the boundaries drifting across the microsecond.
- `pmax=us mx=r,d,x,s,f,a,q,g,t,l,h,e`: the window's **worst whole pass**, and
  the worst each segment contributed, in the same order as `us=`. **Absolutes,
  not deltas** — cleared when the window opens, because two snapshots cannot
  subtract into a worst case.

  **This is the half of the instrument a placement decision uses.** Rendering is
  not a per-pass cost: it is a handful of working passes per 16 ms frame among
  hundreds that do nothing, so `us=` reports its average and can never report its
  spike, and a keyboard's felt latency is its worst pass. Read a maximum only
  where `us=` says the segment is doing something.
- `over=n1,n2,n3`: passes in the window that ran **50 µs, 100 µs and 200 µs or
  longer** — deltas, and each band is a subset of the one before it. **A maximum
  cannot tell a once-a-minute outlier from a thousand-a-second rhythm, and those
  are different designs**; this is the field that separates them.

  **The bands are absolute microseconds and this rung's baseline pass is not the
  shipping one**, so a population near an edge is read with the instrument's own
  price added. That is why a chunk size is chosen to clear an edge on *this*
  image — clearing it only on the shipping one leaves the band unreadable.

  **The band's owner is named, and its rate is the same on every half.** With RGB
  switched off, both halves put about **100 passes a second** over 50 µs. That is
  the scheduler's maintenance body running at the **authority sample's 10 ms
  deadline** — the architecture's one poll (`era_overview.md`), whose period is
  `ERA_SPLIT_AUTHORITY_POLL_PERIOD_MS`, derived from the SOF freshness window
  rather than chosen
  (`split/scheduler/era_split_transport_scheduler_internal.h`). What varies
  between configurations is the body's **cost**, not its rate: tens of
  microseconds standalone against ~154 µs as a HOST-PEER HOST.

  **So a LEFT/RIGHT gap in this field is usually the band edge and not a
  different event**, and mistaking one for the other costs a whole investigation:
  one half's cluster landing under an edge and the other's over it reads as a
  seventy-fold difference in the same event. **Read `hkmx`'s middle column and
  the ≥50 count beside this one** before concluding that two halves differ.
- `hk=f,s,t hkmx=f,s,t`: the `HK` segment split into its three measured parts and
  one derived one, all inside `era_split_keyboard_task()`
  (`split/era_split_keyboard.c`) — the feature-task facade
  `era_common_features_task()` (`system/era_common_features.c`), the split
  scheduler task, and the once-per-millisecond gate body. Accumulated
  microseconds and this window's maxima. **The fourth part is derived**, not
  printed: `us[HK]` minus the three is the wire diagnostics task plus the
  plumbing. It exists because `HK` is the largest segment and the one whose
  maximum moves most between relations, and a segment mean cannot say which of
  its four parts carries that.
- `rgb=t,s,r,f,y rgbn=… rgbmx=…`: the `RGB` segment split into the per-pass
  timer head `rgb_task_timers()` and the four arms of the state machine beside it
  — starting, rendering, flushing, syncing — all five in
  `quantum/rgb_matrix/rgb_matrix.c`. Accumulated microseconds, pass counts and
  this window's maxima. The remainder of `us[RGB]` is the switch and the state
  read.

  **`rgbn` is what makes a maximum readable here.** At most one arm runs per
  pass, so the counts say how many passes a 16 ms frame spends in each, and an
  arm's maximum is then that arm's own cost rather than an outlier. **The render
  arm's is the figure a core1 chunk bound is set from.**

  **At most one, and not exactly one.** The idle gate
  (`RGB_MATRIX_IDLE_GATE_ENABLE`, `quantum/rgb_matrix/rgb_matrix.c`) returns from
  `rgb_matrix_task()` before the timer head on a `SYNCING` pass whose millisecond
  has not turned over, and such a pass stamps neither the head nor an arm. Three
  consequences: the `SYNC` count is the passes on which the **gate opened**,
  about one a millisecond rather than one a pass; **the gated passes are `ph`
  minus the four arm counts**, which is the only place they are visible; and
  `rgb=`'s `TIMERS` and `SYNC` accumulators are amortised over the whole window
  while being spent about a thousand times a second. `us[RGB]` still closes on
  every pass — that mark is `quantum/keyboard.c`'s — so a gated pass contributes
  the QTM tail and the RGB head to it and nothing else, which is why `us[RGB]/ph`
  falls much further than the arms' own sum does.

## The Key Path And The Scan Facts

- `wire keypath n= avg= max= h=b0,…,b7`: the PEER's key-path span — core0
  publishing a source-push to core0 applying its ACK, so it contains core1's
  pickup, the wire exchange and the HOST responder's turnaround. Microseconds;
  the histogram's bucket upper edges are 320, 384, 448, 512, 640, 896, 1408, then
  everything above — centred on the measured **402 µs source-push exchange**
  (`wire hp peer_tavg sp`). **Only the first half of this span is on the key's
  path**: the matrix crosses in the request, and the ACK it waits for is
  bookkeeping — so what the buckets separate is whether the request waited for
  core1 before it started, which is the term any new core1 work would move.

  **It is the only figure here about the half whose keys do not travel on
  core0.** Every other performance number in this document is a core0 scan rate,
  and in HOST-PEER the PEER's keys reach the computer over the wire. Cumulative
  and free-running because the half that produces this line has no console, so it
  is read after that half is given a USB host.

  **Read the histogram, not the mean.** A mean cannot separate a clean round trip
  from one that waited behind another exchange, and that difference is the entire
  question whenever new work is proposed for core1.
- `wire qmk scan_hz/raw/raw_us/raw_max` (+ `smp_hz/ovr/rearm/fd1/fw` on the PIO
  sampler): the wire variant's scan facts. `raw_us` and `raw_max` bracket the raw
  read; on the PIO sampler that bracket is the frame fetch and decode, which is
  core0's whole share of the scan on that backend. `smp_hz` is the sampler frame
  rate between the two captures, `ovr`/`rearm` the boot-cumulative counters the
  qwin bullets define, `fd1` the stall flags since the previous capture, `fw` the
  slots per frame.

## Scheduler State And Maintenance

- `bem`: wire-diagnostics transaction-engine mirror fresh/fallback-count. Fresh
  `1` means the current capture proved one stable even publication. Fresh `0`
  means it discarded a colliding candidate and used the last previously proven
  core0-local snapshot; the second value is the cumulative fallback count.
- `be`: transaction backend initialized/init-role/current-role. **Init-role is
  the handedness latch and never moves — LEFT `1` initiator, RIGHT `2` responder
  — so `init-role != current-role` names a half running inverted against its
  latch**, which is what a cabled LEFT that becomes HOST does and a cabled RIGHT
  that becomes HOST does not. That comparison is the cheapest predictor of a
  core1-death wedge: a forced core1 kill wedged core0 on exactly the two inverted
  combinations and on neither un-inverted one. Read the pair, not the third field
  alone.
- `bri`: transaction backend reinit-on-role-change flag.
- `initwork`: actual scheduler initialization work count.
- `hkwork`: actual scheduler housekeeping maintenance work count.
- `wire maint entry/stor/rsp/stand/init/time/mode/route`: the decomposition of
  `hkwork` by contributor, read cold at print time.
  `ERA_SPLIT_SCHEDULER_MAINT_SOURCE_*` in `era_split_transport_scheduler.h` fixes
  the column order; the names are printed short and **two of them are traps.**
  `init` is `CORE1_INITIATOR` — the core1 result poll, the drain half of the
  per-exchange cost — and is unrelated to `initwork` on the `wire sched` line,
  which is scheduler *initialization*. `rsp` is the responder drain, not a
  response count. `stand` is the standing state's own apply, so on a serviced
  relation's initiator it is where the grant's core0 cost lands and is the column
  that answers *why* `hkwork` is what it is. `entry` counts task bodies that
  passed the due gate, so `entry - hkwork` is the passes that woke core0 and
  found nothing to do.
- `rsnp` on `wire maint`: responder-snapshot publish retries — a differing
  snapshot that hit a core1 claim collision or an undrained result and re-armed
  the due latch. Small single digits per session is the designed reading; a rate
  rising with typing says the publish is fighting the claim window.
- `plan`: full scheduler mode-planning run count.
- `dirty`: cached scheduler dirty flags.
- `due`: cached scheduler route due flags. **Two live route bits, one non-route
  bit, and four retired un-reused**, per `era_split_scheduler_events.h`: bit
  `0x01` attach/status and bit `0x04` the HOST-PEER matrix source-push. Bit
  `0x10` is not a route at all — it is the standing plan's publication bit, and
  it lives in this word because `scan_idle()` reads this word. Retired: `0x02`
  (HOST-PEER liveness heartbeat), `0x08` (HOST-source response poll), `0x40`
  (AUTHORITY push) and `0x20` (an earlier response poll). A `due=` value is
  captured, so none of the four is recycled.
- `open_ms`: the two boot instants, in milliseconds since `timer_init()` at the
  top of `keyboard_init()`, at which the explicit core1 launch step was entered
  and returned. Captured once per boot in keyboard post-init, wire-diagnostics
  diagnostic variants only, saturating at 65535. Entry dates the wire opening against
  everything `keyboard_init()` does before it, and on the half without a cable it
  is the direct check that no boot-master poll runs inside `split_pre_init()`
  upstream of the reading. Exit minus entry is the launch handshake's own cost.
  Both are boot-only: a nonzero delta between two captures of the same boot is a
  reading error, not an event.
- `owner`: owner route step/kind/reason.

### Runtime section counters

- `wire sect tx/rx sync anc`: cumulative runtime section counts, the shared clock
  and the time-anchor watch. All free-running from boot and printed in every
  relation, which is the point of the line — **the half these describe is usually
  the one that cannot print anything.**

  `tx`/`rx` are the same underlying pair as `wire dh era rt=` and differ only in
  the unit of time: `rt` is era-scoped and already differenced, and its block
  accumulates only in DUAL-HOST, so a HOST-PEER capture reads its runtime
  sections here and nowhere else. Counted in sections and never in polls.
- `app=vis/rgb/auth` on the same line: the visual, RGB and AUTHORITY section
  apply counters, cumulative from boot so a post-promotion capture carries the
  whole PEER era. **There is one initiator apply path, so each value crosses once
  and applies once** — a step down here is a real loss. The per-lane `rgbn`
  counter on `wire csp` is distinct and must read zero.

  **These count distinct applied values.** The standing record is latest-state,
  so the apply block is reached whenever *any* field of it moved and each arm
  re-applies its own unchanged cached value on every unrelated section's edge —
  deliberate, and fatal to a counter incremented at the call site. The counters
  therefore take their answer from the unit that holds the value. **A capture
  predating that correction inflates all four**, in proportion to how much
  unrelated standing traffic the window carried, and any rate derived from one
  must be re-taken rather than re-scaled.

  **`app=rgb` counts every RGB-state apply on this half, on both of its
  producers** — the standing apply and the DUAL-HOST push-receive apply — one
  counter per fact. `app=vis` has the same two producers; **`app=auth` has one**,
  the standing apply, which makes it path-complete by construction. The DUAL-HOST
  readings it anchors: an arrival that the receiver's RGB policy bit gates off
  moves `rt`'s rx side and **not** `app=rgb`, which is the console's "sent but not
  applied"; a sender whose own bit is off moves neither, which is "didn't send".
  **A one-poll RGB deferral behind a due AUTHORITY is the designed order, not the
  starvation falsifier**: starvation means frozen across a window, not late by
  one poll. In DUAL-HOST `app=rgb` counts configuration-only applies.
- `lay`: the INPUT-class apply leg, counting distinct applied peer-layer values
  inside the policy gate — what makes the layer byte readable the way `app=rgb`
  makes RGB readable, and subject to the same correction. The INPUT policy
  readings mirror RGB's three-way pattern: sender off — one neutral crossing at
  the disable edge, then "didn't send" (`adv` may still tick; it is an internal
  composition with no wire effect, so **read sends from `rt`/`atx`, never
  `adv`**); receiver off — `rt` moves while `lay` and `aap` freeze; both on —
  unchanged readings. `app=vis` moves in DUAL-HOST too (the visual cell's
  diff-replay apply), behind the receiver's RGB bit, at two per keystroke on the
  sending half.

### The shared clock and the time anchor

`sync` is `sync_timer_read32()` (`quantum/sync_timer.c`), the shared clock,
beside `wire pc last=`, which is this half's local one; a half that has never
applied an anchor reads them within the print's own skew of each other and the
difference is the applied offset.

- `anc=applies/back/back_max_ms`: the anchor watch — applies, backwards steps,
  and the worst backwards step in milliseconds. **No document sets a numeric
  pass condition on `back_max_ms`**; how to read it is three paragraphs below,
  and the reading is what decides. `back` is a counter
  rather than a field because a sampled clock cannot see a step that happened and
  was corrected, which is exactly what a latched timestamp produces.
- `corr=signed_ms/interval_ms`: the last signed step the setter applied to the
  shared clock, and the local-timer interval since the previous apply. Their
  quotient is the drift rate — after the send-side stamp the delivery term is
  sub-millisecond, so the residual the pair measures is crystal drift. Signed,
  because the direction is part of the reading; `interval_ms` is `0` on the first
  apply of a boot. Read the pair from one line — they are written together at the
  apply.

**`applies` and `corr` count arrived anchors; `back` and `back_max_ms` count
every apply.** The setter is reached on every standing edge and re-applies the
same cached anchor many times per arrival, so counting re-applies made `corr`'s
interval the gap between two re-applies of one anchor and the quotient
meaningless. `back` stays per-apply because a step backwards under the consumers
is real whoever caused it.

**`back` is a truncation counter and carries no finding by itself.** The
re-apply's held term is a millisecond quotient, so it advances in whole steps
while the shared clock advances continuously and loses up to 1 ms each time it
rolls — hundreds of backward steps against tens of arrived anchors is the
ordinary reading. Read `back_max_ms`; `back` answers only "how many re-applies
happened". **The re-apply is not idempotent in the way the correction makes it
look**: the millisecond division is what steps the clock back.

**The first apply of a relation era is the offset adoption, and it is counted
into `back` like any other.** It consumes the difference between two halves'
independently started clocks, so on a fresh pair it can set `back_max_ms` to a
value the steady-state residual never approaches. Relation rotation clears the
behavioural *adopted* fact while leaving the corrected shared-clock value in
place; LINK_SPEED/CLEAN cannot arm a fresh shared-clock deadline until the new
relation's anchor arrives. This matters in sequential-reboot captures: a large
new correction after the peer comes back is legitimate epoch adoption, while a
deadline armed only because an older relation had once supplied an anchor is
not. `applies=1` with `interval_ms=0` identifies only the first arrived anchor of
the boot because the diagnostics stay cumulative; later relation adoptions keep
the ordinary inter-arrival interval. The relation's anchor sender is the
responder in both relations (the HOST, or the DUAL-HOST Right), so this reads on
the initiator half.

Read `sync=` against `wire pc last=` with the print pipeline in mind: `last=` is
stamped once at capture and `wire sect` prints many paced 150 ms lines later, so
the pair differ by the pacing distance plus the applied anchor offset. On a
time-source half (the wire responder; `sync_timer_is_time_source()`,
`split/era_split_transport_scheduler.c`) `sync` ≡ the local timer and the
difference is the pacing distance alone — which is how a frozen pacing-distance
reading on an *adopting* half becomes the offset-never-landed signature of an
inert anchor apply. **Never read their difference as the anchor offset without
subtracting the pacing distance.**

`ahold=last/max` on `wire crsp`: the send-side hold in microseconds — the elapsed
between core0 capturing the anchor reading and core1 encoding it into a response,
per anchor sent and the worst seen. The value on the wire is already corrected by
it: `ahold` is the instrument's record of what was corrected, not a residual
error.

### Tap-hold activity

`wire act win=n spec=a/r/x jh=h/p rcl=n adv=n atx=n aap=n`: the tap-hold family,
every counter cumulative from boot and printed in every relation.

- `win` counts judgment windows opened — a tap-hold press with any of the three
  tapping options armed — so fresh defaults hold it at zero, which is the silence
  legs' first reading.
- `spec` counts the ERA layer-tap half of the speculative family alone:
  activations, tap-settle reverts, and overflow-clear aborts, so `a - r - x` is
  the holds that kept their speculative *layer*. Upstream's mod-tap half
  increments nothing, so an MT-heavy capture with `spec` frozen is the counter's
  scope, not a failed arming.
- `jh` is the cross-half settles by judge — hold-on-other-key-press first, the
  permissive-hold approximation second — and `rcl` the retro taps a peer press
  cancelled; all three move only on the half whose key was in flight.
- `adv` counts changes of the advertised ACTIVITY value and `atx` the
  responder-confirmed sends of the section (an initiator's sends read from the
  generic `rt`/`tx=` pair); `aap` counts peer values actually applied into the
  judgment cache. **The three-way console distinction is deliberately
  cross-half**, and DUAL-HOST has a console on both halves: this half's `adv`
  (and `atx` on a responder) moving while the other half's `rt` rx side sits
  still is "didn't send"; the peer's `rt` rx moving while its `aap` sits still is
  "sent but not applied"; its `aap` moving is "applied".

## Reset Edge

- `wire edge reset`: diagnostic-only local serial owner/role reset boundary
  snapshot. It carried `hbmiss=pre/during` until the heartbeat lane retired; what
  still brackets a boundary here is `dtap_ms`. A boundary includes same-role
  restarts after flash, relation rotation and diagnostic flush, and the
  `rel/prev` fields separately identify the last mode edge.

## Relation-Role Lines

- `wire resp`: compatibility responder diagnostics projected on core0 from
  generation-matched results executed by core1; it is not a core0 responder
  thread.
- `th/svc/blk/io/trn/rst/qsc`: responder thread-started, service-enabled,
  admission-blocked, in-serial-IO, in-transaction, reset-requested and quiesced
  compatibility state. In the final profile `th=0`, `svc=0`, `blk=1`, `io=0`,
  `trn=0`, `rst=0` and `qsc=1` are structural proof that no core0 responder
  executor is active. They are constant by design; a nonzero `th`, `svc`, `io` or
  `trn` would mean a core0 responder had been revived.

HOST-oriented (`wire hp role=host`): `pc` peer cache valid; `pseq` HOST peer
cache sequence; `ack` ACK_STATUS hz/count; `vis` HOST visual-resync tx count;
`rgb` HOST RGB-state tx count; `cache` update/project/flush counts; `rrx/hrx/srx`
relation request, heartbeat request and source-push request rx.

PEER-oriented (`wire hp role=peer`): `lmr` local matrix ready; `f` forced
source-push token; `seq` current/host-known source-push sequence; `sp`
source-push hz/tx/ack; `vis` HOST visual-resync rx count; `rgb` HOST RGB-state rx
count.

**There is no `hb` on the PEER line, no `hb` on `hp peer_era`, and no `wire chb`
line at all; a capture that has any of them predates the heartbeat lane's
retirement.** They counted the core1 HEARTBEAT *initiator lane*, which nothing
enqueued once the standing exchange took the relation's periodic traffic, so both
read frozen for a whole development phase before retiring with it. **The
relation's traffic is in `io` and `rt`.** A counter that froze because its
carrier moved is not evidence that the thing it named stopped, and that is the
reading error these two invited.

**`vis` and `rgb` count distinct applied states**, not every frame that repeated
one, because core1 reports those sections on an edge and core0 applies from the
standing state. They are counted at the apply, so a section that stopped arriving
cannot hide behind a carrier that moved.

**They are also structurally unreachable.** The PEER line is printed only while
`rel=` is `HOST_PEER_PEER`, and a PEER has no open USB host by construction, so it
has no console; after promotion the half prints `role=host` or `role=off`, neither
of which carries these two fields. What the Active-Cable gate reads instead is
`wire sect rx` for the arrivals, `wire sect app=` for the applies, the HOST's own
`wire hp role=host vis=/rgb=` tx counts for what was sent, and the visual baseline
by eye. **Read `wire sect rx` and not `rt` for arrivals in this relation**: `rt`
prints only on `wire dh era`, which accumulates only in DUAL-HOST.

## Communication-Core Lines

### Lifecycle — `wire ccore`

- `av`: communication-core source is compiled and available.

  > **REFUSED:** delete the six `av=` fields and `rxm=` as dead weight.
  > **WHY:** they are structurally-constant instruments — a constant that is
  > *supposed* to be constant is the reading, so deleting one deletes the
  > measurement rather than dead code.
  > **REOPENS:** a field whose constancy nothing depends on.
- `init`: communication-core state initialized.
- `att`: a launch handshake is in progress or has already succeeded. It is not
  cumulative and it is not a history:
  `era_split_communication_core_start()`
  (`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`)
  sets it and rolls it back to `0` when the handshake fails, so a half that has
  attempted and failed thousands of launches still reads `att=0`. Read `ec`'s
  first field for what was actually attempted.
- `launch`: core1 entry published launched.
- `run`: core1 idle actor reports running.
- `stop`: quiesce requested. The field and counter names keep the `stop` spelling
  because they are the published diagnostic identifiers; the API behind them is
  `era_split_communication_core_request_quiesce()`, which parks core1 rather than
  stopping it.
- `wake`: wake pending.
- `err`: launch-error flag/stage/phase. Stage is the handshake word the failure
  happened at, `0`-`5` over `{0, 0, 1, vector_table, stack_end, entry}`. Phase is
  `0` none, `1` FIFO write timeout, `2` FIFO read timeout, `3` echo-mismatch
  restart cap. The distinction is the whole diagnostic value of the field: `1`
  and `2` mean the peer core did not answer, `3` means it answered inside the step
  timeout and answered wrong, and only `3` is the failure the restart cap exists
  for.
- `eto/sto`: entry timeout and stop timeout flags.
- `cnt`: start/stop/wake/wake-observed counts.
- `loop/idle`: core1 idle actor loop/idle counts.
- `park=count/us idleus=us`: the backend's in-transaction parks and their total
  microseconds, and the loop idle park's total microseconds, all boot-cumulative
  — deltas against the elapsed window, never a single capture's total over its
  uptime. `park` counts every backend wait in both roles: the responder's
  windows, and the initiator's response-window, FIFO-full and drain waits. Zero
  on the release image.
- `ec`: launch-error/entry-timeout/stop-timeout counters.
- `cap/dead`: the per-boot launch-attempt cap latch, and the count of times core0
  judged core1 dead and hardware-reset it for relaunch. Both are structurally
  zero on a healthy boot; `cap=1` is the give-up state (`LOCAL_NO_LINK` by
  policy, and the latch never clears in-session), and a nonzero `dead` names a
  post-boot death the owner layer recovered from — read it beside `cown`'s
  `reclaim` field, which moves with it.

  **What clears the streak behind the latch is an observed return to core1
  service, not a successful launch.** `era_split_communication_core_start()`
  (`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`)
  returning true proves only the handshake and the entry flags, and the device
  showed that this is compatible with a core1 that dies at its first loop pass —
  so the clear is the owner layer's service report, and a ready wait expiring
  counts *into* the streak. Re-keying the clear on `start()`'s own success makes
  the cap structurally unreachable, whose device shape is a half that never
  converges to `LOCAL_NO_LINK` after a post-boot core1 death.

  **A zero here proves nothing about a half that wedged, because a wedged half
  cannot print.** The console is core0's, so the only captures a forced-kill run
  yields are from halves that survived — and a half survives by taking
  `owner_ensure_core1_role()`
  (`split/communication_core/era_split_communication_core_owner.c`)'s fast path,
  which returns without touching core1 and leaves `cown`'s `revoke` at zero. So
  `dead=0 cap=0 revoke=0` on a survivor is the fast path working, not the
  judgment being reached and declining.

### Queue — `wire cqueue`

`gen` queue generation; `cap` usable SPSC ring capacity; `q` request queue
level/high-water; `r` result queue level/high-water; `flush` queue flush count.

### Owner — `wire cown`

- `own`: backend owner, where `0` is unavailable and `2` is core1. Numeric value
  `1` is retired/unassigned and must not appear.
- `role`: active backend role, where `0` is disabled, `1` is initiator, `2` is
  responder.
- `epoch`: current owner/released/ready epochs.
- `rev`: revoke/cancel/reset pending flags for the current epoch.
- `cnt`: owner transfer/revoke/release/ready counts.
- `err`: owner transfer-timeout/ready-timeout/init-fail/reclaim counts. The
  fourth counts leases reclaimed from a judged-dead core1 — two consecutive
  revoke-wait timeouts, then declare-dead and a core0-side completion of the
  transfer. Structurally zero in ordinary service.
- `io`: queue-expired/owner/epoch/cancel/reset/PIO-error/send-timeout/
  response-timeout/partial-frame/IO/decode/response-contract counts, in that
  order.

### Session — `wire csess`

`pend/ready` session request pending and result-ready latches; `gen` current
session request generation / last drained result generation; `sub` session
submit/accept counts; `full/sf` pending-or-queue-full and core-start/owner-ready
failure counts; `tx/res` core1 session transaction and core0 result-drain counts;
`stale/rfull` stale result and result-slot-full counts; `ok` OK/MISS/BAD/FAIL
result counts; `last` result/failure/request-sent/route-kind/route-reason;
`rsp/peer` response kind and decoded peer-status-valid flag.

### Responder — `wire crsp`

- `src`: responder snapshot source, where `0` is none and `2` is a live
  core0-published responder admission/response snapshot. Value `1` belongs to a
  retired, unreachable non-FULL diagnostic probe.
- `snap`: responder snapshot valid latch.
- `slot/ready`: source-push result-slot reserved / result-ready latches.
- `own`: responder backend-owner gate readiness, ready-count and blocked-count.
  Production responder snapshots require the gate to be ready before admission.
- `gen`: owner epoch / relation generation / current snapshot generation / last
  drained result generation.
- `pub/resv/full/acc/stale/drain/noack/arx/undec/quiet/coal`: snapshot publish,
  result-slot reserve, slot full, accepted-result publication, stale reject,
  core0 drain, no-response prevention, accepted-RX, undecodable-arrival and
  quiet-answer counts, then coalesced HEARTBEAT replies / the runtime sections
  they physically carried. There is no `clr`: the responder rotates by
  republish-plus-generation-fence, not by an explicit clear.
- **`arx` and `undec` are the two wire facts core0 reads off core1 outside
  diagnostics**, printed side by side. `arx` is every frame the transaction
  engine accepted, before admission — the count the responder-silence stale
  watch rides — and is not `acc`, which counts only the results published to
  core0. `undec` is arrivals that were not a frame: a PIO stop-bit error, a
  first byte whose frame window never completed, or a body that failed the
  marker, length, CRC or direction check. Together they are the link
  listener's instrument (`split/era_split_link.h`'s **Reconciliation**): a
  Right half alone that sees `undec` climb while `arx` stands still is hearing
  a Left half at another rate and will step; a peer booting on the power the
  cable just gave it holds the line low until its firmware claims the pin and
  produces **one** `undec` by derivation — whether the boot transient can
  produce a second is what the device item for the cold-peer plug reads off
  this field, because a second inside one dwell is a step taken in error; a
  working pair moves `undec` only on line noise. `undec` is a subset of
  `noack`, which also counts admission refusals of frames that decoded.
- **`quiet` is requests core1 answered without publishing a result**, because the
  response was a bare ACK that asks core0 for nothing but counters. Under the
  constant DUAL-HOST poll it is almost every request, and it is the field that
  says so. With no coalescing, **`prep - resv` reproduces `quiet`**; when
  `coal` moves, its first value is the additional no-result HEARTBEAT count and
  must be subtracted before using that identity.

  `coal=heartbeat/runtime_sections` is different by construction. These are
  section-bearing HEARTBEAT replies that **did cross the wire**, but whose exact
  Core0 sent-shadow work was already represented by one successful pending
  result for the same responder snapshot and actual section mask. They are bulk
  folded into the responder projection/ACK/runtime counters, so `quiet` remains
  strictly bare-ACK traffic. During the synchronous responder Apply that
  motivated the field, a healthy pressure reading is `coal` increasing while
  `full` and `noack` do not increase; `coal=0` with `full` increasing means the
  repeated responses were not eligible duplicates and the capacity problem is
  elsewhere.

  **Do not compute `resv - acc`: it is identically zero.** The reservation is
  taken only for a response that will publish
  (`era_split_communication_core_responder_service.c`), so a quiet poll reserves
  nothing and `resv == acc` on every capture.

  **A high `quiet` beside a low `acc` is the design working, not traffic being
  dropped.** The frames were received and answered — `prep` counts them, and the
  responder-silence stale watch rides core1's accepted-RX counter, incremented
  above the publish decision. What was skipped is only the core0 wake. The
  counters those responses would have moved are folded into `wire resp` and `wire
  hp host ack` in bulk at the drain's cadence, so they are at most one
  housekeeping pass stale at print time rather than exact.
- `prep`: production responder ACK/HSRSP payload prepare count / fail count.
- `last`: last accepted matrix sequence, request-kind mask, and responder
  response-section mask. Request mask bit `0x01` is heartbeat admission, bit
  `0x02` source-push admission, bit `0x04` DUAL-HOST runtime admission. **Bit
  `0x01` does not imply a HOST-PEER matrix relation**: both relations answer a
  heartbeat, and only HOST-PEER answers a matrix. Response mask follows
  `ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_*`; live core0 snapshots may
  show `00` when no response section is currently due.
- `plen/vr`: response payload length and visual-resync reason captured from the
  last prepared responder snapshot.
- `ahold=last/max`: defined under the shared clock above.

### Source-push — `wire csp`

- `wire csp`: FULL core1 HOST-PEER source-push lane line. **This lane carries the
  matrix and nothing else**, so the lane and the matrix move together. The one
  core0 enqueuer sets
  `ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_CORE0_LANE_SECTIONS`, which is
  `SECTION_MATRIX` alone and is named rather than spelled out precisely so that a
  fourth section on core0's lane fails the validator instead of quietly
  re-opening a second carrier. The matrix's own bookkeeping —
  `source_push_tx`/`source_push_ack` and the seq convergence under `hp peer_era`
  — advances for a frame that carried the matrix section, which is every frame on
  this lane, so a `csp` that runs ahead of those counts is a finding rather than
  bookkeeping.
- `pend/ready`: source-push request pending and result ready latches.
- `gen`: current source-push generation / last result generation.
- `sub`: source-push submit/accept counts.
- `full/sf`: source-push pending-or-queue full count and core start-fail count.
- `tx/res`: core1 source-push transaction count and core0 result-apply count.
- `stale/rfull`: stale source-push result ignore count and result slot full
  count.
- `ok`: OK/MISS/BAD/FAIL result counters.
- `last`: last result/request-sent/route-kind/route-reason.
- `seq`: last source-push matrix sequence associated with the result.
- `rsp`: last response kind/payload length/section byte.
- `lock/vis/rgb/news`: last lock-state valid/value, visual snapshot valid,
  RGB-state valid, and storage news valid/value flags.
- `hsrsp/visn/rgbn/newsn/secor`: cumulative HSRSP responses, the visual, RGB-state
  and news-value summaries core1 extracted from them, and the OR of observed
  HSRSP section bytes. Sticky, so an ACK response cannot erase a prior HSRSP
  observation. **All five must read zero**: this is core0's own request lane, and
  the response section set has one carrier, the standing exchange's answer
  (`era_route_contract.md`), so a non-zero reading is a section crossing on a lane
  that may not carry one — the suppression failing, not traffic observed. No apply
  counter stands beside them; the apply totals are on `wire sect app=`.
- `rxm`: last core1 initiator RX wait mode. `0` means no transaction observed;
  `1` means bounded response-window FIFO polling.
