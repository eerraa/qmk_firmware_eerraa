# ERA Authority Contract

Status: active
Genre: contract
Canonical for: relation authority and its derivation, local session facts, the
persisted sync-policy bits and their EEPROM block, matrix readiness, storage
direction authority, revalidation policy, and the authority/scheduler boundary
Read when: editing reducer, mode planner, session, router, or responder gates

## Local Authority Facts

- `accepted_host_open`: this half has an accepted local HOST.
- `accepted_no_host`: this half has accepted no local HOST.
- Exactly one of `accepted_host_open` or `accepted_no_host` may be true in a
  valid local session.
- `bulk_page_supported` rides the same session status but is a compile-time
  capability, not a session fact (Replacement Storage Authority, below).
- Local USB authority has exactly one derivation: the reducer, initialized at
  `keyboard_pre_init_kb` and reduced continuously from reduced USB stack state
  plus reducer-local SOF/suspend freshness. There is no boot latch and no
  cold-boot master probe. `is_keyboard_master()` and
  `is_keyboard_master_impl()` are one-line projections of
  `accepted_local_host_open`, and the `split_config.master` the second fills is
  read by nothing.
- `is_keyboard_master_impl()` MUST keep its ERA override. QMK's weak version
  calls `usb_disconnect()` on any half it judges non-master, which would drop
  the D+ pull-up at boot and make the reducer's later promotion to HOST
  unreachable. Absence of a `usb_disconnect` symbol in the linked image is the
  check, and the procedure for it is a Source Gate item in
  `era_performance_gates.md`.
- The reducer latches handedness from `SPLIT_HAND_PIN`, not from
  `is_keyboard_left()`. QMK does not fill `split_config.left` until
  `split_pre_init()`, several init steps after the reducer exists, and the
  peer-unknown wire initiator is derived from the side alone, so reading the
  QMK projection at init would leave both halves responder.
- ERA RP2040 split role authority MUST NOT use raw VBUS detection. ERA RP2040
  split builds require `SPLIT_USB_DETECT` and must not define `USB_VBUS_PIN`.
  Neither selector is read by ERA code now that the probe is gone; both remain
  required because QMK's weak `is_keyboard_master_impl()` falls back to
  `usb_vbus_state()` without `SPLIT_USB_DETECT`, and that fallback becomes live
  the moment the override above is removed.
- USB suspend after an accepted HOST-open session is preserved as
  `accepted_host_open` only inside the local suspend grace window. Short
  suspend is a low-power state of the same local HOST, not a HOST close.
  On RP2040 ChibiOS USB builds, indefinite suspend without resume/configured
  evidence can look like split-cable-powered USB removal, so the reducer treats
  sustained suspend as HOST close/reset and returns to `accepted_no_host`.
- A USB suspend notification with no active local USB configuration is not a
  valid HOST suspend. ERA treats that local stack state as HOST close/reset so
  split-cable-powered halves can return to `accepted_no_host`.
