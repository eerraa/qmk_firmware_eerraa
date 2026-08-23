# ERA Replacement Storage Contract

Status: active; in force
Genre: contract
Canonical for: portable storage domains, which relations admit the lane and
in which role, the recency layer and latest-change-wins arbitration, the
storage news value, the pull and push transactions, cross-core capacity,
durable apply, the EEPROM-clean restart requirement, and recovery
Read when: editing storage capture, payloads, Core1 storage lanes,
scheduler admission, EEPROM apply, or storage diagnostics

The `host_peer` in this document's name, in `era_host_peer_storage.[ch]`, and
in the engine's symbols is historical: it named the only relation the lane was
once admitted for. The lane is admitted for DUAL-HOST as well, on the same
engine. Renaming a proven engine is churn rather than a correction
(`era_source_map.md`), so the names stay and this paragraph is the one
statement of what they now cover.

## Scope And Authority

This contract replaces the removed V16 durable engine. It does not preserve
that engine's manifest, epoch-selection, source-election, or operation
semantics.

- Direction is per-domain **latest-change-wins** arbitration, the one
  pair-level sync semantic: a one-sided change wins that side,
  both-changed resolves by persisted divergence count then Left, and the
  winner's whole-domain content moves under the CRC/generation transfer
  authority below. There is no constant source and no constant apply target:
  an in-relation PEER takes no VIA edits, so the steady-state outcome is the
  same, while a divergent-history reopen preserves the newer content instead
  of reverting it.
- The relation's initiator initiates every request — pull, push, and the
  arbitration exchanges alike. Responder storage data or acknowledgement is
  returned only in the response slot admitted for that request. Responder
  independent send is forbidden: arbitration decides which half's content
  wins, never which half initiates.
- The lane consumes the unified relation-independent EEPROM sync requested
  bit and the EEPROM policy generation (Persisted Sync Policy,
  `era_authority_contract.md`). There is one lane with one transaction
  authority, one state, one generation set, and one diagnostic surface; a
  relation change rotates them rather than selecting between two families.
- Both halves' local requested policy — the unified EEPROM sync bit — must
  remain enabled. An initiator with the local policy disabled does not select
  storage. A responder with the local policy disabled may return
  `POLICY_CLOSED` in an admitted proof response and admits no data response.
- Transfer completion and durable apply completion are different authorities.
  A validated final chunk completes transfer only. PEER core0 alone declares
  durable apply after guarded write, read-back integrity, runtime-owner reload,
  and communication-core restart. HOST learns that completion only from a
  later PEER-initiated `COMPLETE_REQ`.
- The DUAL-HOST runtime bundle, INPUT, unrelated RGB payloads, matrix/digest/
  mirror routes, and direct HID injection remain closed. DUAL-HOST *storage*
  is open; admitting it grants none of those.

Runtime route selection, responder admission, bulk-page advertisement, and
target mutation are open and in force. Classifier recognition of an identifier
is not execution authority (`era_closed_surface_contract.md`).

## Relation Admission

Which relations run the lane, and in which role. This is the engine's whole
relation coupling: three helpers in `era_host_peer_storage.c` consult the
mode, and everything below them is generation-based and relation-neutral. A
relation that gains storage service is added there and nowhere else — a
fourth place would mean the neutrality claim was false, and the discrepancy
would be the finding.

| Relation | Initiator | Responder | Confirming peer fact |
| --- | --- | --- | --- |
| `HOST_PEER_PEER` | this half | — | peer is host-open |
| `HOST_PEER_HOST` | — | this half | peer is no-host |
| `DUAL_HOST_LEFT` | this half | — | peer is host-open |
| `DUAL_HOST_RIGHT` | — | this half | peer is host-open |

In DUAL-HOST both halves are host-open, so the peer fact cannot separate the
roles and the physical side does — which the mode value already carries, and
which matches the wire ownership `era_invariants.md` fixes (Left initiator,
Right responder). Admission additionally cross-checks the settled wire role,
so a mode disagreeing with it admits neither side.

The initiator arm alone consults the local requested policy: a policy-off
responder still pins and answers `POLICY_CLOSED` in its admitted slot.

**A half with no initiator role holds no initiator work.** The
cell queues, the deferred slot, the armed summary and the round scope are
drained by the initiator alone, and a responder selects no storage route — so
a half that queued work as a HOST-PEER PEER and then became a DUAL-HOST
responder would hold a probe mask and a conflict cell nothing could execute,
and would keep arming more, because a settled capture is role-independent while
the token it raises is not. Entering the responder role releases them.

Nothing is lost, and the reason is the round-validity rule rather than
bookkeeping: every route from responder back to initiator is a mode change,
which rotates the relation and opens a fresh verify-all audit, and that audit
re-derives every domain's direction from both halves' *current* facts. A
remembered queue would instead execute a decision taken in a relation that no
longer exists. What stays is what is a fact rather than queued work — the
storage news value, whose maintenance is role-independent; the per-domain
failure backoff, which persists across relation events by contract; and the
delta full-fetch repair mark.

**Why DUAL-HOST needs the lane at all** is not a measured defect and does not
have one. Without it every portable domain exists in DUAL-HOST as two copies
with nothing keeping them equal while the user's configuration is
single-valued: each half serves its own rows from its own copy, and the exit to
HOST-PEER runs a per-domain whole-image arbitration that must discard one side.
That follows from the admission gate plus per-domain arbitration and needs no
device run.

### DUAL-HOST Convergence

Two properties bind the DUAL-HOST arm specifically. Neither is a nicety.

**Steady-state silence.** A DUAL-HOST window with no settled config change
carries no storage transaction, and costs no measurable scan rate. DUAL-HOST
exists to keep the split wire out of the input path — two direct USB paths
instead of one plus a wire hop — so periodic storage traffic would spend the
thing the mode was built to protect. This is why the design is a hint rather
than a polling cadence, and why the lane's initiation is change-triggered in
every relation (`era_route_contract.md`). A domain that *cannot* converge — a
policy-closed responder is the reachable case — closes on the terminal refusal
and arms nothing, so the window stays silent.

**One hint mechanism, both relations.** The responder advertises its storage
*news value* in the response slots the initiator already opens, and the
initiator arms a whole-family summary on any change of it. There is no
per-relation split: DUAL-HOST does not carry a one-bit level on
`SESSION_STATUS`, and no core0 poll exists to read one
(`era_route_contract.md`). The value's own contract — forward-only, stepped per
settled capture, never retired — is in Storage News Value And Relation-Open
Audit below, and it is the same in both relations.

**A policy enable is news by construction, and needs no explicit re-arm.** The
responder's advertisement is gated on its own EEPROM policy, so while the
policy is closed it advertises `0` whatever that half holds, and an initiator
edit sits refused with nothing to re-trigger it. What closes that hole is the
news value's own rule: **departed settles step the value whether the policy is
open or closed**, because the policy gates the advertisement and never the
local capture (Source Revision And Identity below), so on the enable edge the
value differs from the `0` the peer last saw and the ordinary news test arms
the summary.

**The edge is news only if that half has ever settled a departure.** Since the
at-agreement narrowing, a half whose settles all sat on its
baselines still advertises `0`, so an enable edge over a parked initiator edit
carries no news and that edit waits for the next relation event or this half's
next departed settle — a bounded delay, never a loss, and the same state a
responder with no settles at all always produced. Whether that boundary
reopens the slow-retry refusal below is an open owner question.

**The initiator does not gate acceptance on its own EEPROM policy.** A hint is
a probe *scheduling* input that bypasses no selection gate, so a half with the
policy off never selects the domain the summary would queue anyway. Gating
acceptance instead would make the arriving change unobservable and lose the
claim for good across a policy toggle, because the wire value does not move
when the reader's policy does.

> **REFUSED:** give the terminal refusal one slow retry.
> **WHY:** it would work, and it reintroduces the polling shape this design
> exists to refuse — in a state a user created deliberately.
> **REOPENS:** a policy edge that leaves an initiator edit refused with
> nothing to re-trigger it, which is the hole the news value's step closes
> above.

**Convergence must outrun a second edit.** The window from settled edit to
convergence must be short enough that a second edit cannot land inside it:
settle is `ERA_HOST_PEER_STORAGE_DIRTY_QUIET_MS` (1000 ms) and the hint rides
the relation's own lane, so convergence begins about 1 s after settle, against
a human switching VIA from one device to the other. This matters because **in
DUAL-HOST a two-sided changed state almost always means propagation lag, not
divergent histories**: one user at one PC edits sequentially, because VIA
attaches to one device at a time, and the count-then-Left rule applied to a lag
would discard an edit made seconds ago. The rule is right; applying it to a lag
is the error, and the window is what keeps it from being applied to one.

The conflict cell is kept, not removed: it still fires for genuinely apart
histories — a half powered down across an edit on the other, a reboot in the
gap — which is what it was designed for and the only correct answer there. The
window makes it rare; it does not make it wrong.

## Current Storage Inventory

The Tomak79H VIA build has a 24 KiB logical wear-level EEPROM. The replacement
protocol synchronizes semantic owner ranges, never wear-level backing bytes.

**Schema 1 is geometry-parameterised** (owner decision 2026-08-13): the
dynamic-keymap image is `4 layers × MATRIX_ROWS × MATRIX_COLS × 2 bytes` on
both cores, and the macro base follows it, so the formula is the schema and
the resolved numbers are the board's. Every other address and size below is
geometry-independent and resolves identically on every ERA board. What makes
per-board resolution sound is the identical-image rule plus the wire's own
identity check: both halves of a pair always run one image, so no peer can
disagree about a size, and a `PROBE`/`PROOF` size or schema mismatch is
refused before any byte moves. The decision's stated reason is that future
split boards inherit this logic rather than re-deriving it.

On the 12×9 boards (`tomak79h`, `tomak79s`) the formulas resolve to ERA
config `37..212`, QMK default layer `3`, keymap config `4..5`, RGB Matrix
`23..30`, VIA layout options `296`, dynamic keymap `297..1160`, and dynamic
macro `1161..17544` — every number this document's recorded figures were
measured against, unchanged. On 12×11 `sirind/tomak` the keymap image is
1,056 bytes at the same base and the macro base is `1353`. Compile-time
assertions still close the geometry-independent anchors, the store sizing,
and the layer count (the schema layer literal is bound to
`DYNAMIC_KEYMAP_LAYER_COUNT` where core0 can see both); geometry-driven
movement is the design, and any other movement still fails the build.

