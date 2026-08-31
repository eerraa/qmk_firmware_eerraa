# ERA Capture Reading

Genre: manual
Canonical for: decoding a `WIRE_DIAG` console line — every field's semantics,
line pacing and sample skew, which counters are totals against deltas against
rates, and the reading traps that decide whether a reading is valid at all

Identifiers (mode, route, payload, status, VIA value ids, retired numbers) are
`maps/era_identifier_map.md`. Comparison figures are
`manuals/era_performance_gates.md`. This file is the printed field and how it
may be read.

## How A Capture Is Sampled

### Line pacing and sample skew

One `WIRE_DIAG` press freezes a snapshot per family and then emits one line
every `ERA_SPLIT_WIRE_DIAGNOSTICS_LINE_INTERVAL_MS` 150 ms.

| Line family | Sample instant |
| --- | --- |
| scheduler, authority, eeprom, storage | the frozen capture; one instant |
| `ccore`, `cqueue`, `cown`, `csess`, `csp`, `crsp` | re-sampled at print; each is that line's index times the interval after the freeze |

`ERA_SPLIT_WIRE_DIAGNOSTICS_SCHEDULER_LINES` is `N` and is deliberately unstated
here — take it from the enum in `split/diagnostics/era_split_wire_diagnostics.c`.
The six communication-core lines occupy the block `N` through `N` plus five.
**Never take a delta across the two classes.**

### Cumulative counters are totals, not rates

`csp sub`, `crsp pub`, `csess sub`, `sess`, `hkwork`, `owner` and `txrx` are
free-running from boot. A rate is the difference between two captures divided
by the elapsed time between them, never a single capture's counter over its
uptime.

**The elapsed time is `last` on `wire pc`, in milliseconds, and it is not
`raw_us`.** `raw_us` is `raw_matrix_read_us_total` (front of the scan only).
**Divide by the `last` delta.** `raw` delta divided by elapsed seconds must
reproduce `scan_hz`. `raw_us` falling across two captures is the reboot
detector — no delta spans a reboot. `core_free` is the manifest's
`ram0_free_bytes` minus an image-generation alignment (recorded pairs 8 and
4). **Record the pair per image set.**

### Fields that do not answer the question asked of them

| Field | Does not answer | Read instead |
| --- | --- | --- |
| dump with `storage active=1`, or `open` ahead of `close` | end-of-operation; `fall` is the previous span (`spans` already rose) | the other half's span, or dump after close |
| "HOST-PEER exchange rate" on the HOST | the initiator rate (initiator is the PEER; no console) | HOST `crsp prep` (or `quiet` + `resv`) increase rate |
| `eeprom pol req=` | INPUT or RGB policy | EEPROM bit only; other two from VIA, behaviourally |
| per-half policy bit while the half is PEER | that bit on the PEER | stage both bits from DUAL-HOST first |
| `wire hp peer_tavg` | a maximum | `n`, timeouts, means, last sample only |
| visual baseline / lock indicator | a counter | the LEDs; a capture is itself a wire interruption |

**Name a rig by its relation — HOST-PEER or DUAL-HOST — never by a shorthand.**

### The capture is taken by pressing a key

`WIRE_DIAG`, `WIRE_DIAG_2` and `WIRE_QWIN` are declared in
`keyboards/era/sirind/common/tomak_common.h`. Bind, restore, and the rule that
shipped keymaps bind none of them live in `era_performance_gates.md`'s **The
Instruments Are In The Tree And Off In Release**.

The snapshot is taken inside `process_record_kb` in `keyboard_task()`
(`quantum/keyboard.c`), which the main loop runs before `housekeeping_task()`.
**The first press after an enforced silence is a valid pre-trigger reading of
that silence.** The capture keycode is itself a settled-capture trigger, so
`open` moves in every bracketed delta; for "no key was touched", **`xfer` is
the discriminator, not `open`.**

### Role-era lines are deltas, not totals

`hp peer_era`, `hp host_era` and `dh era` reset on every capture:
`split/diagnostics/era_split_wire_diagnostics.c` calls
`era_split_transport_scheduler_reset_diagnostics_era_baselines()`
(`split/scheduler/era_split_transport_scheduler_diagnostics.c`) at the end of
the snapshot, before print. **Never difference two captures' `_era` lines.**

They are role-scoped (only while this half is in the owning mode). `sess`,
`txrx` and `csess` mix roles. `meas=1` means both boundary reads of the
published transaction-engine mirror were proven. **`meas=0` shows as an exact
zero, never as an undercount.** Under the constant DUAL-HOST poll, initiator
`meas=0` is ordinary; take the wire count from the responder's `dh era io`
(`bem` corroborates; `csess` and `crsp prep` too). Judge every poll-driven
counter against the current periods in `era_route_contract.md` — no second
table of those periods lives here. `stor` and `rt` never go through the
mirror.

`mode=` is `last_mode`, not necessarily current. **Read `rel=` on `wire pc`.**
DUAL-HOST owns an era block; `LOCAL_NO_LINK` (`era_identifier_map.md`'s
**Modes**, value 0) owns none. `DUAL_HOST_LEFT` is 3, `DUAL_HOST_RIGHT` is 4.

### The live CLEAN phase line

`wire clean ev=… t=… i=… l=… lp=… pp=… pa=… ap=… dl=… cq=… hs=…` prints at the
transition from `split/era_split_restart_agreement.c` so the reset cannot
erase it. Acceptance lives in `era_performance_gates.md`'s **EEPROM CLEAN
agreement device gate**.

| Field | Meaning |
| --- | --- |
| `t` `i` `l` `lp` `pp` `pa` | local timer; local-initiator; local-Left; reboot-durable PREPARED; exact peer AUTHORITY/PREPARED match; cached peer phase |
| `ap` `dl` | RESTART_ARM `0` idle / `1` PREPARE / `2` COMMIT; candidate or adopted shared deadline |
| `cq` | `claim/tx/rx/publish/fail/request-claim/result-claim/result-ready` (boot-cumulative) |
| `hs` | `xfer/apply/complete/abort/timeout/active` (boot-cumulative) |