- Outside suspend, host-open holds only while the local stack is CONFIGURED
  *and* the SOF stamp is fresher than `ERA_SPLIT_AUTHORITY_SOF_FRESH_MS`. A
  stale stamp and any other configure state — an actual host close or reset —
  both return to `accepted_no_host` through the same residual arm
  (`split/era_split_authority_reducer.c`,
  `era_split_authority_host_open_for_state_locked()`'s final return). This is
  the third close, and it is the one a reader counting the two above would
  otherwise not find.
- A firmware-initiated USB re-enumeration is not a HOST close and not lighting
  sleep. VIA Apply bounces the bus so the toggle-as-action reads back off
  (`split/era_split_via_link.c`); `era_usb_session_note_firmware_reattach()`
  (`system/era_usb_session.c`) holds host-open and the frame-loss arm for that
  window. A real unplug that outlasts the hold closes as before.
- The suspend grace and host-open close govern authority only. Local lighting
  sleep MUST NOT be gated on authority host-open, and it must persist through
  the authority close that a sustained suspend deliberately causes. The sleep
  predicate takes **two detectors of one physical event** — the host's explicit
  USB suspend and the loss of USB frames — owned by `system/era_usb_session.c`,
  whose frame-loss arm the split predicate ORs into its remapped
  configure-state `SUSPEND` term; the detector model is canonical in
  `era_board_adoption.md`'s **Non-Split Board Baseline**. That predicate answers
  "what does **my** USB session say" and is not by itself the decision — see
  **Lighting Sleep Ownership** below.

## Lighting Sleep Ownership

**A half's lighting sleep has exactly one owner, and which one is a property of
the relation rather than of the USB stack:**

| Relation role | Owner |
| --- | --- |
| `LOCAL_NO_LINK`, both DUAL-HOST roles, the HOST-PEER **HOST** | its own USB session |
| the HOST-PEER **PEER** | the wire — the relation's HOST |

**The non-owner does not write the render gate.** A HOST-PEER pair shares one
USB session and that session is the HOST's, so on the PEER the local predicate
above has no subject; every other role has one and answers from it.

**Ownership transfer is an edge, with a rule per direction, and both directions
are load-bearing:**

- **local → wire** (a half demoted into a PEER): the incoming owner has said
  nothing yet, so the half holds **no** locally-decided value and resolves
  **lit**. Lit rather than dark because dark-until-told is the defect this rule
  exists for, and because the relation rotation drops the responder's RGB sent
  shadow — so the HOST's answer is due on the relation's first response and
  arrives within one poll.
- **wire → local** (promotion, or becoming DUAL-HOST): the wire's last word is
  dropped and this half's own session decides from the next pass. Keeping the
  HOST's last sleep bit across a promotion is the same defect pointed the other
  way.

**What this replaces is two writers and no owner**, and the premise that made
that survive review is the one to read before touching this: the local predicate
ran on every half in every relation, safe only because "a PEER never
enumerates" — so its has-a-host-ever latch and the unconfigured
`SUSPEND`→`INIT` remap both stayed shut. **A half demoted from DUAL-HOST to PEER
is a PEER that has enumerated**, and both arms fire on it: the bus goes idle so
the LLD reports `SUSPEND` with a non-zero USB configuration, which the remap
does not touch, and the latch never comes down. Device-reported 2026-08-13 as a
demoted half whose keys kept working while its lighting went dark and stayed
dark until a key was pressed on the other half.

**A board status report may still punch the render gate** to force a frame
visible — the core1 launch-failure report holds it for 3.68 s. That is a render
override and not a second owner of the decision: it re-asserts for as long as it
runs, and the resolver writes only on an edge of its own value. **What a
HOST-PEER HOST publishes to its PEER is the resolved decision and never the raw
gate**, so a status report on one half cannot flash the other half's lighting.
- USB remote wake is a local USB session operation, and its trigger consumes
  exactly two local facts: the local session state (bus suspended, host
  granted remote wake) and the composed-matrix input edge. It MUST NOT be
  gated on authority host-open, `is_keyboard_master()` (`split/era_split_authority_reducer.c`), or the action
  pipeline. The suspend-grace close above propagates through QMK's weak
  `should_process_keypress()` and silences `process_record()` for the rest of
  a sustained suspend, so **a wake requester placed behind the action pipeline
  is unreachable exactly when it is needed** — that arrangement shipped once as
  a regression, masked because the RGB sleep beside it looked correct. The
  input that emits the wake is consumed locally rather than forwarded as
  ordinary key input: past the grace window the closed action pipeline consumes
  all suspend-time input, and inside it the `process_record()` requester still
  swallows the waking press.

  **A PEER's key reaches this trigger only as the HOST's own composed-matrix
  edge** — ordinary source-push, ordinary projection, no second path; transport
  receive paths still must not inject HID events or push raw USB wake state.
  That makes the requester's ungating necessary and not sufficient, and the
  insufficiency is what shipped: the input it fires on is *produced by the
  relation*, so the relation has to outlive the suspend the wake exists for.
  It did not. The grace close ended the relation itself, the PEER stopped
  selecting source-push and the HOST stopped projecting, and a correctly placed,
  correctly ungated requester was left with nothing to fire on for the whole
  sleep — device-reported 2026-08-13 as a HOST key that wakes the computer and a
  PEER key that does not. **Relation Hold** below is what keeps that input
  alive, and it is the load-bearing half of this rule rather than a neighbouring
  concern.

## Persisted Sync Policy

There is no DUAL-HOST parent switch and no separate per-relation EEPROM
request. What persists is one relation-independent requested bit per sync
family:

- EEPROM, INPUT, and RGB sync requested values are persisted per-half owner
  intent. **The EEPROM bit is the one request replacement storage consumes, in
  every relation the lane is admitted for**: one relation-independent storage
  engine, one transaction authority, one state, one diagnostic surface — one
  bit feeds one engine, and there are no two storage families to share it
  between. **DUAL-HOST storage support is therefore `requested` alone**, with
  no separate effective-support term. INPUT and RGB keep theirs below.
- **The RGB requested bit's scope is the DUAL-HOST RGB runtime — the config
  sections, and the visual pressed-baseline family with its reactive half, in
  both directions**: effect is `requested && the section being eligible at
  all`, never a compile-capability macro, with the eligibility half held by the
  table in `era_wire_contract.md`. The arms follow the EEPROM precedent — the
  sender's own requested bit gates capture and arming (the standing plan's RGB
  and visual fields, the responder RGB and visual snapshots), and the
  receiver's own requested bit gates the apply (the DUAL-HOST standing visual
  apply advances its sequence shadow either way, so a gated-off apply reads
  "sent but not applied", never a replay left pending). HOST-PEER's RGB
  response section and its visual baseline are deliberately ungated by this
  bit: the dark PEER mirroring the HOST's render state is that relation's
  correctness path, not a sync preference.

  **It defaults ON since storage version 5** (owner decision 2026-08-13), which
  makes all three families default-on. Its default-off era rested on "a lighting
  preference is a preference rather than a correctness fix" — true of the bit and
  false of the shipped result, because a pair whose halves render independently
  is two keyboards that happen to be cabled together. Turning it off is now an
  owner's explicit acceptance of that, the shape the INPUT bit already had.