| Domain | Id | Owner/source range | Schema | Image bytes | Target reload |
| --- | ---: | --- | ---: | ---: | --- |
| `ERA_CONFIG` | 0 | `ERA_EEPROM_CONFIG_ADDR + 0 .. +175` | 1 | 176 | Tomak keyboard config plus ERA common feature reload |
| `DYNAMIC_KEYMAP` | 1 | `DYNAMIC_KEYMAP_EEPROM_ADDR ..`; 4 layers x MATRIX_ROWS x MATRIX_COLS x 2 bytes | 1 | geometry (864 on 12×9) | none; QMK reads the NVM keycode image directly |
| `DYNAMIC_MACRO` | 2 | `DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR .. +16383` | 1 | 16384 | none; QMK reads the NVM macro image directly |
| `QMK_RGB_MATRIX` | 3 | `(uintptr_t)EECONFIG_RGB_MATRIX .. +7`; `rgb_config_t` | 1 | 8 | silent disable/read/validated-enable reload |
| `QMK_KEYMAP_CONFIG` | 4 | `(uintptr_t)EECONFIG_KEYMAP .. +1`; `keymap_config_t` | 1 | 2 | reload `keymap_config` from EEPROM |
| `QMK_DEFAULT_LAYER` | 5 | `(uintptr_t)EECONFIG_DEFAULT_LAYER` | 1 | 1 | read and apply `default_layer_state` |
| `VIA_LAYOUT_OPTIONS` | 6 | `VIA_EEPROM_LAYOUT_OPTIONS_ADDR`; exactly one byte | 1 | 1 | apply through VIA layout-options owner |

The domain table is a build contract. A geometry difference is schema 1's own
formula resolving on a different board and is not a schema change; changing the
formula or a geometry-independent size still requires a new schema and
simultaneous source, wire, and contract review. Schema mismatch, unknown
domain, or size mismatch rejects the whole domain before any target write.
There is no compatible-prefix or partial-domain apply.

**What the schema pins is the domain's wire-visible shape — its identity and
its size — and not the owner map inside it**, and the two are separated here
because the sentence above once ran them together. The schema byte crosses the
wire so that two halves cannot transfer a domain they disagree about; both
halves of a pair always run one identical image, so they cannot disagree about
an interior offset either, and bumping the schema for a rearrangement would put
a second value in a per-domain field that has one. What a rearrangement does
owe is the *local* migration, and that is `ERA_EEPROM_RESET_KEY`'s
(`storage/era_eeprom_layout.h`), whose bump is what stops an older block being
read under the newer map.

`ERA_CONFIG` schema 1 is the active syncable range exactly, and the range is
`storage/era_eeprom_layout.h`'s — grouped by meaning, each region ending where
the next begins, every boundary asserted there:

- input behaviour — Tap Dance `0..87`, tapping `88..99`, mouse keys `100..115`,
  debounce `116..123`, SOCD `124..139` (LR `124..131`, UD `132..139`), KKUK
  `140..143`;
- lighting — backlight effect `144..147`, RGB indicator `148..155`;
- the board's own config `156..163`;
- reserved zero `164..175`.

The contiguous 176-byte image keeps owner offsets stable and allows a single
prevalidated apply. Reserved bytes are part of schema 1 and must be zero, and
there is one reserved range rather than the three this list carried before
2026-08-18 — `era_host_peer_storage.c` walks it in one loop.

## Excluded Local State

The following are never members of a HOST-PEER portable image:

- QMK EEPROM magic and debug/storage identity;
- `EECONFIG_HANDEDNESS`, USB identity, accepted local authority, session
  facts, and side-specific hardware facts;
- VIA build-date magic;
- the ERA local sync-policy range `176..199`, including the unified requested
  flags, both the flags and EEPROM policy generations, and the divergence
  counters in its counter bytes (Recency Layer below);
- ERA protected/reset range `176..255` whole, including the reset guard, epoch
  metadata, and the split link's selected rate at `200..203`;
- wear-level logical-to-backing metadata, write profile state, and backing
  flash bytes;
- owner epoch, relation/request generations, local dirty counters,
  diagnostics, retry state, and transaction state;
- unused QMK eeconfig fields, including local connection state and legacy
  keyboard/user words not owned by an active Tomak79H feature.

The protected range `220..251` is the recency baseline record (Recency Layer
below). It is never a member of a portable image either: baselines are each
half's private memory of the last agreement, and syncing them would destroy
exactly the divergence they exist to detect. Nothing may be reserved in this
range without a real reader — the record satisfies that rule because the
arbitration consumes it at every relation-open sweep and the recency
diagnostic line reads it. A strict reset zeroes the range, which reads as an
invalid guard and degrades conservatively. The boundary asserts remain.

## Recency Layer

The persisted fact the arbitration above consumes: each half privately
remembers, per domain, the last converged content and how many settled edits
it has made since. `changed = current CRC ≠ own baseline` is the fact this
layer exists to answer, and it is what every cell selection reads — it turns
"the halves differ" into "who departed from the last agreement", each half
judging itself locally.

Persisted homes, both sync-excluded, both outside every domain range so a
recency write can never re-dirty a domain, and both zeroed by a strict
reset:

- **Baseline record** at ERA config `220..251`, exactly 32 bytes:
  `[0..27]` seven per-domain CRC32 values LE by domain id, `[28..31]` a
  guard — CRC32 over `[0..27]` XOR the layout version constant
  (`ERA_HOST_PEER_STORAGE_BASELINE_GUARD_XOR`). A guard mismatch (including
  the all-zero range a strict reset leaves) invalidates every baseline and
  degrades every domain conservatively to changed — toward the conflict
  cell, never toward a silent winner.
- **Divergence counters** in the sync-policy block's counter bytes
  (`+10..+23`, seven 16-bit LE saturating counters by domain id; the layout
  and version claim are in `era_authority_contract.md`, Persisted Sync
  Policy). The counters have no guard of their own: integrity authority is
  the guarded baselines, and a torn counter costs at most one tie-break of
  the conflict cell, which the count-then-Left rule absorbs.

Update rules, all at the cold task boundary, all write-on-change-only so an
idle boot writes nothing and a MATCH re-proof of an unchanged agreement
writes nothing:

- **Settled capture** (after the trailing quiet interval): content equal to
  a valid baseline resets the domain's counter to zero — an edit-and-revert
  dissolves its own divergence — and anything else increments it, saturating
  at the 16-bit maximum. Quiet coalescing makes one settled capture the
  natural "one change" unit. An invalid baseline record still counts edits;
  convergence is what rebuilds the record.
- **Convergence close** (a `MATCH` re-proof or a durable `COMPLETE`, on both
  roles): the closing content's CRC becomes the domain's baseline and its
  counter resets to zero. This is the only baseline writer.
- **Boot capture**: the increment arm must not run — a reboot loop would
  count phantom edits. Exactly two idempotent repairs are permitted: content
  equal to a valid baseline clears a stale counter, and a divergence whose
  settling was lost to a power cut inside the quiet interval (changed
  content with counter zero under a valid record) is bumped to one so the
  conflict cell still sees that work.

The persisted baselines and counters have no RAM copy: readers and writers
run at the cold boundary and read through the wear-level cache. What the
layer does keep in RAM is one derived byte — the changed-vs-baseline domain
mask, written only from `era_host_peer_storage_note_changed_shadow()`
(`split/era_host_peer_storage.c`) beside the record reads that already
happen, and never from the lamp path, which only loads it. It costs four
bytes of the core0 state budget and no more, and the account of which sites
write it belongs to the indicator's local arm in **Diagnostics** below. The
diagnostic view of the persisted side is the `wire storage recency` line
(`era_capture_reading.md`).

The responder answers `SYNC_STATUS` from the recency seat inside its
published metadata snapshot, which is built at the publish boundary — core1
never reads EEPROM. **A settled capture therefore marks that snapshot
dirty**, so the seat a summary is answered from is never older than this
half's own last settle. Without it the responder reports a recency mask that
predates its own change and the initiator classifies a domain from facts that
half has already superseded; with it the republished generation makes the
stale answer fail the drain's identity gate instead.

## Arbitration

One rule covers every cell, in-session and at reopen. Each half evaluates
`changed = current CRC ≠ own baseline` locally; the flag pair selects the
cell, and only the conflict cell needs ordering information:

- **(✗,✗)** — verify: a probe episode closes `MATCH`.
- **responder changed only** — pull: the probe's `TRANSFER` runs the
  existing pull transaction.
- **initiator changed only** — push: the push transaction below.
- **(✓,✓)** — conflict: the per-domain counter exchange runs first; the
  larger divergence count wins, tie to Left, and the winner's direction is
  then queued as a push (initiator won) or a pull (responder won).

**Tie to Left is this firmware's one answer to "two halves disagree and nothing
else decides", not this engine's private one.** The split link's agreement
breaks its own tie the same way and says so at the rule
(`split/era_split_restart_agreement.h`), and Left is the side that already
initiates DUAL-HOST and the peer-unknown bootstrap and owns the low matrix rows.
A lane that meets this situation and answers differently is adding a second
convention for one question.

**"Latest-change-wins" names the one-sided cells, not the conflict cell.**
Three of the four cells are decided by the changed-flag pair with no
ordering information at all, and those are the ones the name describes. The
conflict cell is most-*changes*-wins: it compares counts, so a half with
several older settled edits beats a half with one recent edit. That is the
decided rule rather than an approximation of a temporal one; ordering the two
halves' edits by a shared clock was evaluated for this cell and rejected.
**That rejection's reason is in no record** — not here, and not in the commit
that wrote this sentence — so it stands as a decision rather than a
derivation, and a session going to look for the argument will not find one.
Reading the name as recency is the misconception this paragraph exists to
stop, because the conflict cell is exactly where a reader would expect recency
to apply.

**The summary exchange.** Every relation (re)establishment opens with one
whole-family `SYNC_STATUS` exchange before any domain episode: the
initiator sends its changed mask and baseline validity, the admitted
response returns the responder's. Because a convergence close writes the
same winner CRC as the baseline on both halves, converged baselines are
identical — so this single exchange classifies all seven domains into the
cells above, and the former blanket always-pull sweep becomes the
cell-shaped drain. Every domain lands in exactly one queue, preserving the
sweep's bounded completion; the drain stays ascending by domain id at the
storage retry cadence, conflict exchanges first within a domain.

**The same exchange runs in session, with one scope difference.** The
relation-open summary is *verify-all*: every domain neither half declares
changed still lands in the probe queue, because proving all seven is what
the mandatory sweep is for. An in-session summary classifies only what the
two halves declare changed — the verify cell is skipped, since the halves
agreed at their last convergence close and nothing has said otherwise.
Without that split one settled edit would cost six `MATCH` episodes to move
one domain, which is a change-triggered signal behaving like a poll the
moment it fires. The scope travels with the armed token, so a
relation-open summary can never be downgraded by an in-session arm landing
on top of it.

The relation-open signature is therefore `open=8 close=8` on a converged
pair **on the initiator**, and `open=7 close=7` on the responder: the
summary is an initiator episode, and the responder answers it inside an
admitted response slot without opening one of its own. Reading the
initiator's 8 as the pair's number is how a healthy responder gets called a
regression (`era_capture_reading.md`).

**Conservative degradation.** An invalid baseline record reads as
all-changed. Which cell that lands in depends on the *other* half, and
predicting the wrong one has already produced a wrong expectation: only when
**both** records are invalid does `mine & peer` cover all seven and put every
domain in the conflict cell. One invalid record against a converged half gives
`mine & peer == 0`, so every domain lands one-sidedly in the degraded half's
favour — device-shown, where an EEPROM CLEAN on the HOST propagated its
post-clean defaults to the PEER through the pull cell, not the conflict cell.