| `ev` | Event |
| --- | --- |
| 1 | REQUEST advertised |
| 2 | initiator selection/PREPARE |
| 3 | responder PREPARE receipt |
| 4 | reboot-durable local PREPARED |
| 5 | prepare failure (terminal for that boot: no 6–10 may follow; storage stays quarantined) |
| 6 | initiator COMMIT publication |
| 7 | responder COMMIT adoption |
| 8 | initiator COMMIT_ARMED echo adoption |
| 9 | unanswered-COMMIT disarm |
| 10 | reset commit (may remain buffered; corroboration, not required) |

Healthy serviced CLEAN: event 4 on both halves before any event 6; initiator
then 6 then 8; responder sees 7. Events 4/6/7/8 occur before the deadline.
From event 4 through the last pre-reset event, all eight `cq` values and all
six `hs` values stay unchanged; the three generation fields and `active` are
already zero at event 4.

## The Relation's Own Counters

`wire dh era valid=%u mode=%u io=tx/rx/miss/bad/fail stor=open/close/xfer
rt=tx/rx meas=%u`. `io` is compact wire traffic; `stor` must not move across a
window with no settled config change; **`rt` is runtime sections** (`tx`
sent, `rx` accepted). `rx` counts arrival **before any policy gate**.

**There is no periodic `SESSION_STATUS` in a serviced relation.** A standing
exchange that stops costs one session revalidation (`era_route_contract.md`'s
**The standing exchange grant**). `csess sub` counts those stop edges — read
it beside the initiator's transaction-failure counters; equal deltas are
designed, a `csess` that outruns them is a finding.

`sfg` on `wire sess` is teardown, not `csess sub`'s neighbour:
`era_split_scheduler_session_forget_peer_from_scheduler()`
(`split/era_split_scheduler_session.c`) zeroes the peer block and clears
`peer_known`. One call site, from the mode planner's
`peer_session_forget_required` in `split/era_split_transport_scheduler.c`,
raised from `secondary_stale` alone. A step with nothing beside it is the
first suspect for a dead core1.

**`io` is not a silence instrument in DUAL-HOST.** The runtime lane polls
unconditionally; **a rising `io` on an idle pair is the mode working.**
Silence questions ride `stor` and `rt`. The `meas=1` rule applies to `io`
alone.

**`rt` counts sections, never polls**, net of the 60 s anchor
(`ERA_SPLIT_TIME_ANCHOR_REFRESH_MS` 60000): a settled window longer than that
reads one `rx` per refresh (`sec=` carrying `0x80`) while `tx` stays 0. Read
silence from a *settled* window. A frame carrying two sections counts two.

| Direction | Section set |
| --- | --- |
| responder `tx` | INPUT_LAYER, AUTHORITY, RGB_STATE, ACTIVITY (`split/scheduler/era_split_transport_scheduler_responder.c`) |
| initiator `rx` minus responder `tx` | TIME_ANCHOR, STORAGE_NEWS, VISUAL (rx-only; not loss) |
| initiator `tx` and responder `rx` | INPUT_LAYER, STORAGE_PENDING, AUTHORITY, RGB_STATE, ACTIVITY, VISUAL (deltas match) |

Lock does not cross this lane — `ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_RSP`
(`split/era_split_wire_protocol.h`) omits it. There is deliberately no defer
field on `rt`.

## Scheduler And Storage Lines

`WIRE_DIAG` only; never automatic. Five storage lines without the cause
selector; nine when `ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE=yes`
(`build_variants/cause.mk`).

| Field | Meaning |
| --- | --- |
| `st` | raw `era_host_peer_storage_runtime_state_t` index in `split/era_host_peer_storage.c`. `0` IDLE, `7` HOST_READY (after push apply), `9` HOST_CLOSED (after proof/match close). Initiator idles at `0` with `dom=255`. **A responder never reads `st=0` in a confirmed relation.** |
| `open/close/abort/restart` `proof/match/xfer` `chunk/dup/retry/timeout` | episode lifecycle (`restart` is Core1 restart around a durable apply, not a failure). **`close + abort == open` is initiator-only.** **`proof` counts `PROOF_RSP`, not relation-open sweeps.** Chunk and retry progress. |
| `apply/complete` `stale/full=i/r` `source` `integrity/version/domain` `qfail` | durable ERA NVM replacement and pair-visible completion. Failed NVM leaves both unchanged. After NVM success there is no rollback (`era_host_peer_storage_contract.md`'s **Replacement Apply: ADMIT And Public Authority**): `apply` without later `complete` is post-durable work. Generation reject; PEER-initiator / HOST-responder result-capacity failure — responder `full` moving on a healthy push apply is a real fault (`era_host_peer_storage_contract.md`'s **Capacity And Publication**). Saturated source-supersession (`SOURCE_CHANGED` or local target-dirty abort). Exact image/schema/domain rejects. Core1 quiesce/restart failure (not rollback; after `apply` advanced, NVM is already canonical). |
| `news/probe` `df` on `eeprom pol` | this half's news value and initiator pending-probe mask. `news=` on `csp` is a different field. `newsn` on `csp` **must read zero**. Sync-policy change log (`era_identifier_map.md`'s **USB Session**). |

All-pull sweep: initiator `proof=14 match=7 retry=7`, responder
`proof=7 match=7 retry=0`. All-push: both halves `proof=0 match=7`, initiator
`retry=7`. Mixed is the sum. `BUSY` is not a failure.

`news` is a forward-only counter, wraps `1..7F`, `00` means nothing to claim
(`era_host_peer_storage_contract.md`'s **Storage News Value And Relation-Open
Audit**). **`news` and `chg` answer different questions** — a reverted edit
still steps `news` while `chg` reads `00`.