- **The INPUT requested bit is live from storage version 4, and its scope is
  exactly the DUAL-HOST INPUT-class runtime — the layer byte and the
  ACTIVITY body, both directions.** It defaults ON, because its absence is
  the cross-half layer defect the INPUT class exists to fix and a default-off
  toggle would re-ship it; turning it off is an owner's explicit acceptance of
  single-half input semantics for that half (cross-half layers and tap-hold
  judgment degrade to single-keyboard timing, cleanly — every judgment-window
  close is local). The arms are the RGB pattern with one refinement: an off
  **sender substitutes the neutral value** — layer 0, the zero activity record
  — instead of withholding the section, because INPUT-class state is transient
  correctness state the receiver holds applied and must retire through the
  apply path. The sent shadows make each neutral cross exactly once (the layer
  byte's invalid shadow forces that one send at relation open; ACTIVITY's
  zero-baseline rule sends nothing on fresh defaults), so the off state's steady
  wire cost is zero. The disable edge clears peer-derived INPUT state (the peer
  layer and the tap-activity caches, the rotation-clear set); the enable edge
  re-observes the standing state so an initiator's inbound state heals
  immediately — the responder side has no retained push record to replay and
  heals on the peer's next edge, the accepted residual. HOST-PEER carries no
  INPUT-class section in either direction (asserted), so the bit reaches
  nothing there.
- Persisting a requested value opens no route selection, wire payload, or
  execution by itself.

**The split link level is not one of these and must not be added to them.** It
is persisted per half and sync-excluded like them, and it is a different kind of
fact: a sync policy is an owner preference each half may hold differently. The
wire must run one level or no session opens; stored levels may differ, and
`split/era_split_link.h`'s **Reconciliation** is what makes the running
levels match — the pair meets at Low and raises to the winner's stored
level. Syncing the stored byte through this block would gate that pair
invariant on a preference that can be switched off, and would give one byte
two writers, which is why it has its own protected region rather than a flag
in this block (`contracts/era_wire_contract.md` for the wire,
`maps/era_source_map.md` for the units).

**Nothing in the protected range travels, and that is why an EEPROM clean is an
agreed restart rather than a local one.** A clean on one half converges all seven
storage domains through the engine's own cells — device-shown, and recorded in
`era_host_peer_storage_contract.md`'s **Arbitration** — but leaves `176..255`
split, because that range is not a domain, so the two halves keep different sync
toggles and different link levels — and the pair's link target is the
winner's stored level (`contracts/era_route_contract.md`), so a clean taken
on one half alone would not clean the pair's. Erasing on both halves at one agreed instant is the
fix, and it is the storage contract's to state.