That is not a silent winner, because the cell chooses a *direction* and the
probe still decides whether anything moves: an agreeing pair closes `MATCH`
and transfers nothing however it was classified. The case that could lose
work — a degraded record on one half while the other holds genuinely newer
content — sets both changed flags and reaches the conflict cell, where the
degraded half's counters are zero and the half with real edits wins. Fresh
counters resolve 0-vs-0 to Left, and a push or pull whose contents already
match short-circuits to a `MATCH` close that heals the baselines.

**In-session carriers.** Two signals arm a summary, and everything the
summary decides follows from both halves' declared masks rather than from
one half's:

- **this half's own settled capture while in relation, when it departed
  from the last agreement** — a direction has to be recomputed. An
  at-agreement settle arms nothing and steps nothing (Storage News Value
  And Relation-Open Audit below);
- **a responder-changed signal**, carried in both relations as the HSRSP
  storage news section.

Both arm the *same* summary; neither queues a direction of its own. Queueing a
push straight off this half's settled capture uses only this half's facts, so a
responder edit that had already settled is overwritten by a push granted before
that responder's signal arrives — the exact loss DUAL-HOST storage exists to
prevent, and one compact exchange against a 1000 ms settle is not a cost worth
racing for. The abort path obeys the same rule: **every direction decision is
derived from both halves' facts, and there is no path that queues one from a
single half's.**

The arbitration exchanges are nonexclusive proof-class episodes: they pin
nothing and cannot bypass quiet deadlines, generation allocation, or route
priority.

**A decision is valid only for the round that produced it.** This is the
rule the whole semantic rests on, and it is stated here because breaking it
is how the engine loses an edit. An episode that closes successfully
consumes its decision. An episode that does **not** close returns the
domain to *needs arbitration* — a fresh summary — and never to a
remembered direction. The three cell masks are the output of one
classification, consumed or discarded, never repaired in place.

> **REFUSED:** re-arm an aborted episode's own direction and let a later
> signal from the peer correct it.
> **WHY:** the repair depends on a second event arriving, and when none does
> — a policy-closed responder, or one that makes no further edit — the
> remembered direction executes unchecked; recovered from the episode state
> machine, which the abort path overwrites, every aborted push turns into a
> **pull** and destroys the initiator's edit, the exact loss DUAL-HOST storage
> exists to prevent.
> **REOPENS:** permanent while a fresh summary needs no second event and
> costs one compact exchange against a failure backoff.

**A terminal refusal is not retried.** `POLICY_CLOSED`,
`UNSUPPORTED_DOMAIN`, `UNSUPPORTED_SCHEMA` and `SIZE_MISMATCH` cannot change
without an event that would itself re-trigger the domain, so the episode
closes and arms nothing; re-arming them only spins. `INTEGRITY_FAIL` is
deliberately not in that set: it retries with hints disabled, which is a
repair rather than a refusal.

**Races converge, never overwrite.** A push opened against a responder
whose domain is inside its trailing quiet interval pins invalid and
answers `SOURCE_CHANGED`, exactly like the pull probe; a responder-side
write during a push episode aborts it the same way. Both return through a
summary that reclassifies from both halves' current facts, so simultaneous
edits cost bounded retries, not either side's content.

## Source Revision And Identity

Each domain has a cached core0 manifest entry containing domain id, schema,
exact size, full-image CRC32, nonzero source revision, dirty generation, and
validity. Capture occurs only at init, after the trailing dirty quiet deadline,
or for a clean domain selected by an explicit cold storage task request. A
request can never bypass a pending dirty quiet deadline.