`sec=` on `wire txrx` / `wire hp peer_txrx` is the last timed transaction's
`response_section_byte` (`split/diagnostics/era_split_wire_diagnostics.c`).
Read it as flags only when the same line shows `rsp=2/9`; otherwise `sec=00`
means "not a flags byte". Using `sec=` as image identity cost a gate run —
never. Reserved `SESSION_STATUS` bits and section-body layouts are
`era_wire_contract.md`'s **SESSION_STATUS** and **`HOST_PEER_HOST_SOURCE_RSP`**.

| Kind | Bits |
| --- | --- |
| `SESSION_STATUS` flags, reserved-zero (frame refused) | `0x04`, `0x08`, `0x20` |
| section mask | `0x01` layer, `0x02` rsp ACTIVITY, `0x04` AUTHORITY, `0x08` lock, `0x80` anchor; push ACTIVITY `0x10` |
| `df` FIELD | `0x01` EEPROM, `0x02` INPUT, `0x04` RGB. The same `0x04` in `chg`/`probe`/`psh`/`cfl` is the macro domain by coincidence |

A lock-only response prints exactly `08`. As a section mask it is a pure mask.

`pnews` on `wire sess` is the peer's news value bits only (`mir` is the pending
flag). A DUAL-HOST Right reads `00` structurally. **Read it only against the
peer's `news`, and only for equality.**

| `recency` | Meaning |
| --- | --- |
| `ok` | baseline-record guard validity |
| `chg` | per-domain changed mask (all-ones while `ok=0`) |
| `cnt` | seven divergence counters in domain-id order |
| `arb` | arbitration flags / peer's last declared changed mask |
| `psh`/`cfl` | push and conflict cell queues |
| `prv` | local-changed/relation-cell excluded from the indicator, never from arbitration |
| `cfm` | `1` = a `TRANSFER` or retry fault promoted the initiator round |

Rules: `era_host_peer_storage_contract.md`'s **Recency Layer**.

| `arb` | Status |
| --- | --- |
| `0x01` | live: summary-done (stays off-role) |
| `0x02` | live: summary-pending (responder must clear) |
| `0x04` | live: verify-all round (responder must clear) |
| `0x08` / `0x10` / `0x20` | retired, reserved un-reused (comments, not names, in `split/era_host_peer_storage.c`) |

Format is `arb=%u/%02X`: `arb=16/00` is flag `0x10` alone; `arb=21` is `0x15`,
not `0x21`. Off-role, `arb` is history of the last relation *this half
initiated*. **A responder reading `0x02` or `0x04` is a finding**; a lit
`due=` on a responder is not expected. `probe`, `psh` and `cfl` are drained,
not accumulated — `cfl=00` does not mean no conflict ran. `probe=` on `wire
storage` reads `00` on HOST (queues released every pass).

**No document records an expected table for the relation-open signature.** It
has been mis-scoped once; compare only same role and same relation.

### The EEPROM SYNC indicator shim