Local sync policy storage is the ERA config local-policy block:

- ERA config offset `176`, size `24` bytes
  (`ERA_EEPROM_CONFIG_ADDR + ERA_EEPROM_LOCAL_POLICY_CONFIG_OFFSET`). The block
  starts the protected range, which is asserted in `storage/era_eeprom_layout.h`
  rather than restated here.
- Offset `+0`: sync policy signature.
- Offset `+4`: flags generation (moves on any requested change).
- Offset `+6`: EEPROM policy generation (moves only on the EEPROM bit, so an
  INPUT/RGB toggle cannot invalidate an in-flight storage transaction's
  generation match).
- Offset `+8`: sync policy storage version, **5, and the only accepted value**.
- Offset `+9`: requested flags: EEPROM, INPUT, RGB — **all three default on**.
- Offset `+10..+23`: the seven per-domain 16-bit LE divergence counters of the
  storage recency layer. They fit this block's reserved range exactly but are
  storage-engine territory: their meaning and update rules are canonical in
  `era_host_peer_storage_contract.md` (Recency Layer), an ordinary policy
  persist writes only the `+0..+9` prefix so a policy toggle can never clobber
  them, and their values carry no validity meaning for this block.

The sync policy signature supersedes the retired dual-host-only policy
signature; changing the signature resets this block at boot.

**There is no in-place upgrade, and version 5 is the only accepted value**
(`era_source_map.md`'s Stored-Data Compatibility carries the owner decision and
the upgrade path it rejected). A block whose signature, flag mask, generations
or version byte is not exactly this one fails validity and is rewritten
**whole** — defaults, a fresh signature, version 5, counter bytes zeroed. The
rewrite must be whole rather than partial because an ordinary persist writes the
`+0..+9` prefix only, so a partial rewrite would leave a foreign block's counter
bytes alive underneath a valid signature. A block from an earlier layout
therefore loses its divergence counters along with its toggles, which the
recency layer degrades conservatively for
(`era_host_peer_storage_contract.md`, Arbitration).

**A changed default rides the version byte, and that is the rule rather than
what happened twice.** A valid block is loaded verbatim, so a default nothing
rewrites reaches only hardware that has never booted — the layout can be
identical and the version still has to move. Version 4 carried INPUT's
default-on and version 5 carries RGB's.

## Replacement Storage Authority

The exact domain and transaction contract is
`era_host_peer_storage_contract.md`. This section governs every relation the
lane is admitted for; the `host_peer` in the file and symbol names is
historical, retained because renaming a proven engine is churn rather than a
correction (`era_source_map.md`).

- Direction authority is per-domain latest-change-wins arbitration: a one-sided
  change wins that side, both-changed resolves by persisted divergence count
  then Left, and the winner's whole-domain content moves under the existing
  CRC/generation transfer authority. The rules, cells, and carriers are
  canonical in `era_host_peer_storage_contract.md`. **A fixed
  HOST-source/PEER-target winner is the rejected alternative**: in DUAL-HOST
  both halves are USB-enumerated and either can take a VIA edit, so a fixed
  winner discards whichever half the user did not edit second. HOST-PEER's
  steady state is unaffected either way — an in-relation PEER takes no VIA
  edits, so the HOST side is the only mover — and what the rule changes there is
  divergent-history reopens, which now preserve the newer content instead of
  reverting it.
- **The relation's initiator is the only initiator**, for the pull lane, the
  push lane, and the arbitration exchanges alike: the PEER in HOST-PEER, the
  Left in DUAL-HOST. Responder data or acknowledgement is legal only in the
  admitted response slot opened by an initiator storage request; responder
  independent send remains closed. Arbitration decides which half's content
  wins, never which half initiates.
- **Which relations admit the lane, and in which role, is canonical in
  `era_host_peer_storage_contract.md`** (Relation Admission). Three helpers in
  one source file are the engine's whole relation coupling, so this contract
  states the rule and carries no second copy of the table to drift against it.