Initialization captures the seven domains once, reading and CRC-visiting
`176 + keymap + 16384 + 8 + 2 + 1 + 1` bytes in total — `17436` on the 12×9
boards, `17628` on 12×11 `sirind/tomak`. This is boot
work, not a scheduler-loop period. There is no periodic idle-proof patrol and
no idle due token: with no local EEPROM write, a captured domain stays
quiescent and triggers no repeated EEPROM reads, CRCs, or source comparison.
**The EEPROM sync policy bit gates none of this local half.** It decides
whether a cross-half episode opens (on the initiator's arm) and whether the
news value is advertised (on the responder's), and it carries no term
on the local write, on the dirty note, or on the 1000 ms capture below.
Turning EEPROM SYNC off leaves a VIA save's flash episode and its core0 outage
byte-identical — the bit is a statement about the wire, never about the main
loop.

Read with it: a VIA lighting *change* runs no EEPROM setter of its own (every
continuous control's setter is a `_noeeprom` variant; the two toggles that do
write at the set are named in `era_board_adoption.md`) but still reaches flash,
because **the VIA application pairs `id_custom_save` with every
`id_custom_set_value`** — there is no user-initiated save. That save
*schedules* the write `ERA_STORAGE_QUIET_DEFER_MS` after the last one rather
than performing it (`era_source_map.md`), so the defer converts the
application's save storm into one flash episode per pause of 500 ms or longer.
For a device leg this means **lighting is not a flash-free control**: an arm
whose control is "no Save pressed" has excluded nothing, and only closing VIA
does. The policy channel is the inverse case — its `id_custom_save` is a no-op
and the *set* persists the policy block synchronously — so turning a sync bit
off is itself a write and owes its own dirty-quiet interval before an arm
starts.

An actual local write may recapture only its overlapping domain after the
1000 ms trailing quiet deadline even while the cable is absent. Every further
write to that domain replaces the deadline and advances its dirty generation;
there is no revision history queue. Thus intermediate keymap edits are
coalesced into the latest whole-domain image. Edits less than 1000 ms apart do
not each trigger a whole keymap-image transfer; one capture and, if needed, one
whole-image transfer (four chunks on a 12×9 board) becomes eligible after the
final edit settles.

Time-based storage work is serviced only at the scheduler's existing cold task
boundary, not by a separate per-loop poll. The always-present 10 ms authority
poll bounds this service cadence, and a compile-time assertion keeps that
period no longer than the shortest 25 ms storage retry. Dirty-quiet,
audit-sweep, retry, and episode predicates may therefore become observable at
most one cold tick after their exact deadline; this bounded latency is part of
the replacement-storage contract. A local EEPROM dirty producer updates only
the cached dirty generation and quiet deadline. It does not start a scan-bound
classifier or construct a separate scheduler deadline.

Core1 result publication remains asynchronous. The cold runtime owner
publishes one cached result-watch fact to the scheduler while an eligible
HOST-PEER relation or an old transaction needing cleanup exists. A quiescent
no-relation fast gate reads only that false fact and does not read the two
Core1 result-ready generations. While the watch is true, a nonzero aligned
generation wakes the cold task and still requires the complete
generation-matched drain. Revision-wrap recovery suppresses the published
watch without discarding the internal cleanup obligation. A failed
revision-wrap relation rotation is retried no sooner than the fixed 25 ms
storage retry deadline; the cold service cadence — bounded above by the
always-present 10 ms authority poll, and able to run sooner when other work
marks the task due — does not shorten that backoff.

- A source revision is a nonzero 32-bit core0 capture generation. It changes
  whenever a new immutable image is captured, even when content later returns
  to an earlier CRC.
- CRC32 is the image integrity/content identity; source revision is the
  transaction freshness identity. They are not interchangeable.
- The active identity is owner epoch, relation generation, request generation,
  EEPROM policy generation, storage transaction generation, domain id,
  schema, chunk id when applicable, source revision, image size, and image
  CRC32.
- Cross-core request/result apply requires every locally relevant field to
  match, including the current responder-snapshot generation on HOST. Wire
  records carry storage transaction generation, domain, schema, source
  revision, chunk id, and CRC where the operation needs it; owner, relation,
  request, snapshot, and policy generations remain local cross-core authority.
- A local write to the active HOST source domain invalidates the published
  responder snapshot immediately. No later success response may use the old
  source revision. The immutable bytes remain frozen until close/abort so a
  concurrent Core1 copy cannot tear.
- If a requested HOST domain is still inside its trailing quiet interval, the
  one unpinned `BUSY` handoff pins an invalid source fact instead of capturing
  early. The retried proof returns terminal `SOURCE_CHANGED`; PEER closes that
  nonexclusive episode and schedules the same domain after another 1000 ms.
- If the HOST source becomes invalid after `TRANSFER`, HOST returns an admitted
  `ABORT_RSP/SOURCE_CHANGED` in place of the next `CHUNK_RSP`. Invalidation
  detected while copying the chunk is converted to the same compact response
  before transmit. It is a successful wire response and never a storage
  timeout. `APPLY_RSP/SOURCE_CHANGED` has the same terminal supersede meaning.
- A local write to the active PEER target domain aborts the target transaction
  rather than overwriting the newer local state.
- Owner/relation/policy change, role change, reset, cable loss, or source
  invalidation cancels the active transaction. A new relation always allocates
  a new nonzero storage transaction generation.
- A 16-bit storage transaction generation is never reused inside one relation.
  Reaching `UINT16_MAX` closes and flushes storage, forces relation-generation
  replacement, and only then permits generation 1. The same relation-rotation
  rule applies before a 16-bit request or responder-snapshot generation is
  reused. A 32-bit source-revision wrap likewise rotates the relation and
  invalidates every cached manifest before revision 1 may be published. Zero
  is always invalid. This prevents a delayed frame from becoming current
  solely because a compact or local handoff generation wrapped.

## Storage News Value And Relation-Open Audit

There is no periodic storage idle-proof patrol. In its place stands a two-layer
sync-acceleration structure: a hint that says *ask*, and a mandatory audit at
every relation open. The probe/proof/chunk/apply/complete machinery, generation
binding, and CRC content authority are the unchanged transaction authority
underneath both.

- **Every half keeps a RAM-only storage news value: a forward-only counter,
  `1..127`, stepped once per settled capture that departs from the persisted
  agreement, with `0` meaning nothing to claim.** It is stepped exactly when a
  capture completes after the trailing quiet interval with content off its own
  valid baseline, never during a write burst — a dirty domain inside its quiet
  interval has no settled capture, which preserves latest-state coalescing.
  **A settle at the agreement steps nothing and arms nothing, in either
  role**: the changed mask it would contribute to any summary is zero by the
  same comparison at the same cold boundary, so the round a step would buy is
  decided before it runs (device-read on a matched pair, both edit
  directions — one summary exchange and ~0.17 s of lamp per identical-content
  reload, buying nothing). An invalid or torn baseline record reads as
  departed and still steps, so degradation errs toward signalling; and the
  suppression consumes no peer fact, which is what keeps it out of the
  periodic-refresh refusal's WHY below. Maintaining the value is
  role-independent because a settled capture is:
  which half is the responder decides only whether the value reaches the wire,
  never whether it is kept. It has no boot arming, no retirement and no
  re-arm.

  **It is a counter and not a per-domain mask, because a level has to be able
  to fall and this one cannot be made to.** A mask's clear is derived — a bit
  comes down when its domain converges — which owes a wire form for the fall, a
  receiver that can hold an absence, and a guard for the case where a domain's
  content moves again before its bit can. That last one is unreachable: a
  domain proven while this half already holds newer content keeps its bit
  raised, the advertised value never moves, and the peer hears nothing more.
  **Stepping per capture rather than per domain closes it** — a second edit of
  a domain already claimed moves the value even though the domain set has not,
  while a second edit that returns the domain to the agreement leaves nothing
  to hear and steps nothing.
  The per-domain identity a mask would carry is not load-bearing: the
  `SYNC_STATUS` summary (Arbitration above) already carries per-domain change
  derived from the durable baseline, self-expressing, reboot-surviving, and
  carrying direction with it. The hint's whole job is to say *ask*.
- The value is advertised as the `HOST_PEER_HOST_SOURCE_RSP` storage news
  section, and **it has one carrier: the standing exchange's answer.** A
  source-push answer carries no section. The rule and its generalization are
  canonical in `era_route_contract.md`'s **One carrier for the response section
  set**.

  **It is latest-state and edge-armed, and the all-clear is a value** rather
  than an omitted section. A level-triggered form — repeated while nonzero,
  omitted while zero — is incompatible with this carrier, which reports edges
  and caches the last value, and a cache cannot hold an absence. The carrier
  discipline and the forced refresh that covers a lost response are canonical
  in `era_wire_contract.md`, which also carries the standing claim that **no
  behaviour depends on the advertised value being correct**: a peer that lies
  about it costs one bounded summary exchange per lie, the same cost a truthful
  one pays.

  **A responder whose local EEPROM sync requested policy is disabled still
  answers the section** rather than withholding it (`era_wire_contract.md` for
  the value it sends). That retires the peer's held claim instead of stranding
  it, which is the policy gate reaching the wire as a transition.
- **Nothing retires the news value, in either role.** There is no
  convergence-close retirement and no guard standing on one: clearing a claim
  out from under a newer local write is a data-loss defect, and it is a hazard
  only a *falling* carrier has. A convergence close writes the domain's recency
  baseline, zeroes its divergence counter, and does nothing else — in
  particular it retires no per-domain claim, because there is none to retire.

  **The section therefore re-crosses once per refresh period for the life of a
  relation**, from its first settled capture onward: the refresh gate reads
  "has ever settled" rather than "unconverged". It is bounded and invisible
  downstream — the initiator discards a value equal to the one it holds, and
  the section has no `rt` arm — and it costs one responder snapshot republish
  per period.

  > **REFUSED:** narrow the refresh so it stops repeating once a relation has
  > converged.
  > **WHY:** the responder cannot observe what the initiator took, so every
  > cheap narrowing trades that bounded repeat for an unbounded lost config
  > edit.
  > **REOPENS:** a device reading that shows the repeat costing something, or
  > a responder-side view of what the initiator consumed.
- **The initiator consumes the value as news and arms a whole-family summary.**
  It selects no domain, so it cannot bypass quiet deadlines, the `BUSY`
  pin handoff, generation allocation, storage selection gates or route
  priority, and it never makes storage exclusive by itself. The summary then
  decides directions from both halves' current changed masks, exactly as a
  local settled capture's summary does.

  **Every direction decision goes through one funnel,
  `arm_summary_refresh()` (`split/era_host_peer_storage.c`), and nothing may bypass it.** Three bypasses existed
  and each one cost an edit: queueing a push straight off this half's own
  settled capture used one half's facts; an aborted episode re-armed the
  direction it had been given; and writing `probe_pending_mask` from the peer's
  hint queued a direction from the peer's claim about itself with no reading of
  what this half holds, so a hint arriving while this half had also edited
  executed as a pull instead of reaching the conflict cell. A hint that names a
  domain is the shape of all three.

  What the initiator remembers is one byte: the last value it acted on. An
  advertisement equal to it arms nothing, whichever carrier repeated it; `0`
  arms nothing because an absence of news is not work.
- **The consumer is idempotent: an advertised value equal to the last one it
  took schedules nothing.** This is a requirement rather than an
  optimisation, because this consumer *queues work* while its neighbours on
  the same delivery path are guarded setters. A carrier legitimately repeats a
  value — a responder answering many polls from one published snapshot, an
  initiator replaying a cached byte on every edge of the record that holds it —
  so a repeat must cost nothing. Without the guard each replay arms another
  probe; the signature is `open`/`match` an order of magnitude above what the
  contract asks for, with no failure counter moving at all.
- A PEER episode that aborts re-arms its domain's pending probe bit, so an
  aborted domain stays inside the mandatory seven-domain sweep instead of
  dropping out until the next relation event. Its retry is paced by the
  per-domain failure backoff. A domain parked in the deferred `SOURCE_CHANGED`
  slot is owned by that slot alone and carries no pending bit.
- On every relation (re)establishment, the initiator runs the mandatory
  relation-open arbitration (the Arbitration section above): one summary
  exchange, then the cell-shaped seven-domain drain in ascending domain id
  order at the 25 ms storage retry cadence with bounded completion. The
  initiator holds that arm until `era_split_link_runtime_settled()`
  (`split/era_split_link.h`) so the first EEPROM SYNC runs at the session's
  target baud, not during the Low meet; the link byte does not enter this
  engine. A generation mismatch seen while the raise is still owed is left
  pending and fires when the predicate becomes true. The link raise waits
  only on live pair work (`era_host_peer_storage_restart_should_wait()` in
  `split/era_host_peer_storage.c`), not on the boot changed-shadow the
  audit is what clears: waiting on that shadow deadlocks a CLEAN, because
  the shadow is the lamp and the audit cannot start until the raise
  finishes. Every
  legitimate divergence path (promotion use, either-side reboot or power
  loss, cable loss) rotates the relation and is caught there; the news value
  covers responder-side settled changes inside a live session, and the
  initiator's own settled captures are the push signal.
- The periodic idle patrol is removed entirely. The residual class that
  leaves — in-session silent corruption and hint-implementation defects,
  detected only at the next relation event — is accepted deliberately, and is
  listed with the other accepted costs under **What The Lane Costs A Typist**.

### Why An EEPROM Clean Is An Agreed Restart

The VIA EEPROM CLEAN command invalidates the store and then reboots, on **both**
halves at one instant. Three separate things make that shape necessary, and each
would be reintroduced by dropping one of them.

**The restart is required by *this* contract, not by local state.** Local core0
state does not need it: every item `eeconfig_init_quantum()` (`quantum/eeconfig.c`)
and `eeconfig_init_kb()` re-initialize either has a runtime re-apply path or has
no RAM shadow, and QMK itself ships a no-reboot arm of the same command under
`NO_RESET`.

Split convergence does need it, and **the reason is the relation rotation**. A
clean leaves two domains with no changed bytes and therefore no dirty note —
`DYNAMIC_MACRO`, whose image was already zero, and `VIA_LAYOUT_OPTIONS`, which
returns to a default it may already have held. This half's news value does not
step for either, so nothing in the live relation asks about them; the peer keeps
its pre-clean content and the two halves silently disagree on exactly the
domains the operator believed they had just reset. Only a relation
(re)establishment runs the verify-all sweep that compares all seven regardless
of what either half declares, and the restart is what produces one.
`era_host_peer_storage_init()` (`split/era_host_peer_storage.c`) rebuilding the seven manifests is the other half
of it, and it early-returns once initialized.

Removing that early return to obtain a no-reboot clean is therefore not a
simplification. It would need its own change and its own device evidence; the
restart reaches the same converged state through a path that is already proven.

> **REFUSED:** a pre-clean campaign (write defaults while connected, then
> restart into an ordinary boot)
> **WHY:** it trades this proven restart path for ~0.3 s of instrument-blind
> runtime outage and an unknown app-timeout, to save one erase
> **REOPENS:** device evidence that the restart's own write burst breaks a
> host-side contract the raw-HID silence gate does not cover

**Both halves erase, because the protected range never travels.** The
convergence above covers all seven domains — the keymap, the macros and the
`ERA_CONFIG` range at `0..175` included: a clean on the HOST propagates its
post-clean defaults to the PEER through the pull cell, which is device-shown and
is the **Arbitration** section's degradation case. **The protected range
`176..255` is not a domain and crosses on no lane at all**, so before this the
two halves kept different sync toggles — untidy — and different **link levels**,
which a clean on one half cannot put right: the pair's target is the
winner's stored level (`era_route_contract.md`), so a one-half clean is
overwritten by the other half's stored level on the next connection and the
pair is not at defaults. The clean therefore rides
`split/era_split_restart_agreement.[ch]`, which resets both halves at one
instant with both stores invalidated (`era_wire_contract.md` for the wire).

**Both records invalid moves every domain from the pull cell to the conflict
cell**, and that is the one thing about the existing convergence this changes.
There the counters tie at zero, Left wins, and the winner's direction is queued —
but both halves already hold the same defaults, so the transfer short-circuits to
a `MATCH` close and nothing moves. **No content behaviour changes, only which
half also erased**; the path to it is different and the outcome is not.

**The clean is the agreement's first user, and its failure mode is why.** The
agreement is a general mechanism and was built inside the feature with the
highest cost of failure; a failed link agreement loses the owner's setting,
while a failed clean agreement degrades to exactly the behaviour above — the
commanded half alone, which this tree already proves. So the clean requires no
confirmation: the commanded half takes its own deadline when it asks and acts at
it whether or not the peer ever armed.

**The erase happens at the boot and not before it.** `eeconfig_disable()`
(`quantum/eeconfig.c`, over `quantum/nvm/eeprom/nvm_eeconfig.c`) is a
whole-backing erase plus a write of `EECONFIG_MAGIC_NUMBER_OFF`, and only the
second has to survive the restart, because the boot that magic forces opens with
`eeconfig_init_quantum()`, whose first statement is `nvm_eeconfig_erase()` — the
same erase. The pre-reset erase duplicated one that runs anyway, and it cost
more than its own ~0.4 s: with the store already erased, `via_init()` found the
VIA magic invalid and ran `eeconfig_init_via()` a few init steps before
`quantum_init()` erased that work and ran it again, so a clean recovery boot
wrote the whole dynamic keymap, the macro area and every ERA region twice. What
the prepare does now is one word (`era_via_system_eeprom_invalidate()`,
`system/era_via_system.c`), at the commit instant like every other agreed act.

This needs no retained object and no reset-cause branch at boot: the invalid
magic *is* the persisted state, and the boot path that reads it is the one every
boot already takes. Every reader between `via_init()` and `quantum_init()` sees
the pre-clean store for a few init steps and is overwritten by
`eeconfig_init_kb()`; the family that guards its own strict reset already tests
`nvm_eeconfig_is_enabled()` and defers to the pending init for exactly this
reason (`sirind/common/tomak_common.c`).

## Fixed Wire Contract

The replacement family retains class `0xE0` but assigns new operations. The
removed `ATTEST/BEGIN/DATA/COMMIT/ABORT` interpretation is not compatible.

**The op/id/lane/direction table for class `0xE0..0xEF` is canonical in
`era_wire_contract.md`.** The body layouts below are canonical here, and that
contract defers them to this document.

Every compact storage payload is exactly 15 bytes:

```text
common bytes
  byte0: compact control with extension bit
  byte1: operation
  byte2: domain id
  byte3: schema
  byte4..5: storage transaction generation, little-endian

PROBE_REQ bytes6..14
  byte6..7: PEER EEPROM policy generation
  byte8..9: PEER image size
  byte10..13: PEER image CRC32
  byte14: reserved zero

PROOF_RSP bytes6..14
  byte6: status
  byte7..10: HOST source revision
  byte11..14: HOST image CRC32

CHUNK_REQ bytes6..14
  byte6..9: HOST source revision
  byte10: chunk id
  byte11: expected data length
  byte12..14: 24-bit local-chunk CRC hint, little-endian; zero means no hint

APPLY_REQ and COMPLETE_REQ bytes6..14
  byte6..9: HOST source revision
  byte10..13: full image CRC32
  byte14: reserved zero

APPLY_RSP and CLOSE_RSP bytes6..14
  byte6: status
  byte7..10: HOST source revision
  byte11..14: full image CRC32

ABORT_REQ and ABORT_RSP bytes6..14
  byte6..9: source revision, or zero before proof
  byte10: abort reason/status
  byte11..14: reserved zero

SYNC_STATUS_REQ bytes6..14 (domain 0xFF = whole-family summary)
  byte6: sender changed mask (summary, bit7 zero) or changed flag
  byte7: sender baseline-record validity
  byte8..9: 16-bit divergence counter (per-conflict form only)
  remaining bytes: reserved zero

SYNC_STATUS_RSP bytes6..14
  same shape as the request, for the responder's facts; the summary
  form's bytes 8..11 are the relation time-anchor seat, REQUIRED
  permanently zero by the validator

> **REFUSED:** fold the two byte-identical `SYNC_STATUS` validator arms.
> **WHY:** the shared arm costs more in coupling than the duplication does —
> the two forms are selected by the domain byte and may diverge.
> **REOPENS:** the request and response forms becoming one body.

PUSH_CTL_REQ bytes6..14
  byte6: phase (0 open, 1 apply, 2 complete, 3 abort)
  byte7..10: push source revision (the initiator's capture revision)
  byte11..14: episode full-image CRC32, or byte11 = abort reason with
  byte12..14 reserved zero on the abort phase

PUSH_RSP bytes6..14
  byte6: status (the fixed table below)
  byte7..10: push source revision echo
  byte11..14: episode full-image CRC32 echo
```

**The anchor seat is permanently zero, and that is a prohibition rather than a
schedule.** The relation time anchor has its own carrier in every relation —
the `TIME_ANCHOR` section — so this seat is reserved for nothing and no work
arms it. Any writer of it is on the permanently closed list
(`era_closed_surface_contract.md`, which owns the prohibition); the validator's
`reserved_zero` call is the enforcement, and the zero reading is collected in
`era_performance_gates.md`.

`PUSH_CHUNK_REQ` is the bulk mirror of `CHUNK_RSP` with the same prefix —
domain, schema, transaction generation, push source revision, chunk id,
data length 1..252, then the copied immutable data. It has no zero-length
form: the chunk-CRC delta hint is not generalized to the push lane.

> **REFUSED:** a zero-length content-match form on `PUSH_CHUNK_REQ`, to give
> the push lane the pull lane's chunk-CRC saving.
> **WHY:** the hint is the requester's, and a pusher holds none of the
> receiver's chunk CRCs to decide an omission from, so the push moves every
> chunk whole either way — 8.6 ms each against pull's 5.5 ms, which is
> 0.06-0.34 s per operation, not the seconds it looked like before the pump
> fix took the walk out of the residue.
> **REOPENS:** `PROOF_RSP` carrying the 66-entry chunk-CRC manifest instead,
> which both halves diff locally so each lane sends only differing chunks and
> the per-chunk hint field retires; 66 × 3 B + 12 B of bulk header = 210 B
> fits one page under `ERA_SPLIT_WIRE_BULK_PAGE_MAX_PAYLOAD_LEN`. Unspecified
> today — body room, epoch binding, staging cost against the SRAM budget, the
> validator arm — and a closed wire format, so an Opening Rule decision.

Storage status/reason ids are fixed:

| Status | Id | Meaning |
| --- | ---: | --- |
| `MATCH` | 0 | HOST and PEER full-domain CRCs already match |
| `TRANSFER` | 1 | HOST has pinned a different valid source image |
| `APPLY_READY` | 2 | HOST still validates the transferred identity |
| `COMPLETE` | 3 | HOST accepts PEER durable-completion publication |
| `ABORTED` | 4 | transaction closed without durable completion |
| `POLICY_CLOSED` | 5 | local EEPROM sync requested policy is disabled |
| `UNSUPPORTED_DOMAIN` | 6 | domain id is not in the exact table |
| `UNSUPPORTED_SCHEMA` | 7 | schema is not the exact domain schema |
| `SIZE_MISMATCH` | 8 | declared size differs from the schema-fixed size |
| `STALE` | 9 | transaction or generation identity is stale |
| `BUSY` | 10 | the one unpinned core0 handoff is required, or a different pinned transaction owns the lane |
| `INTEGRITY_FAIL` | 11 | payload, chunk, or full-image integrity failed |
| `RESULT_FULL` | 12 | local dedicated result capacity was unavailable |
| `TIMEOUT` | 13 | bounded retry/episode deadline expired |
| `ROLE_CHANGED` | 14 | relation or physical execution role changed |
| `SOURCE_CHANGED` | 15 | the pinned HOST source revision was invalidated |

`RESULT_FULL` is a local classification and may appear in a later
`ABORT_REQ`; lack of result reservation never permits transmitting a response
that merely reports `RESULT_FULL`. Unknown op, malformed length/reserved byte,
or invalid compact/bulk lane is a classifier reject and has no storage success
response.

`ABORT_RSP/SOURCE_CHANGED` is also the only legal alternate response operation
to an admitted `CHUNK_REQ`. It carries the request's transaction, domain,
schema, and source revision and terminates that superseded transaction. It does
not authorize HOST independent send and is invalid for any other request/status
pair.

`CHUNK_RSP` has a maximum 264-byte bulk payload and 271-byte frame:

```text
byte0: compact control with extension bit
byte1: CHUNK_RSP
byte2: domain id
byte3: schema
byte4..5: storage transaction generation
byte6..9: HOST source revision
byte10: chunk id
byte11: data length, 0..252; zero is the content-match acknowledgement
byte12..: copied immutable data (absent when data length is zero)
```

The fixed chunk data size is 252 bytes. The last chunk is short. A 16 KiB
domain therefore uses at most 66 chunks. Chunk offset is `chunk_id * 252`; an
offset, length, or chunk count outside the schema-fixed image is rejected.

Request-embedded chunk-CRC delta: the PEER stages its local
chunk bytes into the staging image at request build and sends the low 24
bits of their CRC32 as the `CHUNK_REQ` hint; a zero hint (including a real
CRC that truncates to zero) always requests full data. The HOST responder
compares the hint against the chunk it copied into scratch after the
publication-sequence recheck; on equality it answers a zero-length
`CHUNK_RSP` in the same admitted slot and the PEER keeps its already-staged
local bytes. A zero-length response is accepted only when the pending
request carried a nonzero hint. The full-image CRC32 before apply remains
the sole content authority: a 24-bit false match surfaces as the existing
`INTEGRITY_FAIL` abort, which marks the domain for full fetch (hints
disabled) until its next successful close, so recovery is deterministic.

Compact frames use the existing frame CRC8. Bulk frames use the existing
frame CRC32. The target additionally verifies the proof CRC32 over the complete
staged image before apply and over the complete stored image after apply.

`SESSION_STATUS.bulk_page_supported` is set only where this exact domain
table, bulk decoder, dedicated result capacity, and guarded apply path are
all available — which is what `ERA_HOST_PEER_STORAGE_V1_ENABLE` compiles in.
Both halves must advertise support before PEER selects storage.

## Transaction State Machine

PEER owns forward progress. Domains are considered in ascending id order.

1. `IDLE`: the relation's due token selects the work and starting the episode
   allocates a new transaction generation. A token carries a kind — summary,
   conflict, push, or probe — and, except for the whole-family summary, the
   lowest eligible domain id from that kind's pending mask. Tokens come only
   from the deferred `SOURCE_CHANGED` slot, the relation-open audit, the
   arbitration cell queues, and this half's own settled captures, paced by the
   25 ms storage retry deadline; the grant order is summary, then conflict,
   then push, then probe — arbitration first, then the directions it decided.
   A locally dirty domain is skipped by the grant loop and stays pending until
   its own trailing-quiet capture, and a domain pending in both the probe and
   push queues collides into the conflict queue at grant time.
2. `PROBE`: PEER sends its cached size/CRC. An unpinned HOST returns `BUSY`
   once so HOST core0 can select the requested domain and publish a pinned
   responder snapshot; PEER retries the same wire identity. HOST captures only
   when the domain is clean. A dirty domain is pinned invalid and the retry
   returns terminal `SOURCE_CHANGED`, after which PEER schedules that same
   domain after the trailing quiet interval. Once a clean transaction/domain
   is pinned, the prior `BUSY` is not replayable and the retried request is
   replanned from the pinned image. A
   pinned HOST returns `MATCH`, `TRANSFER`, `POLICY_CLOSED`, `UNSUPPORTED`, or
   a classified stale/busy status.
3. `TRANSFER`: for `TRANSFER`, PEER requests chunks in order. A chunk becomes
   received only after Core1 reserved result publication and core0 copied a
   matching result into the bounded apply image.
4. `TRANSFER_VERIFIED`: PEER has every chunk and the staged CRC matches proof.
5. `APPLY_READY`: PEER sends `APPLY_REQ`; HOST rechecks its pinned source and
   returns `APPLY_RSP`. This acknowledges transfer consistency only.
6. `APPLYING`: PEER core0 performs the durable apply as a sliced write from
   the validated staged image with Core1 running and the wire alive:
   bounded sub-range writes **pumped back-to-back** —
   `era_host_peer_storage_apply_write_active()` (`split/era_host_peer_storage.c`) holds `housekeeping_event_due()`
   true for the width of this phase, so the walk advances once per scan pass
   rather than at the cold cadence — with the wire held by
   whatever still runs while core0 is inside the write. **The wire is not
   exclusively storage's here**: exclusivity ended at `TRANSFER_VERIFIED`, so
   normal routes — the matrix push first — run in
   the windows between the apply's EEPROM operations, and the initiator's
   keys cross while its own apply runs. Any mid-write abort
   trigger (identity change, target dirty) is latched; the local sliced write
   of the validated image finishes first, then read-back, then the wire
   episode aborts — no torn durable state exists.

   **The episode deadline is not one of those triggers, and must not be made
   one.** It cannot prevent (the write-through rule above outranks it) and it
   cannot detect (`eeprom_update_block()` (`drivers/eeprom/eeprom_driver.c`) is synchronous on core0, so a wedged
   write wedges the core that would run the check); all it can do is fire on an
   apply that is slow but progressing, turning a successful convergence into a
   reported abort, a re-probe and an indicator flash. This phase is bounded by
   its own shape instead: the slice cursor advances unconditionally, so the
   apply is strictly monotonic and terminates in ceil(image_size / slice) ticks.
7. `REVALIDATING`: after read-back CRC and runtime-owner reload, one brief
   rotation (revoke/stop, capacity flush, restart under a new owner epoch,
   a few milliseconds of wire silence) anchors identity; `SESSION_STATUS`
   re-establishes the same HOST-PEER authority. No old wire result is
   reused across the owner epoch.
8. `COMPLETING`: PEER sends `COMPLETE_REQ`; HOST returns `CLOSE_RSP` and both
   sides close that domain.
9. The next domain starts, or the episode closes and normal route priority is
   restored.

`MATCH` closes a domain without data/apply. The unpinned `BUSY` handoff and a
clean `MATCH` are proof-only states: they do not make storage exclusive, close
normal HOST matrix response admission, flush the matrix relation, or force a
source-push baseline.

## Push Transaction

The push lane moves the initiator's content to the responder under the same
fixed wire roles, mirroring the pull machine stage for stage:

1. A push-queued domain captures fresh on the initiator, so the publication
   core1 serves from is the settled content the episode's full-image CRC
   names; the capture's revision is the push source revision.
2. `PUSH_CTL/open` runs the probe-symmetric one-shot `BUSY` pin handoff:
   the responder core0 selects the domain, captures its own content, and
   pins as the apply target. A dirty responder domain pins invalid and the
   retried open answers `SOURCE_CHANGED`. The pinned retry compares the
   episode CRC against the responder's own content CRC — `MATCH` closes
   with nothing to move (and heals baselines), `TRANSFER` begins staging
   and storage exclusivity on both roles.
3. `PUSH_CHUNK_REQ` stages each chunk: core1 validates identity and the
   expected chunk against the snapshot, writes the bytes into the pinned
   image — the one place core1 writes the image, as the episode's single
   writer under the same odd/even publication discipline the pull chunk
   copy reads with — and acknowledges with `PUSH_RSP/TRANSFER`. Any
   publication instability refuses the chunk as `SOURCE_CHANGED` instead
   of half-landing it. Chunk bytes travel once: no result record carries
   data in either direction.
4. `PUSH_CTL/apply` is answered `APPLY_READY` on identity and completeness;
   the responder core0 then validates the staged image against the episode
   CRC before any durable write and runs the sliced apply — the accepted
   sequence unchanged: bounded slices with
   dirty-note suppression per slice, read-back CRC, runtime reload even on
   the deferred-abort path, publication. **The CRC validation is this role's
   transfer-verified boundary**: exclusivity clears there, so normal
   admission and response content reopen for the width of the sliced apply;
   the initiator's side cleared at its last acknowledged chunk.
   **This walk is pumped exactly as the initiator's is**, and the predicate
   that does it is written on the activity rather than on the role:
   `era_host_peer_storage_apply_write_active()`
   (`split/era_host_peer_storage.c`) is true in both `PEER_APPLY_WRITE` and
   `HOST_PUSH_APPLY_WRITE`, and holds `housekeeping_event_due()`
   (`split/era_split_transport_scheduler.c`) true on whichever half is
   applying. The walk is `IMAGE_BYTES / APPLY_SLICE_BYTES` = 512 ticks and
   **content-independent** — `apply_write_slice()`
   (`split/era_host_peer_storage.c`) advances its cursor unconditionally, so a
   one-block edit and a 443-block edit cost the same ticks — which is ~34 ms
   pumped. The ~3.0 s figure was the cost of a role-written predicate that
   excluded this side, and it is the one measured number here: before that
   predicate was corrected, the initiator's completion polls stood at 117-129
   per initiator-edited operation against zero on the responder-edited
   direction, flat across a 443-block and an 8-block apply. The ~34 ms is
   derived from the tick count, not measured.
5. The initiator polls `PUSH_CTL/complete` at the retry cadence;
   `APPLY_READY` answers "still applying" — each valid answer resets the
   failure streak **and re-arms the episode deadline** (Scheduler And Matrix
   Recovery below), so what the initiator bounds is "the responder stopped
   answering" rather than "the responder's write took too long" — until the
   durable declaration flips the answer to `COMPLETE`. Both halves
   write baseline = episode CRC and counter = 0 at their own close.
6. **Durable-declare-then-rotate**: only after the initiator has been told
   does the responder run its identity rotation. The initiator returns
   through `SESSION_STATUS` revalidation into a fresh relation open, whose
   arbitration re-proves the pair — the push close's accepted reopen cost.
   A rotation failure costs a re-proof, never divergence: content is
   already durable and reloaded.

The write-through rule binds the responder's apply exactly as it binds the
initiator's: a local write mid-apply, a relation change, the episode
deadline, and a context exit each latch the abort and let the sliced write
finish first, and the reopen arbitration then re-proves the domain
(expected `MATCH` when the write-through content landed). While the
responder applies, the initiator's complete polls are the accepted frames
that feed the responder-silence stale watch; the durable apply mandates no
wire silence beyond the terminal rotation. Unsupported domain/schema, size mismatch, integrity
failure, or policy close never falls back to a prefix or different domain. It
closes/aborts visibly and retries only after new probe eligibility from the
relation-open audit sweep or from the whole-family summary the news value arms,
or after relation revalidation.

## Capacity And Publication

All storage capacity is static. No allocation or Pico SDK queue is allowed.

- one shared core0 source-snapshot/target-apply image: 16384 bytes;
- seven 16-byte cached manifest entries: 112 bytes; HOST source validity is
  the nonzero source revision, while a clean PEER target with revision zero
  may still use its cached CRC to request repair;
- core0 transaction and due state: exactly 144 bytes
  (`ERA_HOST_PEER_STORAGE_CORE0_STATE_BYTES`), asserted by equality at its
  owner and consumed by the communication-core aggregate through that
  macro. The assert is `==` and not `<=` on purpose: an inequality silently
  absorbs struct growth, which is how the aggregate's peer literal once
  drifted four bytes behind. It is three structs — local-EEPROM truth (68),
  relation-scoped episode bookkeeping (24, including the arbitration cell
  queues, the peer summary, and the token kind), and episode runtime data
  (52) — and every move is paid as deliberate arithmetic;
- one semantic Core0-to-Core1 initiator request: at most 40 bytes — the
  push chunk is served from the initiator's own published image, so the
  record carries the image and publication-seq addresses for exactly that
  operation, zero for every other;
- one reserved Core1-to-Core0 initiator result: at most 280 bytes;
- one immutable responder metadata snapshot: at most 64 bytes, carrying
  the recency seat (core0's cold changed mask, baseline validity, and the
  seven counters — what the admitted sync-status answers read, so core1
  never reads EEPROM) and the push staging progress field;
- one reserved responder result: at most 48 bytes;
- one 271-byte bulk wire scratch, one 271-byte decoded-frame scratch, one
  272-byte shared decoded semantic frame, and one 252-byte chunk copy scratch;
- one 88-byte core0 storage diagnostic record and one 8-byte Core1 lane counter
  record. The wire-diagnostics profile additionally owns one 32-byte Core1
  initiator probe, one 88-byte storage copy, one 32-byte probe copy, and two
  4-byte scheduler-edge records for the live state and explicit wire snapshot;
- two aligned 32-bit Core1-to-Core0 result-ready generations are final
  publication hints for the initiator and responder result slots;
- the current wire-diagnostics probe totals **18436** additional static bytes,
  including alignment and publication words, and the cap
  (`ERA_HOST_PEER_STORAGE_STATIC_BUDGET_BYTES`) reads the same figure. The cap
  is the measured aggregate by construction, so the next growth fails a build
  rather than being absorbed into headroom. **The cap is never raised to make
  room; it is moved as arithmetic after the measurement it records.**

  The wire-diagnostics aggregate is pinned by an equality at 18436 and the
  selector-gated cause profile by one at **18660**, so growth in those two is
  caught by the build. Release and qwin carry only the `<=` against the cap,
  which is why the cap is kept at the measured figure rather than left as
  headroom; they total **18276** — 16384 image + 7 x 16 manifest + 144 core0
  state + 88 core0 diagnostics + 1548 Core1 storage, with every diagnostic term
  zero. It is the one figure in this section no `_Static_assert` pins, which is
  why it is the one that drifted. The cause profile's timeline records are an
  explicit addition to the profile budget
  (`ERA_HOST_PEER_STORAGE_PROFILE_BUDGET_BYTES`), not an exemption from the
  cap, and the cap assert is armed in every profile.

This enumeration counts mutable static records only. It does not include the
seven-entry `const` domain descriptor table, which the SRAM-resident image
places in real SRAM; the totals above are therefore a floor, and the actual
figure is whatever the ELF gate measures.

The implementation must use `_Static_assert` for domain, chunk, record, and
aggregate caps and verify actual `.bss`/`.data` movement at the ELF gate.

Cross-core single-producer/single-consumer publication is record, `DMB`, then
aligned 32-bit sequence/index/ready word. A result publisher commits its even
record sequence before publishing the matching nonzero ready generation. A
zero ready generation is a cheap no-result hint; a nonzero hint authorizes
only the full publication/generation validation and never authorizes apply by
itself. The 280-byte initiator result is produced directly in its already
reserved static slot and consumed through a generation-held immutable view;
neither Core1 nor Core0 creates an automatic-stack copy. Core0 copies chunk
bytes from that view into the bounded apply image before releasing it. An
`APPLY_RSP` view is identity-validated and released before flash quiesce can
clear capacity. A published initiator request is one-shot: while its matching
result ready generation is nonzero, Core1 cannot claim the request again. A
failed result-begin releases only the request claim and cannot cancel or erase
an already-ready result. Release or authorized cancel clears result claim
before ready. Immutable source publication is bytes, metadata, `DMB`, then an
even generation. A reader accepts it only when the before/after generation is
the same nonzero even value. Core0 does not mutate the published bytes until
Core1 has released the transaction.

Initiator result capacity is reserved before PEER sends any storage request.
Responder result capacity is reserved before HOST sends any success/status/data
response. The runtime lane's section-less ACK is the one response that reserves
nothing, because it publishes nothing (`era_invariants.md`); every storage
response on this lane carries status or data and keeps its reservation, with
one carve-out on the publication half of the rule: **the provisional
`APPLY_READY` answer to a repeated push complete poll reserves as every
response does, but publishes nothing and releases the reservation unused after
the send.** The reserve cannot be carved out and is not: it is taken for every
admitted responder frame before the response has been planned at all
(`split/communication_core/era_split_communication_core_storage_service.c`),
and a reserve refused there mutes the poll exactly as a full slot would. It is
re-planned
per poll from the published snapshot, its drained record was an explicit no-op
on core0, and holding the slot past the send muted the responder into one initiator
timeout per poll that landed inside the applier's own 70–81 ms flash stalls —
device-read as `timeout` +9 against responder `full` +9 per 443-block apply.
The apply trigger's `APPLY_READY`, the `COMPLETE` flip, every refusal, and a
failed send keep their reservation and publication. For a data response, Core1 also copies the selected immutable chunk
into bounded scratch before transmit. No capacity means no success response;
the classified full counter rises and PEER retries or aborts.

## Retry, Duplicate, And Failure Semantics

- A request uses one transaction generation and operation identity until a
  valid response arrives. Timeout retries the same identity after 25 ms.
- `PROOF_RSP/SOURCE_CHANGED`, `APPLY_RSP/SOURCE_CHANGED`, and the exact
  `CHUNK_REQ` alternate `ABORT_RSP/SOURCE_CHANGED` are valid terminal
  supersede responses. They increment the source-change diagnostic, not the
  timeout or integrity counters; close the current episode without relation
  revalidation; and schedule the same domain after the trailing quiet interval.
  An exclusive transfer still restores the forced matrix baseline on close.
- **The failure streak's abort bound is derived, not chosen.** Consecutive
  timeouts/classifier failures abort the episode — and force `SESSION_STATUS`
  revalidation — only once the streak spans
  `ERA_HOST_PEER_STORAGE_PEER_SILENCE_MS` (1 s) of continuous peer silence at
  the 25 ms retry cadence: twice the widest window in which a healthy peer
  legitimately answers no storage request, its own sliced consolidation erase
  (measured 391–494 ms band), whose slices run back to back while their gaps
  run the keyboard pass and not the wire. `_Static_assert`s beside the
  constants hold the derivation. Because the streak advances once per deadline
  evaluation from the cold cadence, a stall of this half's **own** core0
  contributes one failure whatever its width — a local flash window is
  evidence about this half, never about the peer. The relation's 100 ms
  silence watch keeps its no-exemptions stance untouched: that watch reads
  core1's accepted frames, which an apply never stops; this bound reads
  storage answers, which an apply legitimately does. Three-at-75 ms was the
  old bound and sat inside the peer's own erase window, so every
  consolidation-bearing apply aborted a healthy episode into revalidation and
  a reopen `MATCH` re-proof — device-measured at two aborts and nineteen
  status rounds for one VIA layout load, all self-healed and none necessary. A
  complete episode keeps the 5 s bounded per-phase deadline; no storage state
  may suppress recovery beyond it.
- If that deadline expires while a request is still marked pending, core0 does
  not clear shared capacity in place. It first revokes/releases/stops Core1,
  clears capacity, restarts the same wire role with a new owner/relation
  generation, and only then clears the pending fact and aborts the episode.
- Duplicate `PROBE_REQ`, the current or immediately previous `CHUNK_REQ`,
  `APPLY_REQ`, `COMPLETE_REQ`, and `ABORT_REQ` are idempotent. HOST returns the
  same proof/data/status while its pinned source/replay identity remains valid.
- A replayed `ABORT_RSP/SOURCE_CHANGED` remains bound to the exact original
  `CHUNK_REQ` fingerprint and source revision so loss of that terminal response
  cannot turn source supersession into a timeout.
- Replayed `MATCH`, `TRANSFER`, chunk data, or `APPLY_READY` additionally
  requires the same current non-torn publication sequence, source revision,
  size, and CRC. Source invalidation cannot replay an old success. A replayed
  `CLOSE_RSP` retains the already-established durable-completion identity and
  carries no source bytes.
- PEER accepts a retransmitted current chunk only when identity, length, and
  bytes match the current staged offset. A conflicting or out-of-order chunk
  aborts as integrity failure.
- A lost HOST response is retried by PEER. A lost `CLOSE_RSP` is repaired by a
  repeated `COMPLETE_REQ`; HOST retains one closed replay identity until the
  next transaction or relation generation.
- Out-of-order future chunks, cross-domain/op replies, stale transaction or
  source revisions, and mismatched owner/relation/request generations are
  rejected and never applied.
- Cable loss, role change, local authority change, policy change, reset, or
  source revision change aborts. Reconnect starts from `SESSION_STATUS` and a
  new transaction generation; partial wire progress is not resumed.
- Push-specific: the open's `BUSY` pin is one-shot and replay-suppressed
  like the probe's. A `PUSH_RSP/SOURCE_CHANGED` (the responder's content
  moved, or its publication destabilized under staging) closes the episode
  with the domain re-armed as push; the responder's dirty-pin refusal and
  its later settled hint then force the conflict exchange, so neither
  side's edits are lost to the race. Complete polls answered
  `APPLY_READY` reset the failure streak, never count toward the
  failure-streak abort, and re-arm the episode deadline (the per-waiting-phase
  rule is in Scheduler And Matrix Recovery below). A long responder-side
  write is therefore bounded by the responder answering at all, not by its
  duration.
- **A complete poll answered `APPLY_READY` is never replayed.** It is the
  one request in this family that is a poll rather than an idempotent
  operation: it exists to observe the responder crossing into durable, so
  every poll must be re-planned against current state. Consecutive polls
  are byte-identical and therefore share a request fingerprint, so replay
  would latch the provisional answer and the flip to `COMPLETE` could never
  be observed — the failure class is the full 5 s episode deadline burned on
  a domain already durably applied. A previous `COMPLETE` still replays, as
  the lost-response repair, and so does the apply phase's `APPLY_READY`,
  which acknowledges identity rather than polling for a transition.

## Durable Apply And Power Loss

PEER never writes received chunks directly to EEPROM. After full staging and
`APPLY_RSP`, core0 performs this exact sequence:

1. drain every matching chunk from the generation-held result slot into the
   bounded core0 apply image;
2. validate the final `APPLY_RSP`, domain/schema/size/CRC, every
   generation/identity field, and the
   schema-1 `ERA_CONFIG` reserved-zero ranges;
3. release the final result slot and open the apply cursor; Core1 keeps
   running, no storage request stays pending, and the core1 liveness beat
   holds the wire for the whole write;
4. perform the domain-owner write on core0 as bounded sub-range slices of
   the validated staged image, **pumped back-to-back on whichever half is
   applying** — `era_host_peer_storage_apply_write_active()` (`split/era_host_peer_storage.c`) asks about the
   activity rather than the role, so it is true in both applying states and
   holds `housekeeping_event_due()` true for the width of the phase — which is
   also why the apply is unaffected by the raw-microsecond deadline pre-filter
   in front of that gate (`era_route_contract.md`): it is an event arm, and the
   pre-filter sits on the deadline arm only — re-evaluating
   identity and target-dirty at slice boundaries (and not the episode
   deadline, which this phase does not carry); an abort trigger is latched,
   never applied mid-write, and dirty-note suppression covers only each
   slice's own write;
5. read the complete domain back and verify the expected CRC32;
6. reload the affected runtime owner whenever the read-back CRC matches,
   including on the deferred-abort path, so the runtime never diverges
   from durable content;
7. with no latched abort, publish the current image, then revoke/stop
   Core1, flush all storage/general capacity, restart under a new owner
   epoch, and force `SESSION_STATUS` plus source revalidation — the brief
   identity-anchor rotation, a few milliseconds of wire silence well under
   the responder stale limit;
8. publish durable apply completion only after the same HOST-PEER relation
   is re-established. A latched abort instead aborts the episode with the
   domain left stale, so the reopen audit re-proves it (an expected
   `MATCH` when the write-through content already landed).

A rotation failure after the write aborts the episode and increments the
flash-quiesce failure counter. A read-back CRC mismatch marks the domain
for hint-disabled full fetch. There is no optimistic write and no core0
synchronous wire fallback. A terminal role or mode exit with the cursor
open finishes the local sliced write synchronously before the episode
resets, so a promoted or standalone runtime never types on torn content.

The underlying wear-level driver does not provide a multi-byte atomic commit.
Power loss during step 5 may leave a physically partial domain. Such a boot has
no durable-completion authority: each owner follows its existing boot behavior;
validated owners may default, while raw dynamic keymap/macro reads may expose
the physically stored partial image until the next valid HOST relation. The
target captures a comparison CRC even when it has no valid source revision;
that CRC then differs from the HOST proof and the whole domain is retransmitted.
Firmware never reports a partial image as complete and never resumes from a
partial chunk offset after reset. This recovery contract does not add or reuse
durable epoch/journal metadata; stronger power-loss atomicity would require a
separately approved storage format and invariant.

## Scheduler And Matrix Recovery

- Dirty callbacks only update cached domain dirty generation and quiet
  deadline. EEPROM scan, source comparison, CRC construction, snapshot copy,
  route enqueue, and apply run only at the cold task boundary.
- The matrix-scan transport hooks never capture, compare, CRC, enqueue, decode,
  or apply storage.
- Probe scheduling is audit/summary driven and bounded: one mandatory
  seven-domain audit sweep at the 25 ms storage retry cadence on every
  relation (re)establishment, plus the whole-family summary the peer's news
  value arms, whose own result decides direction and domains. **No hint
  schedules a single-domain probe**: a news value names no domain, and no
  path writes `probe_pending_mask` from a peer's claim. There is no periodic
  idle patrol. A dirty domain
  becomes eligible only after the 1000 ms trailing storage quiet interval; a
  proof request cannot force early capture.
- Probe pacing after a failed episode backs off exponentially with the
  failing domain's consecutive-failure streak (25 ms doubling toward a
  1 s bound); the streak persists across relation events and resets on
  that domain's next successful close. A single tracker follows the most
  recently failing domain, so alternating multi-domain failures pace at
  the first backoff step instead of ramping. The mandatory audit sweep
  still reaches bounded completion; only its post-failure pacing
  stretches. A structurally failing transfer therefore degrades to slow
  retry, never a livelock.