| `ind` | Meaning |
| --- | --- |
| `vis` `pnd`/`mir`/`gate` `hold` | lamp as rendered, pending held to `ERA_SPLIT_EEPROM_SYNC_MIN_VISIBLE_MS` 160; local pending arm / peer advertised mirror / cached serviceability-and-policy gate (`pnd` is display-filtered; `prv`-covered bits do not raise it until `cfm=1` or a transfer retires the active domain's provisional bit); local arm has fallen but its zero is not yet confirmed on the active carrier (`STORAGE_PENDING` initiator, `STORAGE_NEWS` bit7 responder) |
| `spans` `rise`/`fall` | rising edges of the pending fact; local-uptime ms of the latest span's first and last pending pass |

Predicate: `era_host_peer_storage_indicator_pending()` in
`split/era_host_peer_storage.c`. A responder outliving the initiator by more
than about one poll period plus a housekeeping pass is a mirror-path finding.
**A dynamic-macro upload is visible before it is durable**
(`era_host_peer_storage_contract.md`'s **Dynamic Macro Transaction**). **The
line is domain-blind** — per-domain facts stay on `chg` / `xfer`.

| `led` | Meaning |
| --- | --- |
| `rn` | red-era count (panel truth: every flushed STATUS frame, zero-flag included) |
| `ron`/`roff` | first STATUS flush of an era / first normal flush after it |

`rn == spans` is clean; `rn > spans` with visuals clean is a healed mid-era
repaint (bracket with `roff`/`ron`); a visual gap with `rn == spans` leaves
only a black STATUS frame. Render-policy wake on a TOMAK STATUS-policy edge
lives in the board RGB render path; a large `ron`/`roff` lag behind
`rise`/`fall` (clock-offset corrected) is a render-path finding.

| `brk` | Meaning |
| --- | --- |
| `count` | last frame that broke a red era while the lamp was still commanded visible; **must equal `rn - spans`** |
| `flags` | raw render flags (`00` = zero-flag NONE/suspend fill) |
| `state` | bit0 RGB enabled, bit1 suspended, bit2 arbitrated status policy on |
| `brkms` | same instant `roff` stamps |

`slp=count@ms`: rising edges of the resolved lighting-sleep decision since
boot. `delta slp=0` across a breaker-showing operation excludes the sleep
path. Boot or an earlier link Apply may leave a nonzero idle count.

**The lamp is one per operator action, not one per transfer.** It follows
unfinished pair work: abort takes the wire and moves nothing; a retrying
failure stays lit; lit with `pnd=1` while `xfer`, cells and `chg` are frozen
is a changed-shadow finding. A CLEAN/fresh first audit that closes all seven
`MATCH` shows no span.

**`excl=` reads 0 through an apply phase** — exclusivity ends at
transfer-verified. Bracket the ERA NVM call with cause `EEPROM_BEGIN` /
`EEPROM_END`; Core0 does not return to the keyboard loop inside that bracket,
so do not infer scan or core0-route movement from a missing intermediate
sample. `era_performance_gates.md`'s **ERA NVM persistence device gate**
demands wall-clock time and standing liveness across that window; it does not
publish a measured NVM-call width, and this file does not invent one.

### The storage core and its cause timeline

| Line | Fields |
| --- | --- |
| `core cl/tx/rx/pub/fail` `gen=req/result/ready` | Core1 initiator claim / wire-TX / response-RX / result-publication / failed-stage (one-shot BUSY handoff); current claim generations |
| `last=` / `flast=` | last Core1 initiator boundary; most recent *failed* boundary (survives later success), plus `class/access` |
| `ddu` `qctx=delay/window` | signed µs from the request's not-after at failure (`-2147483648` = no deadline); publish-to-failure-detection residence and publish-to-not-after window (µs). BEGIN expiry: `delay - window == ddu`. `4294967295/-2147483648` = no published context |
| `psvc=valid/kind/reason/result` `span=start/end/gap` | most recent completed timed route at the failure (`split/era_split_transaction_engine.c`); route-start/end minus publish; failure-detection minus route-end. Sentinel `-2147483648` |
| `fid=owner/relation/transaction/request/domain/detail` | failed publication. `op=EE` (`PUSH_CTL_REQ`) detail `OPEN/APPLY/COMPLETE/ABORT=0/1/2/3` |

| Axis | Values |
| --- | --- |
| probe stage | `NONE=0` … `PUBLISH=7` (CLAIM, BEGIN, ENCODE, TX, RX, CONTRACT) |
| result | `NONE/OK/MISS/BAD/FAIL=0..4` |
| failure | `NONE/QUEUE_EXPIRED/OWNER/EPOCH/CANCEL/RESET/PIO/SEND_TIMEOUT/RESPONSE_TIMEOUT/PARTIAL/IO/DECODE/RESPONSE_CONTRACT=0..12` |
| class | `INVALID/READY/RESULT_FULL/QUEUE_EXPIRED/OWNER_STALE/RELATION_STALE/GENERATION_STALE/POLICY_STALE/TRANSACTION_STALE/CANCELLED/RESET=0..10` |
| access | `OK/OWNER/EPOCH/CANCEL/RESET=0..4`; `255` = axis absent |
| `psvc` | `kind=1` standing; reason `3` section push, `4` response poll |

`qctx` window at the default `ERA_SPLIT_PEER_RESPONSE_WINDOW_MS` 20 is
25000 / 45000 / 85000 us on High / Medium / Low (configured window at the
current scale plus 5 ms compact-TX/handoff).

`wire storage cause` when `ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE=yes`
(`build_variants/cause.mk`): `role=1/2` PEER/HOST, `dom/gen` the transfer,
`n/ov` retained count/overflow, `stale=age/limit` first responder-stale.
Starts when a `TRANSFER` is accepted. Apply bracket lands on the applying
half; push initiator is begin-sparse. Status ids:
`era_identifier_map.md` (cause-timeline decode).

| `ev` high nibble | Event | Low-nibble detail |
| --- | --- | --- |
| `1` | `CHUNK_RESULT` | the sixteenth, not the chunk id (`1X` = chunk `X × 16`) |
| `2` | `APPLY_READY` | storage status id |
| `3` | `APPLY_BEGIN` | — |
| `4` | `EEPROM_BEGIN` | start of the one synchronous `era_nvm_replace(... REMOTE_APPLY)` (`storage/era_nvm.c`) |
| `5` | `EEPROM_END` | NVM result code. Success: NVM is authority; failure leaves the public range old |
| `6` | `CORE1_RESTART` | success is `1` |
| `7` | `SESSION_SUBMIT` | prior peer-known |
| `8` | `SESSION_RESULT` | transaction result `1..4`, or `5` OK-without-valid decode |
| `9` | `SESSION_RX` | bit 3 response-sent, bits `0..2` result |
| `A` | `RESPONDER_STALE` | — |
| `B` | `SESSION_FORGET` | — |
| `C` | `REVALIDATED` | — |
| `D` | `COMPLETE_SUBMIT` | — |
| `E` | `COMPLETE_RESULT` | storage status id |
| `F` | `ABORT` | storage status id |

Sampling is 1/16 because an unsampled stream fills the 32-entry ring before
apply/rotation/abort. Timestamps are 16-bit ms; a byte saturates at 254 ms.

`wire storage edge` is cause-only, interval-scoped; a `WIRE_DIAG` snapshot
copies it and resets the interval in
`split/diagnostics/era_split_wire_diagnostics.c`. Promotion does not reset it
— only storage init and a local snapshot do
(`split/era_host_peer_storage.c`). Do not reboot the promoted half (RAM
state). A storage-triggered fast revalidation is not service leave: healthy
recovery ends by carrier fall (event 4), not event 7.

1. DUAL-HOST: idle baseline on each half → one macro write, no intermediate
   press → wait for lamps to fall → result snapshot on each half.
2. HOST-PEER: attach USB to the PEER, promote, take its baseline; detach, wait
   until HOST-PEER is restored and both lamps are dark; take HOST baseline;
   one write; wait for final lamp fall; HOST result first; then attach USB to
   the former PEER, promote, take its result. Trailing `service leave` after
   final indicator fall is extraction context; before an in-operation fall it
   is a finding.

| Field | Meaning |
| --- | --- |
| `wr` | dynamic-macro EEPROM-change notification count. Not a RAW-HID request counter: `nvm_dynamic_keymap_update_changed_runs()` (`quantum/nvm/eeprom/nvm_dynamic_keymap.c`) notifies only when a request differs from cache; the durable marker adds a trailing notification |
| `at=first/last` | those offsets from the baseline, ms |
| `gap=last/max` | last and largest inter-notification gap |
| `oq` | gaps ≥ `ERA_HOST_PEER_STORAGE_DIRTY_QUIET_MS` 1000. Cannot by itself prove VIA paused |
| `n/ov` | retained edge count / overflow. Receiver normally `wr=0` |

Each `ev=` is `event/arms/rs/domain/dirty/changed/generation@ms`. 64 events;
timestamps saturate at 65535 ms (`split/era_host_peer_storage.c`).
`rs >> 5` is role (`1` PEER, `2` HOST); `rs & 1F` is runtime state.

| Event | | `arms` | |
| --- | --- | --- | --- |
| 1 | advertised rise | `01` | dirty |
| 2 | advertised fall | `02` | visible changed shadow |
| 3 | mirror rise | `04` | visible content-moving cell |
| 4 | mirror fall | `08` | visible summary |
| 5 | indicator rise | `10` | visible content expected |
| 6 | indicator fall | `20` | moving span |
| 7 | service leave | `40` | peer mirror |
| 8 | changed-shadow rise | `80` | service/policy gate |
| 9 | changed-shadow fall | | |
| 10 | dirty rise | | |
| 11 | dirty fall | | |

`wire via macro`: same snapshot/reset boundary. `rx/rsp/dr` are set-buffer
receives, generic VIA responses, and RAW IN drain epochs (`dr < rsp` only when
the host pipelines). `at`/`gap/oq` at actual request entry — this `oq` is the
one that establishes a 1000 ms crossing. `h`/`send`/`drain`/`app` are
total/max ms (handler, `raw_hid_send()`, endpoint-idle, idle-to-next);
`int` excluded intervals; `ovl` overlapping request; `p=1` undrained response.
Seams in `quantum/via.c`; state in `split/diagnostics/era_via_macro_diagnostics.c`.

`wire storage ppath`: cause-only `STORAGE_PENDING` carrier timing, ms from the
same interval; `65535` = edge not observed. Each `rise/fall` is one ownership
boundary: `pub` Core0 publishing a changed standing plan, `tx` initiator Core1
completing the exchange, `rx` responder Core1 decoding, `app` responder Core0
applying to the peer-pending mirror. Third `pub` field is the **fall** context
mask: bit0 plan enabled, bit1 `SESSION_STATUS` revalidation pending, bit2
storage route exclusive. The four-stage clock is the RP2040 free-running
timer, not `timer_read32()` (`platforms/chibios/timer.c`). `app(fall)` should
coincide with edge event 4. For a carrier-caused blink: source event 2, then
receiver 4, then receiver 6.

## The qwin Window

`WIRE_QWIN` starts the silent window on the first press and emits one line on
the second in `ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE=yes` builds
(`build_variants/qwin.mk`).

| Field | Meaning |
| --- | --- |
| `pc/ms/scan_hz/raw/start/end` | print count, elapsed, computed scan rate, scan-count delta, start/end counters. **`scan_hz` is core0's pass rate; `smp_hz` is the sampler.** |
| `ccore` `sp` `sess` | loop/idle/wake/wake-observed deltas; core1 HOST-PEER source-push transaction delta (was `hbsp` until heartbeat retired); production CORE1 `SESSION_STATUS` transaction delta, separate from `sp` |
| `park=count/us` | backend in-transaction parks (`era_split_transaction_backend_park_until()`, `split/era_split_transaction_backend_rp2040.c`) plus the loop idle park; microseconds asleep |
| `settle=ms` `smp`/`smp_hz` `ovr` `rearm` `fd1` `seg=` | start-sample deferral (`ERA_SPLIT_QWIN_SETTLE_MS` 0 = at first press); PIO sampler frames/rate (a `rearm` inside the window makes `smp` meaningless); DMA-lap re-reads (structurally zero here); DMA re-triggers after transfer count ran out (~93 min/channel); PIO1 FDEBUG bits 3..0 TXSTALL:TXOVER:RXUNDER:RXSTALL; scan rate of each `ERA_SPLIT_QWIN_SEGMENT_MS` 10 s slice, up to eight (`-` if shorter) |

**A park is credited entirely to the window it ends in**, so `us` may exceed
the window. A no-cable responder's idle first-byte window is
`ERA_SPLIT_RESPONDER_FIRST_BYTE_TIMEOUT_MS` 60000
(`split/communication_core/era_split_communication_core_responder_service.c`).
**Take a sleep share only where the parks are short against the window.**

Device shapes: a 60 s responder window is **exactly two parks of 29,999,997
µs**, not one of 60 s. An idle no-link responder never reaches its own idle
park: `ccore` shows `loop` +1–2/min against `idle=0`.

`ccore` is how a capture says which relation it was taken in — sitting labels
have been wrong.

| Shape | Relation |
| --- | --- |
| `loop` ≈ 100/s, **`idle` = 0** | HOST-PEER, this half the HOST (wire responder; idle park unreachable) |
| `loop` ≈ 2,955/s, `idle` ≈ 1,970/s | DUAL-HOST, this half the initiator |
| `loop` ≈ 6/s with `sess` = `loop`/3 | no cable — discovery backoff only |

Periods that produce those rates are `era_route_contract.md`.

The reactive hit tracker lives `UINT16_MAX` milliseconds (65.5 s). Every
reactive-effect window steps once at 65.5 s; its tail is the hit-free rate. On
a non-reactive effect a step is a finding. **A key-quiet qwin figure
understates a fix that sits in the reactive path** — recorded case: qwin
+14.1 % key-quiet vs the `wire` variant's HOST-PEER typing **+21.7 %**. Quote
both when a change touches anything the hit tracker feeds.

### The `qwin_phase` rung

The rung's `scan_hz` is not a comparison point. Price is measured per sitting
against a plain `qwin` window of the same length — about **4 µs a pass**.
Subtract that sitting's own measurement.

`ph=passes us=r,d,x,s,f,a,q,g,t,l,h,e`: twelve segments in execution order
RAW, DEB, XPORT, SCANHK, DIFF, ACT, QTM, RGB, KTAIL, LOOP, HK, REST. Names and
marks: `system/era_pass_phase_diagnostics.h`. What each segment runs:
`era_walkthrough.md`'s **1. One pass of the keyboard loop**. **Divide by
`ph`, never read a figure raw**; take a window long enough to hold millions of
passes.

`pmax=us mx=…`: the window's **worst whole pass** and worst each segment
contributed. **Absolutes, not deltas** — cleared when the window opens.

`over=n1,n2,n3`: passes that ran `ERA_PASS_PHASE_BAND1_US` 50,
`ERA_PASS_PHASE_BAND2_US` 100, or `ERA_PASS_PHASE_BAND3_US` 200 or longer —
deltas; each band is a subset of the one before. Add the instrument's own
price when a population sits near an edge.

With RGB off, both halves put about **100 passes a second** over 50 µs — the
scheduler's maintenance body at `ERA_SPLIT_AUTHORITY_POLL_PERIOD_MS` 10
(`split/scheduler/era_split_transport_scheduler_internal.h`). Cost varies
(tens of microseconds standalone vs ~154 µs as a HOST-PEER HOST); rate does
not. A LEFT/RIGHT gap in this field is usually the band edge: **read `hkmx`'s
middle column and the ≥50 count beside this one** before concluding the halves
differ.

| Field | Parts |
| --- | --- |
| `hk=f,s,t hkmx=f,s,t` | inside `era_split_keyboard_task()` (`split/era_split_keyboard.c`): `era_common_features_task()` (`system/era_common_features.c`), split scheduler task, once-per-millisecond gate. Fourth part is derived: `us[HK]` minus the three |
| `rgb=t,s,r,f,y rgbn=… rgbmx=…` | `rgb_task_timers()` and starting/rendering/flushing/syncing in `quantum/rgb_matrix/rgb_matrix.c`. At most one arm per pass — not exactly one. Idle gate (`RGB_MATRIX_IDLE_GATE_ENABLE`) returns before the timer head on a `SYNCING` pass whose millisecond has not turned over |

**Gated passes are `ph` minus the four arm counts.** The render arm's maximum
is the figure a core1 chunk bound is set from. `us[RGB]` still closes on every
pass (`quantum/keyboard.c`).

## The Key Path And The Scan Facts

`wire keypath n= avg= max= h=b0,…,b7`: PEER key-path span, microseconds,
core0 publish to core0 ACK-apply. Histogram upper edges 320, 384, 448, 512,
640, 896, 1408, then above (`split/era_host_peer_matrix_link.c:54`), centred
on the measured **402 µs source-push** (`wire hp peer_tavg sp`). **Only the
first half of this span is on the key's path.** **Read the histogram, not the
mean.** Cumulative; read after that half is given a USB host.

`wire qmk scan_hz/raw/raw_us/raw_max` (+ `smp_hz/ovr/rearm/fd1/fw` on the PIO
sampler): wire-variant scan facts. `raw_us`/`raw_max` bracket the raw read
(PIO: frame fetch and decode). `smp_hz` is the sampler frame rate between the
two captures; `ovr`/`rearm` are boot-cumulative; `fd1` stall flags since the
previous capture; `fw` slots per frame.

## Scheduler State And Maintenance

| Field | Meaning |
| --- | --- |
| `bem` `be` | transaction-engine mirror fresh/fallback-count (fresh `0` discarded a colliding candidate); backend initialized/init-role/current-role. Init-role is the handedness latch — LEFT `1` initiator, RIGHT `2` responder — and never moves. `init-role != current-role` is inverted. A forced core1 kill wedged core0 on exactly those two inverted combinations. Read the pair |
| `bri` `initwork` `hkwork` `plan`/`dirty`/`owner` `rsnp` | reinit-on-role-change; scheduler initialization work; housekeeping maintenance work; mode-planning runs / cached dirty flags / owner route step/kind/reason; responder-snapshot publish retries (small single digits per session is designed; a rate rising with typing is the publish fighting the claim window) |

`wire maint entry/stor/rsp/stand/init/time/mode/route`: `hkwork` by
contributor, order fixed by `ERA_SPLIT_SCHEDULER_MAINT_SOURCE_*` in
`split/era_split_transport_scheduler.h`. **Two names are traps:** `init` is
`CORE1_INITIATOR` (unrelated to `initwork`); `rsp` is the responder drain, not
a response count. `stand` is the standing state's apply. `entry - hkwork` is
passes that woke core0 and found nothing to do.

| `due` | Status |
| --- | --- |
| `0x01` | live: attach/status |
| `0x04` | live: HOST-PEER matrix source-push |
| `0x10` | live, not a route: standing-plan publication (`scan_idle()` reads this word) |
| `0x02` / `0x08` / `0x20` / `0x40` | retired un-reused (`split/era_split_scheduler_events.h`) |

`open_ms`: the two boot instants, ms since `timer_init()` at the top of
`keyboard_init()` (`quantum/keyboard.c`), at which the explicit core1 launch
step was entered and returned. Saturates at 65535. Boot-only; a nonzero delta
between two captures of the same boot is a reading error. On the half without
a cable it is the check that no boot-master poll runs inside
`split_pre_init()`.

### Runtime section counters

`wire sect tx/rx sync anc`: cumulative runtime section counts, printed in
every relation — the HOST-PEER half that cannot print `rt`. Same underlying
pair as `wire dh era rt=`; counted in sections, never polls.

`app=vis/rgb/auth`: distinct applied values, cumulative from boot. One
initiator apply path, so each value crosses once and applies once. `app=rgb`
has two producers (standing apply and DUAL-HOST push-receive); `app=vis` the
same two; **`app=auth` has one** (standing apply). The `rgbn` on `wire csp`
must read zero.

| Console | RGB / INPUT reading |
| --- | --- |
| sent but not applied | arrival moves `rt` rx; `app=rgb` / `lay` / `aap` freeze (receiver policy off) |
| didn't send | neither moves (sender bit off). `adv` may still tick — **read sends from `rt`/`atx`, never `adv`** |
| applied | `app=` / `lay` / `aap` move |

A one-poll RGB deferral behind a due AUTHORITY is designed order, not
starvation. In DUAL-HOST `app=rgb` counts configuration-only applies.
`app=vis` moves in DUAL-HOST too (visual cell diff-replay), behind the
receiver's RGB bit, at two per keystroke on the sending half. `lay` is the
INPUT-class apply leg inside the policy gate.

### The shared clock and the time anchor

`sync` is `sync_timer_read32()` (`quantum/sync_timer.c`), beside `wire pc
last=` (local). A half that has never applied an anchor reads them within the
print's own skew.

| Field | Meaning |
| --- | --- |
| `anc=applies/back/back_max_ms` | applies, backwards steps, worst backwards step (ms). **No document sets a numeric pass condition on `back_max_ms`** |
| `corr=signed_ms/interval_ms` | last signed step the setter applied, and local-timer interval since the previous apply. `interval_ms` is `0` on the first apply of a boot |
| `ahold=last/max` on `wire crsp` | send-side hold (µs) from core0 capture to core1 encode; the wire value is already corrected by it |

`applies` and `corr` count arrived anchors; `back` and `back_max_ms` count
every apply. **`back` is a truncation counter** — hundreds of backward steps
against tens of arrived anchors is ordinary. The first apply of a relation
era is offset adoption (`applies=1 interval_ms=0` identifies only the first
arrived anchor of the boot). Sender is the responder in both relations (HOST,
or DUAL-HOST Right).

`last=` is stamped once at capture; `wire sect` prints many paced interval
lines later. On a time-source half (`sync_timer_is_time_source()`,
`split/era_split_transport_scheduler.c`) `sync` ≡ local timer and the
difference is the pacing distance alone. **Never read their difference as the
anchor offset without subtracting the pacing distance.**

### Tap-hold activity

`wire act win=n spec=a/r/x jh=h/p rcl=n adv=n atx=n aap=n` — cumulative, every
relation.

| Field | Meaning |
| --- | --- |
| `win` `spec` | judgment windows opened (any of the three tapping options armed; fresh defaults hold it at zero); ERA layer-tap speculative family: activations / tap-settle reverts / overflow-clear aborts. `a - r - x` = holds that kept their speculative layer. Upstream mod-tap increments nothing |
| `jh` `rcl` `adv`/`atx`/`aap` | cross-half settles by judge (hold-on-other-key-press, then permissive-hold); retro taps a peer press cancelled (all three move only on the half whose key was in flight); advertised ACTIVITY changes / responder-confirmed sends (initiator sends from `rt`/`tx=`) / peer values applied into the judgment cache |

| This half / peer | Reading |
| --- | --- |
| this `adv` (and `atx` on a responder) moves; peer `rt` rx still | didn't send |
| peer `rt` rx moves; its `aap` still | sent but not applied |
| peer `aap` moves | applied |

## Reset Edge

`wire edge reset`: diagnostic-only local serial owner/role reset boundary.
`hbmiss` retired with the heartbeat lane. What still brackets a boundary is
`dtap_ms`. `rel/prev` identify the last mode edge.

## Relation-Role Lines

`wire resp`: compatibility responder diagnostics projected on core0 from
generation-matched core1 results — not a core0 responder thread.

| Field | Final profile (structural: no core0 responder executor) |
| --- | --- |
| `th/svc/blk/io/trn/rst/qsc` | `th=0 svc=0 blk=1 io=0 trn=0 rst=0 qsc=1`. A nonzero `th`, `svc`, `io` or `trn` would mean a core0 responder had been revived |

| HOST (`wire hp role=host`) | PEER (`wire hp role=peer`) |
| --- | --- |
| `pc` peer cache valid; `pseq` HOST peer-cache sequence; `ack` ACK_STATUS hz/count; `vis` visual-resync tx; `rgb` RGB-state tx; `cache` update/project/flush; `rrx/hrx/srx` relation / heartbeat / source-push request rx | `lmr` local matrix ready; `f` forced source-push token; `seq` current/host-known source-push sequence; `sp` source-push hz/tx/ack; `vis` visual-resync rx; `rgb` RGB-state rx |

**There is no `hb` on the PEER line, no `hb` on `hp peer_era`, and no `wire
chb` line** (`era_identifier_map.md`'s **Routes And Route Reasons**). Relation
traffic is in `io` and `rt`. `vis` and `rgb` count distinct applied states,
at apply.

The PEER line is printed only while `rel=` is `HOST_PEER_PEER`; a PEER has no
console. After promotion the half prints `role=host` or `role=off`, neither of
which carries these two fields. Read `wire sect rx` for arrivals, `wire sect
app=` for applies, the HOST's `vis=`/`rgb=` tx counts for what was sent, and
the visual baseline by eye. **Read `wire sect rx` and not `rt` for arrivals in
this relation.**

## Communication-Core Lines

### Lifecycle — `wire ccore`

| Field | Meaning |
| --- | --- |
| `av` `init` `launch` `run` `wake` `eto`/`sto` `cnt` `loop`/`idle` `ec` | compiled-and-available; initialized; core1 entry launched; idle actor running; wake pending; entry/stop timeout flags; start/stop/wake/wake-observed; idle-actor loop/idle; launch-error / entry-timeout / stop-timeout counters |
| `att` | launch handshake in progress or already succeeded. Not cumulative: `era_split_communication_core_start()` (`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`) sets it and rolls it back to `0` on handshake failure. Read `ec`'s first field for what was attempted |
| `stop` | quiesce requested. The published identifier keeps the `stop` spelling; the API is `era_split_communication_core_request_quiesce()` (`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`), which parks core1 |
| `park=count/us idleus=us` | backend in-transaction parks and loop idle-park totals, boot-cumulative — **deltas against the elapsed window**, never a single capture over its uptime. Zero on the release image |
| `cap`/`dead` | per-boot launch-attempt cap latch, and times core0 judged core1 dead and hardware-reset it. Structurally zero on a healthy boot. `cap=1` is give-up (`LOCAL_NO_LINK`); a nonzero `dead` is a post-boot death — read beside `cown` `reclaim` |

> **REFUSED:** delete the six `av=` fields and `rxm=` as dead weight.
> **WHY:** they are structurally-constant instruments — a constant that is
> *supposed* to be constant is the reading, so deleting one deletes the
> measurement rather than dead code.
> **REOPENS:** a field whose constancy nothing depends on.

| `err` | Values |
| --- | --- |
| stage `0`–`5` | handshake word `{0, 0, 1, vector_table, stack_end, entry}` |
| phase `0`–`3` | none / FIFO write timeout / FIFO read timeout / echo-mismatch restart cap |

Phase `1`/`2`: peer core did not answer. Phase `3`: answered inside the step
timeout and answered wrong — the failure the restart cap exists for.

What clears the streak behind the latch is an observed return to core1
service, not a successful launch. `era_split_communication_core_start()`
(`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`)
returning true is compatible with a core1 that dies at its first loop pass;
a ready wait expiring counts *into* the streak.

**A zero here proves nothing about a half that wedged** — it cannot print. A
survivor takes `owner_ensure_core1_role()`
(`split/communication_core/era_split_communication_core_owner.c`)'s fast
path, which returns without touching core1 and leaves `cown` `revoke` at
zero.

### Queue — `wire cqueue`

`gen` queue generation; `cap` usable SPSC ring capacity; `q` request
level/high-water; `r` result level/high-water; `flush` queue flush count.

### Owner — `wire cown`

`own` backend owner (`0` unavailable, `2` core1; numeric `1` retired, must not
appear); `role` `0` disabled / `1` initiator / `2` responder; `epoch`
owner/released/ready; `rev` revoke/cancel/reset pending; `cnt`
transfer/revoke/release/ready; `err` transfer-timeout / ready-timeout /
init-fail / reclaim (leases reclaimed from a judged-dead core1; structurally
zero in ordinary service); `io` queue-expired / owner / epoch / cancel / reset
/ PIO / send-timeout / response-timeout / partial-frame / IO / decode /
response-contract, in that order.

### Session — `wire csess`

`pend/ready` request-pending and result-ready latches; `gen` current request
generation / last drained result generation; `sub` submit/accept counts;
`full/sf` pending-or-queue-full and core-start/owner-ready failures; `tx/res`
core1 session transaction and core0 result-drain; `stale/rfull` stale result
and result-slot-full; `ok` OK/MISS/BAD/FAIL; `last`
result/failure/request-sent/route-kind/route-reason; `rsp/peer` response kind
and decoded peer-status-valid flag.

### Responder — `wire crsp`

| Field | Meaning |
| --- | --- |
| `src` `snap` `slot`/`ready` `own` `gen` `prep` `last` `plen`/`vr` `ahold` | snapshot source `0` none / `2` live core0-published (`1` retired unreachable); valid latch; source-push slot reserved / result-ready; owner-gate readiness/ready/blocked; owner epoch / relation / snapshot / last drained; ACK/HSRSP prepare/fail; last matrix sequence + request-kind mask + response-section mask; payload length / visual-resync reason; send-side hold (shared clock above) |
| `pub/resv/full/acc/stale/drain/noack/arx/undec/quiet/coal` | publish, reserve, slot full, accepted-result publication, stale reject, core0 drain, no-response prevention, accepted-RX, undecodable-arrival, quiet-answer, then coalesced HEARTBEAT replies / the runtime sections they carried. No `clr`: rotation is republish-plus-generation-fence |

`arx` is every frame the transaction engine accepted, before admission — not
`acc`. `undec` is arrivals that were not a frame (PIO stop-bit, incomplete
first-byte window, or body that failed marker/length/CRC/direction). Together
they are the link listener (`split/era_split_link.h`'s **Reconciliation**;
step rule: `era_route_contract.md`'s **SESSION_STATUS Discovery And
Liveness**). `undec` is a subset of `noack`.

**`quiet` is requests core1 answered without publishing a result** (bare ACK).
With no coalescing, **`prep - resv` reproduces `quiet`**. **Do not compute
`resv - acc`: it is identically zero**
(`split/communication_core/era_split_communication_core_responder_service.c`);
**`resv == acc`** on every capture. A high `quiet` beside a low `acc` is the
design. Folded counters are at most one housekeeping pass stale.

`last` request mask: `0x01` heartbeat admission, `0x02` source-push, `0x04`
DUAL-HOST runtime. **Bit `0x01` does not imply a HOST-PEER matrix relation.**
Response mask follows `ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_*`;
live core0 snapshots may show `00` when no response section is due.

### Source-push — `wire csp`

This lane carries the matrix and nothing else. The one core0 enqueuer sets
`ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_CORE0_LANE_SECTIONS`, which is
`SECTION_MATRIX` alone and is named rather than spelled
(`split/era_split_wire_protocol.h`). A `csp` that runs ahead of
`source_push_tx`/`source_push_ack` and seq convergence under `hp peer_era` is
a finding.

| Field | Meaning |
| --- | --- |
| `pend`/`ready` `gen` `sub` `full`/`sf` `tx`/`res` `stale`/`rfull` `ok` `last` `seq` `rsp` | latches; generations; submit/accept; pending-or-queue-full / start-fail; core1 tx / core0 apply; stale ignore / slot full; OK/MISS/BAD/FAIL; last result/request-sent/route-kind/route-reason; last matrix sequence; last response kind/payload/section |
| `lock/vis/rgb/news` | last lock-state valid/value, visual snapshot valid, RGB-state valid, storage news valid/value |
| `hsrsp/visn/rgbn/newsn/secor` | cumulative HSRSP responses, visual/RGB/news summaries extracted from them, OR of observed HSRSP section bytes. Sticky. **All five must read zero** — the response section set has one carrier, the standing exchange's answer (`era_route_contract.md`). Apply totals are on `wire sect app=` |
| `rxm` | last core1 initiator RX wait mode: `0` no transaction observed, `1` bounded response-window FIFO polling |