- Both halves keep their own local requested policy (the unified EEPROM sync
  bit) and generation. An initiator with local requested policy off does not
  select storage. A responder with local requested policy off admits no data
  and may report `POLICY_CLOSED` in an admitted proof response. This is the
  whole of the VIA toggle's effect on DUAL-HOST storage: the same bit, the
  same two arms.
- One engine means one identity space. Transaction generation, authority,
  state, and diagnostics are the lane's, not a relation's, and a relation
  change rotates them rather than switching between two sets.
- Core0 source capture and target durable apply require matching owner epoch,
  relation generation, request generation, EEPROM policy generation,
  storage transaction generation, domain/schema, source revision, and image
  integrity. Chunk results additionally require matching chunk identity.
- Wire receipt or final-chunk receipt is transfer completion only. The
  applying half's core0 owns durable completion — the initiator for a pull,
  the responder for a push — after guarded mutation, read-back verification,
  runtime reload, and the identity rotation its lane defines (the applier's
  own rotation for pull; durable-declare-then-rotate for push).
- Responder source supersession is a data-generation event, not a
  relation-authority change. Its generation-bound terminal response closes and
  requeues the same domain without granting the responder initiation or
  forcing `SESSION_STATUS`; actual owner/relation/policy change still requires
  revalidation.
- `bulk_page_supported` is a compile-time capability, not a session-varying
  fact: it is `true` exactly in a build that compiles the replacement decoder,
  dedicated request/result capacity, domain table, and guarded core0 apply
  together, mirroring `ERA_HOST_PEER_STORAGE_V1_ENABLE`, and it is not
  board-scoped. It grants no route and no responder independent-send authority,
  and the DUAL-HOST runtime families (bundle, RGB, INPUT) open on their own
  eligibility regardless of it. Wire form: `era_wire_contract.md`.

## Matrix Ready

`SESSION_STATUS.matrix_ready` MUST be true only when:

- local session is valid;
- `accepted_no_host=true`;
- HOST-PEER fast-path local matrix publisher has a publishable snapshot ready.

`SESSION_STATUS.matrix_ready` MUST be false for:

- HOST-open local sessions;
- DUAL-HOST local sessions;
- invalid or unknown local authority.

## HOST-PEER Matrix Admission

HOST accepts HOST-PEER matrix payloads only when all are true:

- local relation is HOST-PEER HOST;
- local session is valid;
- peer session is known;
- peer has `accepted_host_open=false`;
- peer has `accepted_no_host=true`;
- peer has `matrix_ready=true`.

**The first row is the whole of the role term, and `accepted_host_open` is
deliberately not a second copy of it.** The two were the same answer for as long
as the relation could not outlive the local host-open, and stopped being the
same answer the moment it could (**Relation Hold**). A suspended HOST is still
its relation's HOST and still owns the projection its own remote wake reads;
what the closed host-open governs is whether it may emit HID, which QMK's
suspended `should_process_keypress()` (`quantum/keyboard.c`) already holds and this gate never had to
repeat. Asking it here made "am I this relation's HOST" and "may I type right
now" one condition, and only the second is what a sustained suspend answers.

## HOST-PEER HOST Source Response Admission

HOST-source response admission, once a concrete section opens, is response-slot
only:

- local relation is HOST-PEER HOST;
- the PEER request has already passed the HOST-PEER heartbeat/source-push
  admission gate;
- response ACK sequence matches the admitted PEER request.

With no opened HOST-source section the HOST responds with the one-byte
`HOST_PEER_ACK_STATUS`; HOST independent send remains forbidden. Scheduler
route metadata carries no expected-response contract, so this section is
admission authority and not carrier policy: which requests receive sections and
which receive the bare ACK is canonical in `era_route_contract.md`'s **One
carrier for the response section set** — a request from core0's matrix-push
lane is answered with the bare ACK — and which sections may open at all is the
eligibility table in `era_wire_contract.md`.

## Relation Hold

**Both halves reporting no host is the absence of the fact that assigns the
roles, not a fact that unassigns them.** Every relation rule derives the pair
from a half that owns a USB session — local-open with peer-open is DUAL-HOST,
local-open with peer-no-host is HOST-PEER HOST, local-no-host with peer-open is
HOST-PEER PEER — so with no such half there is nothing left to re-decide, and
the relation the pair already has is held rather than ended.