- The initial `PROBE_REQ`/`BUSY` core0 pin handoff remains nonexclusive so
  HOST continues to admit heartbeat and matrix source-push responses. Only a
  validated `TRANSFER` result starts storage exclusivity on both roles.
  Mandatory `SESSION_STATUS` recovery/revalidation preempts it, and relation
  staleness or the 5 s episode deadline cancels it.

  **Exclusivity ends at each role's transfer-verified boundary, not at the
  episode close.** The transfer phase stays exclusive; the apply phase
  is a local flash operation on the receiving half and runs with normal
  routes admitted **between** EEPROM operations — never inside a sliced
  erase's gap, which runs the keyboard pass and not the wire
  (`era_invariants.md`). What exclusivity admits between operations and what a
  gap runs are separate questions. The boundaries, per
  role: the pull initiator's staged-image CRC validation; the pull responder
  answering `APPLY_RSP` (the `APPLY_REQ` exists only because the initiator
  validated); the push initiator's last acknowledged chunk; the push
  responder's own staged-image CRC validation before any durable write. An
  abort still raises exclusivity for its bounded `ABORT_REQ` cleanup, from
  any phase. What this recovers is a press-and-release inside the apply
  window — a key still held at the close always arrived through the forced
  baseline — and **how much of the apply span core0 actually spends outside
  an EEPROM operation is measured**: almost the whole save bracket is inside
  EEPROM operations, leaving the admitted windows the small remainder
  (**What The Lane Costs A Typist** carries both figures). They are wide enough to
  carry a press-and-release across and no wider; no design may assume them as
  usable room.