The hold cannot strand a role, because nothing in that state can claim one: a
PEER with no host of its own does not become the HOST because the HOST's host
went away. It ends when a half actually enumerates, which lands on one of the
rules above and re-roles the pair in a single plan, or when the peer goes stale.

**Assignment, hold, and staleness are one transition function, not rules and
an exception.** Every arm of the planner has the same shape — the next relation
is a function of the current relation, the authority pair, and wire liveness.
The naming arms ignore the current relation, because a pair that names a
session-owning half re-roles the pair outright; the hold arm returns it,
because its state names nothing that can claim a role; staleness resets the
fold, because a relation is held about a peer, not about a memory of one. The
relation is therefore a fold over authority *edges* and never a reading of
instantaneous authority, and everything downstream of it is a projection of
the fold's one result — the initiator table below is the first of them.

**A held relation keeps its standing exchange, priced rather than
incidental.** Core1 keeps exchanging at the relation's own cadence for the
whole no-host span — 10 ms in HOST-PEER, 1 ms in DUAL-HOST, indefinitely if the
host never resumes, so a soft-off computer with powered ports holds the
exchange all night where the pre-hold image decayed to bootstrap probes. The
wake this rule exists for fires on state only that exchange produces.

**What it buys**: the PEER-key wake in HOST-PEER — the composed-matrix input the
wake fires on exists only while the exchange runs — and a teardown-free resume
in both relations, with no relation rotation and no seven-domain audit sweep per
sleep. **Where the cost lands**: core1 and the wire alone, beside a half that
keeps scanning its matrix at ≥18 kHz through the same suspend, so it is not the
sleeping pair's dominant power term. The grace close, the silence watches, and
the cadences keep their existing owners.

**A ≥100 ms wire outage, or a core1 death, inside a held span costs the rest of
that suspend its PEER-key wake.** The silence watches tear the pair down to a
quiet `LOCAL_NO_LINK` hold — the held exchange itself feeds both watches every
period, so this corner needs a physical wire or core event and never scheduling
jitter — and with neither half hosted, nothing re-forms until an authority edge
re-roles the pair in one plan at resume. HOST-key wake survives throughout: it
rides the local composed edge, not the relation. This equals the pre-hold steady
state, reached only through a fault that also degraded the pre-hold image.

**A sustained suspend is the ordinary way into it.** The reducer's grace close
is deliberate and is not what changes here; what changes is that it no longer
takes the relation with it. Before this rule, a computer going to sleep cost the
pair a full teardown — LOCAL_NO_LINK, a forgotten peer session, a wire-role
change, and a bootstrap on resume — and cost its PEER's keys the ability to wake
that computer at all (**Local Authority Facts**, remote wake). It now costs no
mode change, no session forget and no wire-role change.

**The forced peer-session stale recovery is scoped to the same distinction**
(**Revalidation Authority**), and matrix admission reads the relation rather
than the local host-open (**HOST-PEER Matrix Admission**). Those three are one
rule read at three sites; a change that re-derives any of them from
`accepted_host_open` reopens this section.

## Initiator Authority

**The initiator is a projection of the relation and not a second derivation of
it.** The table below is decided with the relation, from the relation, in one
plan. Deriving it separately from the same authority pairs agreed only while the
relation was a pure function of those pairs, and a held relation is not one — a
Left HOST would take the initiator role from under a PEER that is still pushing
to it.

| Relation state | Initiator | Responder | Matrix route |
| --- | --- | --- | --- |
| Peer unknown | Left only | Right | `SESSION_STATUS` only |
| HOST-PEER | PEER | HOST | PEER source-push only |
| DUAL-HOST | Left | Right | forbidden |
| Right standalone/no peer | none | optional responder | none |

The matrix column is the matrix route alone. The same table governs the runtime
route with the same assignment and one difference worth stating: HOST-PEER runs
it as PEER push / HOST response, DUAL-HOST as Left push / Right response, and
peer-unknown and standalone run it not at all. Which *sections* each relation's
runtime carries is not authority and is not here — it is the eligibility table
in `era_wire_contract.md`.

## Revalidation Authority

- **`SESSION_STATUS` is the discovery, bootstrap and recovery frame.** It is
  not a sync child route, and it carries neither role revalidation nor
  policy/generation revalidation: those ride each relation's own lane, in the
  AUTHORITY wire section (`era_wire_contract.md`).