- **The episode deadline is per waiting phase, not per episode** — this
  section owns that rule and the push and retry sections point at it. The 5 s
  deadline is armed at episode start and re-armed at each boundary where this
  half begins waiting on something it cannot see: the responder re-arms when it
  answers `APPLY_RSP` and starts waiting for a `COMPLETE_REQ` it has no wire
  traffic to mark time against, and the initiator re-arms on every provisional
  `APPLY_READY` during a push. It is not carried at all through either role's
  own durable write.

  One budget must not be spent twice on one episode: a large domain's transfer
  and the apply that follows it are separate waits, and the waiting half can
  neither see the apply nor influence it. The transfer's own share is not
  content-scaled either — a domain moves whole, so every macro-domain episode
  is 66 chunks whatever the macro holds.
- Accepted storage requests are relation-liveness evidence. The
  responder-silence stale watch is fed from the Core1 accepted-RX
  observation — a monotonic counter bumped once per frame the wire
  transaction engine accepts, at the single responder service point
  before lane routing — timestamped by cold core0 on observed movement.
  An active `CHUNK` stream therefore never starves the watch; it fires
  only when the peer stops producing accepted frames for the stale limit.
- The durable apply mandates no wire silence, and **what holds the wire is
  core1**. The applying half's core1 stays up through the sliced write and runs
  the standing exchange's liveness beat, so the peer's accepted-RX watch stays
  fed by real frames for the whole apply window. The only wire gap is the
  terminal identity rotation (a few milliseconds). A core0-emitted
  `SESSION_STATUS` keepalive cannot do this job and must not be reintroduced:
  the frame is core0's, and core0 inside the flash write is exactly what cannot
  send one. The beat, its period, and the grant that survives exclusivity are
  canonical in `era_route_contract.md`.

  This covers both serviced relations — one mechanism in one place, with no
  relation-shaped hole. **The HOST-PEER half is unmeasured**: the Storage
  Apply-Liveness Gate has run on DUAL-HOST only, its HOST-PEER leg is owed, and
  DUAL-HOST's figures do not transfer (`era_performance_gates.md`).

  The responder-silence stale watch has no exemptions and needs none. A
  wear-leveling consolidation stall exceeding the stale limit is a
  staleness event rather than an exemption; that stance, and the
  pre-flight gate rejected in favour of it, are in
  `era_sram_residency_contract.md`.