- **An authority edge *inside* a relation whose lane carries the section
  raises no pending revalidation**: the section already moved the fact in both
  directions, so the frame would re-send what the peer holds. The frame has no
  cadence either.
- **Two edges keep the frame**: a relation that **changed** — a different lane,
  different eligibility, different roles, and this frame is what re-decides
  them, which is why the role handover runs this path — and **staleness**,
  because this frame is the recovery target and a doubtful relation is exactly
  the one whose own lane may be carrying nothing. Peer-unknown bootstrap is
  neither, and runs the frame unchanged.
- **Both directions of every serviced relation carry the AUTHORITY section,
  and the response direction is the one with no alternative**: an initiator
  announces its own authority change by raising a pending revalidation, which
  it can send, but a responder cannot initiate at all. The push direction is
  what makes the lane the carrier of record rather than a second opinion, and
  its overlap with the recovery frame costs one seven-byte section per
  authority edge, not a period. Section body, eligibility and deferral order
  are canonical in `era_wire_contract.md`.
- **The authority poll's period is derived, not chosen.** The reducer stamps
  the last observed change of the USB SOF counter and calls the host present
  while that stamp is under `ERA_SPLIT_AUTHORITY_SOF_FRESH_MS` (10) old.
  `ERA_SPLIT_AUTHORITY_POLL_PERIOD_MS` is therefore *defined to* that window —
  the longest interval that cannot step over it — and a `_Static_assert` that
  the period is `<=` the window holds it there. Detecting a stopped counter
  costs the window plus up to one period, 10–20 ms. Any value comfortably under
  the counter's 2048 ms wrap would detect the stop; what the derivation buys is
  that the period cannot drift away from the window it serves.
- Heartbeat/source-push liveness does not carry session facts, but accepted
  relation traffic may refresh liveness for stale detection.
- `local_status_pending` MUST be cleared by successful `SESSION_STATUS`, not by
  HOST-PEER ACK alone.
- Relation fact staleness or payload validation failure returns to
  peer-unknown `SESSION_STATUS` bootstrap/backoff.
- When local authority changes from HOST-open to no-HOST **and the peer is
  hosted**, the planner MUST NOT continue using stale both-hosted peer-session
  facts to choose an initiator. It must force peer-session stale recovery so
  discovery returns to the peer-unknown LEFT-initiator/RIGHT-responder
  bootstrap ordering. There the close genuinely re-decides the wire: this half
  goes from responder to initiator, and the ordering has to be re-derived.

  **Against a peer that is unambiguously no-host it MUST NOT force it**, and the
  qualifier is the rule rather than an exemption from it. That peer was the
  relation's initiator before the close and still is, and it cannot become the
  HOST by an edge on this half, so there is no stale choice to recover from —
  forcing one forgot the peer session and tore down a HOST-PEER relation whose
  roles were never in doubt, once per computer sleep (**Relation Hold**). A
  peer-unknown or otherwise unusable peer session keeps the forced recovery,
  because neither is a relation worth holding.

## Authority Scheduler Boundary

- SOF count movement by itself is not scheduler dirty state.
- SOF loss and recovery are deadline/due conditions. **This is the
  architecture's one poll and it may not be event-driven.** The counter is a
  1 kHz host heartbeat with no loss interrupt, so its absence is observable
  only by noticing the count stopped; a firmware that event-drives this
  believes a dead host behind a live cable is still a host. Every other
  boundary publishes on change and is consumed on an edge
  (`era_overview.md`), and this exception is named rather than tolerated.
- Reduced authority result changes mark authority dirty for scheduler planning.
- Raw authority, SOF, or USB host-state facts must not be pushed to the peer.
- SOF loss during USB suspend must not collapse an already accepted local HOST
  relation before the local suspend grace window expires; the three closes that
  do return to `accepted_no_host` are in Local Authority Facts above.
- Suspend close is based on USB stack state plus the reducer's bounded suspend
  duration, not raw VBUS.
- Scheduler builds require QMK's USB suspended startup loop to stay disabled so
  no-local-USB PEER runtime continues running `keyboard_task()` (`quantum/keyboard.c`).