- A role reset or explicit diagnostic flush revokes/releases the old Core1
  owner before storage capacity is cleared. Restart rotates the relation,
  schedules `SESSION_STATUS`, and aborts any pending storage request
  rather than leaving a cleared result reservation marked pending on
  core0. On the SRAM-resident image an unrelated core0 flash write no
  longer revokes the owner: the wire keeps running through the commit
  window.
- HOST remains responder-only. Storage activity never creates an owner route on
  HOST.
- Close or abort after an exclusive transfer flushes the HOST-PEER matrix
  relation (`era_route_contract.md` is canonical for what that flush does to
  route ordering, and for why a proof-only `MATCH` closes without flushing the
  live projection). **The flush keys on the episode having run an exclusive
  transfer, not on exclusivity still being held at the close**: exclusivity
  ends at the transfer-verified boundary above, so rows accepted during the
  apply phase projected live and the close's forced baseline reconciles held
  keys. A stale/role/owner
  failure still forces relation revalidation and recovery even when detected
  before transfer.

## Diagnostics

**The EEPROM SYNC indicator is one engine-owned pending fact, with no timer
anywhere in its path.** The lamp on each half is
`era_host_peer_storage_indicator_pending()` held to a 160 ms minimum-visible
floor anchored at that half's own rise (`era_split_eeprom_sync.c`); the
floor pads a short process to visibility and never extends a long one, so it
is a rise-anchored guarantee rather than a trailing delay. The fact is the
union of two arms:

- **The local arm** — this half's own unfinished pair work, O(1) RAM:
  dirty content inside its trailing quiet interval; settled content departed
  from its persisted baseline (a RAM changed shadow written at exactly the
  three sites that already read the baseline record — settle, boot,
  convergence close — plus the terminal refusal, which retires a claim the
  pair can no longer act on); the decided content-moving cells
  (push | conflict | the pull-expected `probe ∩ peer-changed` subset); an
  armed **in-session** summary; and a decided episode's own span — the
  content-expected latch taken from the token's cell at start, then the
  moving span through close. All behind one cold-cached gate:
  serviceability AND this half's own sync policy.
- **The mirror** — the peer's advertised fact, which is its whole local
  arm, **dirty phase included** (owner ruling): a VIA save is a multi-second
  write stream, so a settle-anchored advertisement puts the responder seconds
  behind the edited half's rise and dips it dark between one domain's
  convergence and the next one's settle. One fact, one carrier per direction:
  the initiator
  advertises it as the `STORAGE_PENDING` push section, the responder as
  bit7 of the `STORAGE_NEWS` response byte (`era_wire_contract.md` holds
  both bodies), and each half's drain latches the one direction it
  receives. It survives relation identity rotations, because the fact it
  mirrors does and **both carriers force one cross on the fresh relation,
  every value included** — the push section by its INPUT-class first-cross
  discipline, the news byte by the invalid-shadow force. Without that force a
  pure-target responder's all-zero post-rotation byte strands the initiator's
  mirror lit, which is the device-caught case and the reason the re-statement
  is guaranteed by mechanism rather than by the value happening to differ. It
  clears when the relation leaves service, so a standalone half never wears a
  stale lamp.

> **REFUSED:** a responder-side lamp term bridging its own emitted news
> step to the round it buys ("stepped, consequence not yet returned").
> **WHY:** the consequence returns only through the mirror, and the mirror
> is gated by the initiator's local policy, which never crosses — the term
> has no observable termination.
> **REOPENS:** the initiator's storage admission state crossing the wire
> as a fact.

What this buys, stated as the structure rather than a hope: **the two lamps
fall within one poll of the initiator's last close in both directions.** The
initiator's arm falls at its own close processing; the responder's own arm
fell at the same two-sided exchange, and its mirror falls at the section's
zero crossing on the next poll — so the responder's fall is ordered after
the initiator's by the wire, not guessed at by a clock. The bridge this
replaces stood in for the mirror with a fixed 1500 ms constant run on each
half's own clock, and a constant standing in for a missing wire fact has no
correct value.

The doctrines the old arm carried survive, restated against the new terms.
**An operation that moves nothing shows nothing beyond its own save**: the
verify cells, `MATCH` re-proofs, refusals and the relation-open audit sweep
set no term (the audit's verify-all summary is deliberately excluded from
the summary term), so a converged pair's boot, reconnect and post-push
reopen all run dark. **One operator action is one lamp**: the dirty term, the changed shadow
and the cells hold the fact across the write stream, inter-episode gaps,
per-domain grants and the durable apply's identity rotation, so a
multi-domain layout load reads as one span on both halves; a receiver-side
`spans=2` for one load is the settle-anchored advertisement this ruling
removed. **Entry synchronizes in both edit directions**: each half's
advertised fact includes its dirty phase and crosses on its own carrier, so
the pair rises within delivery latency (a housekeeping pass plus a poll plus
the receiver's drain, tens of milliseconds) of the operation's first local
write, whichever half took it. The responder-edited direction was
settle-bound until the news byte's reserved bit7 carried the dirty phase —
the response mask has no free marker, so without it the responder has no
carrier toward the initiator and the initiator joins a whole write stream
plus the 1000 ms quiet later. Widening an existing body rather than opening
a new section is what a full mask leaves
(`era_closed_surface_contract.md` records the opening).
**A terminal refusal goes dark rather than reading as working**: it closes,
arms nothing, and retires its domain's lamp claim — while a *retrying*
failure keeps the lamp honestly lit through its backoff, because re-armed
work is work. A CLEANed or fresh store boots with an invalid baseline
record, seeds the shadow all-changed, and shows one lamp through its first
convergence sweep — the per-domain closes clear it.

The explicit paced `wire storage` line family owns these compact counters:

- `open/close/abort/restart`;
- `proof/match/xfer`;
- `chunk/dup/retry/timeout`;
- `apply/complete`;
- `stale/full`;
- `integrity/version/domain` rejects;
- `qfail` Core1 quiesce/restart failures during apply or pending-timeout
  recovery.
- `news/probe`: this half's storage news value and the current initiator
  pending probe mask (relation-open audit sweep plus summaries the news value
  armed), read from cold core0 state without growing the fixed 88-byte
  diagnostic record. The `csp` lane's arrival counters are an **absence**
  instrument and must read zero; field semantics in `era_capture_reading.md`.
- `recency`: baseline-record validity, the per-domain changed mask, and the
  seven divergence counters (Recency Layer above), read cold at print time
  without growing the fixed 88-byte diagnostic record; field semantics in
  `era_capture_reading.md`.
- `core cl/tx/rx/pub/fail`, current request/result/ready generations, and the
  last stage/result/failure/op/status. These are diagnostic-profile-only Core1
  publication facts and do not authorize execution or expose live owner state.

Diagnostics distinguish PEER initiator result-full from HOST responder
result-full and distinguish transfer completion from durable apply completion.
No automatic output is allowed.

## What The Lane Costs A Typist

Every rule above is in force. These are the behaviours it produces that a
reader would otherwise take for defects, and each is a decision rather than an
oversight.

- **Keyboard operation during an EEPROM write or sync is not guaranteed, and
  guaranteeing it is not a goal** (owner decision). The RP2040 stalls core0 for
  the width of each flash program/erase, measured at **~4.15 ms per write
  block** plus erase, and located as irreducible in place — so a 14.1 KB save
  loses about 3.5 s of scanning. A keyboard being configured is not a keyboard
  being typed on. **What this does not waive**: the relation surviving a save,
  storage convergence completing, and post-close correctness — no stuck matrix,
  and the held-key baseline reconciling after close. No code in the tree exists
  whose goal is typing continuity through a save; the one attempt was falsified
  and reverted.
- **A PEER's keys stop across a macro convergence's exclusive span.** The
  convergence measures roughly 209 ms of transfer plus 2.3 s of apply plus the
  reopen, and a key pressed *and released* while the wire is exclusively
  storage's never reaches the HOST, because the exclusive close's forced
  baseline carries current state and not a transition. **The apply is not part
  of that span**: exclusivity ends at transfer-verified, so admitted routes get
  the windows between the apply's EEPROM operations — wide enough to carry a
  press-and-release across and far too narrow for any design to count on. Core0
  sits inside EEPROM operations for **~97.6 %** of the save bracket, so the
  windows the lift opens are the remaining ~2.4 % and no design may assume
  otherwise. The loss is accepted on reachability: it happens only while the
  user is saving configuration in VIA.
- **In-session silent corruption and hint-implementation defects are detected
  only at the next relation event.** The periodic idle patrol is gone by
  design.
- **A wear-leveling consolidation stall exceeding the responder-silence limit
  is an honest staleness event with clean recovery**, not a liveness exemption.
  Its recorded signature is `stall_ms` about 81 with `sy=sl=12` inside durable
  applies of 2.3 s, at `max_ms` in the 428–494 band; the width is what
  classifies it.
- **A CLEAN convergence has been observed failing to complete one domain**,
  with eight wire timeouts on the CLEANed half's first boot: `core rx` exactly
  eight short of `core tx`, `abort=1`, `xfer=4` against `complete=3`. The same
  boot read `sl=12 sy=0` — the erase decomposed and yielded to nothing, because
  `keyboard_init()` (`quantum/keyboard.c`) runs before the loop those gaps
  yield to — so core0 was blocked for the whole ~399 ms window and requests
  issued across it expire on their own 5 ms core1 bound. That makes the
  timeouts plausibly structural and **the incomplete transfer not**: nothing
  establishes that the aborted domain ever converged. Unfixed, and the
  discriminator is the `cause` profile's storage cause timeline, which orders
  `EEPROM_BEGIN`/`EEPROM_END` against the aborts.

> **REFUSED:** interleave admitted routes into the CHUNK phase.
> **WHY:** storage exclusivity across the transfer is what keeps a partly
> written image from being read as a whole one.
> **REOPENS:** new evidence that the exclusive span costs something the
> reachability argument above does not already accept.
