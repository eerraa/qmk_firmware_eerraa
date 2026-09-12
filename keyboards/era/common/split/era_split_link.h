// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../storage/era_eeprom_layout.h"

/* The split link's rate, chosen by the owner and agreed over the wire.
 *
 * Three levels, and the numbering is the layout header's
 * (ERA_SPLIT_LINK_LEVEL_HIGH/MEDIUM/LOW, High = 0 asserted there). High is the
 * compiled default every image already stores, which is what lets a zeroed
 * EEPROM block and a fresh one both read as the default with no initialiser.
 *
 * The wire does not boot at that stored value. Every half opens the backend at
 * Low, meets its peer there, exchanges the standing surface, and only then
 * raises the running level to the winner's stored value. Low exists because
 * High does not hold on every cable; meeting at Low first is what lets that
 * cable still join, and a raise that then fails leaves this session at Low
 * without writing the failure into EEPROM.
 *
 * Its cost at Low is up to 4 ms of extra peer-half input latency in
 * DUAL-HOST and nothing else: the poll period doubles with the byte time, so
 * core1's load and the exchange's share of its own period both fall as the
 * level drops.
 *
 * **The pair's target rate is the winner's stored level.** DUAL-HOST takes
 * Left; HOST-PEER takes HOST. Both halves compute that identity from the
 * settled mode plus `is_left` (the scheduler projects it; this unit does not
 * re-derive the planner). The winner's stored byte reaches the loser as the
 * link act's param on the agreed-restart carriers. That is the UX result of
 * HOST-wins / Left-wins, not a copy of the EEPROM SYNC engine's latest-change
 * rule — SYNC is not this byte's writer.
 *
 * **Reconciliation** below is the whole of that rule. */

#define ERA_SPLIT_LINK_LEVEL_COUNT 3
_Static_assert(ERA_SPLIT_LINK_LEVEL_LOW + 1 == ERA_SPLIT_LINK_LEVEL_COUNT, "The link levels must be a dense 0..2 range; the wire body carries them in two bits.");

/* The dropdown states 460800 / 230400 / 115200, and the refusal beside the VIA
 * control is what keeps those labels from lying. Medium and Low are derived
 * rather than stated so the set stays coherent under a build that moves the
 * option, and the refusal fires before a label can drift. */
#define ERA_SPLIT_LINK_SPEED_HIGH ((uint32_t)SERIAL_USART_SPEED)
#define ERA_SPLIT_LINK_SPEED_MEDIUM (ERA_SPLIT_LINK_SPEED_HIGH / 2U)
#define ERA_SPLIT_LINK_SPEED_LOW (ERA_SPLIT_LINK_SPEED_HIGH / 4U)

/* The listener's dwell: how long it listens at one level before it judges the
 * window. Two things bound it from below and one from above. It must hold at
 * least two of the talker's discovery probes, because a peer that has just
 * been given power holds the line low until its firmware claims the pin -- the
 * cable carries power from the hosted half, so plugging it boots the other,
 * and an RP2040 pad resets with its pull-down enabled -- and that break
 * produces exactly one undecodable arrival (the RX program parks after the
 * error until the line idles), so one arrival must never move a level; the
 * probe backs off to 500 ms after ten misses, and each probe carries its own
 * response window, so two of them fit inside 1500 ms with room -- the
 * scheduler asserts that arithmetic beside the constants it owns. It must also outlast the time a
 * correct level takes to prove itself, and it does easily: at the right rate
 * the first probe decodes, and one decoded frame inside the window is what
 * cancels the step. From above, every 1500 ms of mismatch is one lap of the
 * ring's three levels at most, so a mismatched pair meets in <= 2 dwells plus
 * discovery, and that is the cost the owner sees once, on a recovery path,
 * not on the common boot. */
#ifndef ERA_SPLIT_LINK_SCAN_DWELL_MS
#    define ERA_SPLIT_LINK_SCAN_DWELL_MS 1500
#endif
/* Undecodable arrivals in one dwell before the listener steps. Two, for the
 * break above: one is what a peer booting on cable power produces before it
 * speaks, and the talker's probes repeat, so a peer that is actually talking
 * clears two well inside a dwell. Whether the boot transient can produce a
 * second -- the line crossing the listener's input threshold twice as the
 * peer's rail rises against this half's pull-up -- is a device reading and not
 * a derivation; a second one costs a step taken in error, which the closed
 * ring corrects within a lap once the peer speaks. */
#ifndef ERA_SPLIT_LINK_SCAN_NOISE_MIN
#    define ERA_SPLIT_LINK_SCAN_NOISE_MIN 2
#endif
_Static_assert(ERA_SPLIT_LINK_SCAN_NOISE_MIN >= 2,
               "One undecodable arrival is the boot-break; two is the first count that can be a talker.");

/* After a runtime raise, how long this half waits for the relation to still
 * be serviced before it treats the raise as failed and reverts the *running*
 * level to Low. It must sit past one responder-silence watch so a High the
 * cable cannot hold is observed as a forgotten session, not as the apply's
 * own owner-down gap. The scheduler asserts this against
 * ERA_SPLIT_RESPONDER_SILENCE_MS. */
#ifndef ERA_SPLIT_LINK_UPGRADE_CONFIRM_MS
#    define ERA_SPLIT_LINK_UPGRADE_CONFIRM_MS 200
#endif
/* How long a non-winner waits after the relation opens for a raise that is
 * not coming (winner stored Low) before it treats Low as the session's
 * settled rate and may adopt. */
#ifndef ERA_SPLIT_LINK_UPGRADE_WAIT_MS
#    define ERA_SPLIT_LINK_UPGRADE_WAIT_MS 500
#endif
/* The fallback report: three long full-field red pulses, then stop. Every
 * interval is a multiple of the 16 ms RGB flush grid the launch report
 * already uses, so the long-on / long-off groups stay legible. 640 ms is
 * four launch flickers; the 960 ms tail is the same end-marker the launch
 * report uses. */
#ifndef ERA_SPLIT_LINK_FALLBACK_REPORT_ON_MS
#    define ERA_SPLIT_LINK_FALLBACK_REPORT_ON_MS 640
#endif
#ifndef ERA_SPLIT_LINK_FALLBACK_REPORT_OFF_MS
#    define ERA_SPLIT_LINK_FALLBACK_REPORT_OFF_MS 640
#endif
#ifndef ERA_SPLIT_LINK_FALLBACK_REPORT_TAIL_MS
#    define ERA_SPLIT_LINK_FALLBACK_REPORT_TAIL_MS 960
#endif

/* A successful listener-side rate recovery is deliberately shorter and
 * quieter than the failure report: two short pulses, then a tail. The link
 * unit owns only the timing; the board presentation layer paints success
 * green. This report is never an input to reconciliation or agreement. */
#ifndef ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_ON_MS
#    define ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_ON_MS 320
#endif
#ifndef ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_OFF_MS
#    define ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_OFF_MS 320
#endif
#ifndef ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_TAIL_MS
#    define ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_TAIL_MS 480
#endif

/* ## Reconciliation
 *
 * Boot opens Low without rewriting the stored record. On a serviced relation,
 * the winner (DUAL-HOST Left, HOST-PEER HOST) hands its stored target to the
 * agreed-restart service once. That service alone owns request, arbitration,
 * timeout, arm and commit. The link lane keeps no owner-Apply flag and does
 * not distinguish a VIA request from a peer or boot request at commit.
 * Even an unchanged/Low target takes this agreement; the non-winner never
 * infers a durable setting from silence. After ERA_SPLIT_LINK_UPGRADE_WAIT_MS
 * it may release the initial storage wait, without writing.
 *
 * **One rate agreement per meeting of the pair.** A successful checked act latches
 * the agreed running level on each half. Reconciliation is reopened by a
 * fresh meeting (the scheduler reports a serviced edge that follows a real
 * departure: bootstrap backoff, wire loss, or a responder's silence), by a
 * change of the rate-winner identity, by a recovery or listener step that
 * moved the running level off the agreed one, or by an explicit Apply. It is
 * NOT reopened by a same-pair reopen inside the scheduler's fast recovery
 * window -- the SESSION revalidation every EEPROM SYNC push close forces --
 * and never by a housekeeping pass after a failed write or expired request.
 * That continuity classification is the one the EEPROM SYNC indicator already
 * uses for its peer mirror; the two consumers share it so a storage round is
 * never interleaved with agreed restarts of a rate the pair already runs.
 *
 * **Local success.** era_split_link_apply() returns true only after the
 * scheduler has quiesced the old owner, selected the divider and all derived
 * timings, restored the role and its Core1 publications, AND the result-bearing
 * NVM replacement has returned OK/NO_CHANGE. Physical transition precedes all
 * LINK SPEED persistence, on both halves. The shared deadline is translated
 * once into each half's monotonic clock; a role flip must not move execution.
 * The scheduler's dirty wire bit requests an actual lease repair even when
 * the planned role has not changed. An equal divider is not proof of a live
 * lease. Runtime publication precedes NVM so Core1 can run during flash work.
 *
 * **Failure.** An expired/unconfirmed request leaves runtime and durable bytes
 * unchanged; a new request is explicit. Runtime reconfiguration failure is not
 * success and writes nothing. It requests Low recovery through the existing
 * owner-down path; a capped Core1 remains safely unavailable. NVM failure after
 * runtime success keeps the selected runtime and the last confirmed durable
 * cache. It does not roll one half back while the other runs the new rate, and
 * it is not silently retried as later "adoption". Applying the same running
 * level remains possible when durable storage still differs.
 *
 * The 200-ms confirmation judges link liveness, not persistence success. A lost
 * relation requests one Low recovery and latches automatic raises off until
 * reboot, an explicit local Apply, or a newly successful peer transition.
 * Recovery and confirmation do not wait on NVM readiness. The listener may subsequently follow noise to
 * find a peer; it is not forced back to Low after every scan step. This fallback
 * never stores Low. The one-shot three-pulse report is presentation only.
 * A listener that actually steps because of undecodable traffic and then opens
 * a serviced relation earns a separate presentation-only two-pulse success
 * report. The newly serviced relation's initiator alone requests LINK_RECOVERED,
 * from its own listener search or the listener's SESSION_STATUS answer to the
 * probe that found it (`rate_searched`, era_split_wire_protocol.h). The
 * discovery talker has no other way to see the search: from its
 * probes a listener at the wrong rate and no listener at all look the same.
 * Ordinary boot Low -> target and same-rate attach raise it on neither half.
 * Neither report changes rate reconciliation or persistence.
 *
 * **One presentation epoch, not two local starts.** The initiator's pending
 * report is offered during the final commit-lead portion of rate confirmation
 * (or immediately when idle), expires after the agreement's request lifetime,
 * and is discarded on loss of service or runtime failure. The unchanged
 * confirmation still gates visibility; overlap removes serial waiting, not
 * liveness evidence.
 * Once accepted, only the agreement owns it; an aborted report is not retried.
 * Initial ownership is the settled initiator, not discovery's talker: a Left
 * HOST becomes responder at discovery and may finish the rate act and request
 * its report between two polls. No idle need cross between those acts, so the
 * initiator's duplicate-request guard must suppress that responder request.
 * An initiator-local report needs no return request or extra idle handshake;
 * no replay guard is relaxed. A later role change retains the agreement's
 * existing deadline/rotation rules.
 * LINK_RECOVERED requires a peer and the existing time-anchor/echo handshake,
 * yields to admitted storage, and temporarily uses the existing timed window.
 * It skips the raw-HID quiet gate: a lamp interrupts neither USB nor NVM.
 * It works even after a searched fallback with automatic raises latched off.
 * Its dispatch captures the agreed local deadline, not the dispatch time.
 * Each advance derives the same two-pulse phase and expiry from that instant;
 * duplicate arms, a role/clock change, late dispatch and a late first display
 * cannot restart it. Zero is a valid local deadline across timer wrap.
 * The renderer advances while masked: sleep and higher-priority launch/failure
 * presentation remain authoritative, but hidden pulses are skipped rather
 * than replayed. A later local runtime failure cancels even an accepted report
 * whose dispatch is still in the future; only a new searched meeting clears
 * that cancellation. The colour is
 * green; EEPROM synchronization retains its separate blue presentation.
 * Clock-anchor error and physical scheduling/LED flush skew still require a
 * device measurement; this is not a promise of zero physical skew or delivery
 * across a lost final message, power cut, or a higher-priority display.
 *
 * **Limits of the agreement.** Its unchanged five-byte arm and seven-byte
 * authority body agree a transition, not a distributed NVM commit. There is no
 * bilateral completion acknowledgement. A lost final echo/disarm, power cut,
 * or one-sided hardware/NVM fault may leave different durable records. Each
 * NVM transaction is locally atomic; reboot mounts that record, opens Low, and
 * the same winner rule reconciles the pair. Never infer pair durability from
 * one half's successful local result. Deadline skew from concurrent unrelated
 * synchronous work, clock-anchor error and physical quiesce/restart latency
 * remains a hardware bound, not a host-test or millisecond-timer guarantee.
 *
 * **VIA and USB.** SET level changes a RAM selection only. Apply hands one
 * immutable parameter to the agreement; later dropdown edits do not rewrite
 * it. GET Apply returns the consumed action value 0, not a success receipt.
 * Neither successful nor failed LINK SPEED work re-enumerates USB, clears
 * keyboard reports, or creates a synthetic authority/USB-session hold. UI
 * refresh belongs to the host application, not the split transaction.
 *
 * An explicit local Apply has a terminal-only presentation receipt. Pending
 * stays available to readback but owns no RGB frame. Checked local runtime +
 * durable success flashes green once immediately on return, as does an
 * already matching runtime AND durable level without work. Busy, failed,
 * expired or superseded requests flash red. LINK_SPEED skips raw-HID quiet:
 * it does not cut the USB session. Storage admission, time-anchor adoption and
 * the shared commit lead still apply; a VIA callback only queues the intent.
 * Automatic reconciliation and peer-originated acts create no local receipt.
 * A repeated click during an own pending request or its health observation
 * preserves the receipt and target rather than replaying a pulse. A global
 * agreement result alone never proves this request succeeded.
 *
 * The existing liveness-confirmation window remains a recovery safety check,
 * not a success-display delay. Its failure corrects that Apply's receipt to
 * FAILED; its success does not re-arm the pulse. A later rate act retires the
 * old receipt's observation so its failure cannot be charged to the old act.
 *
 * The receipt changes no execution admission, and never schedules, retries or
 * persists a level. A failed NVM write can leave the selected runtime active;
 * it is FAILED, not APPLIED. A green receipt proves this half's checked result,
 * not both durable stores or continuing peer liveness. Setting the RGB state
 * before the synchronous NVM return would not flush an earlier frame on Core0;
 * no recursive rendering or deferred persistence is introduced for feedback.
 * Read-only VIA labels expose configured runtime,
 * confirmed saved level and last local receipt; queries do not erase it.
 * Sleep and failure presentation retain priority, hidden pulses expire, and
 * normal effect/lock settings are not rewritten for feedback. Timing and
 * colours are owned by the presenter, not a new split-wire operation.
 *
 * Low-at-boot then reconciliation remains correct after any explicit reboot.
 * Making LINK_SPEED itself reset would repeat on every boot reconciliation,
 * including an unchanged/Low target. A reboot cannot be a success receipt:
 * it disconnects input but cannot distinguish a durable failure or later Low
 * fallback. The explicit receipt leaves the normal boot path untouched.
 *
 * **Stored record.** The four-byte allocation and level values are unchanged.
 * The former unagreed bit is accepted when reading existing records but has
 * no control meaning and is no longer written/cleared by observation. EEPROM
 * SYNC remains excluded: this lane is the stored level's only writer.
 *
 * > **REFUSED:** treat a listener's *unagreed* stored level as the owner's more
 * > recent action and, once the pair opens, raise the agreement for it instead
 * > of adopting the winner's.
 * > **WHY:** the mark does not order edits. Letting it override the winner
 * > would make two halves set alone settle at the non-winner's value.
 * > **REOPENS:** the owner deciding that a level applied to the non-winner
 * > half alone must reach the pair; then this arm returns and the tie prose
 * > is rewritten to match.
 *
 * > **REFUSED:** converge the two halves by syncing the stored level through
 * > the EEPROM storage engine.
 * > **WHY:** the engine is gated by a per-half owner preference that can be
 * > switched off, so the repair of a *pair invariant* would be absent exactly
 * > when the owner needs it -- and it would give one byte two writers with
 * > different rules, one that raises and one that does not.
 * > **REOPENS:** the storage engine becoming unconditional, which would make it
 * > a property of the relation rather than a preference.
 *
 * > **REFUSED:** keep a silence-triggered drop to High beside the listener's
 * > step, as a floor for a listener that hears nothing.
 * > **WHY:** silence is what a late peer sounds like, and a half that moved on
 * > silence and then opened at High is what stored High over a working
 * > Medium on both halves; a listener that hears nothing has nothing to
 * > follow and loses nothing by waiting.
 * > **REOPENS:** a listener state that is provably not "no peer yet" and that
 * > the noise counter cannot see.
 *
 * > **REFUSED:** raise the pair by resetting both MCUs after they have met
 * > at Low.
 * > **WHY:** the divider change is a PIO re-init inside the owner-down window
 * > the listener already uses; a reset is a second hitch the raise does not
 * > need, and it is not what makes the two halves change at one instant —
 * > the shared-clock deadline is.
 * > **REOPENS:** a device reading that the two applies cannot stay inside the
 * > responder-silence watch, so the pair cannot keep the session across the
 * > raise.
 *
 * > **REFUSED:** adopt the running level onto EEPROM while this session is
 * > latched on the Low fallback.
 * > **WHY:** that write is the retired silence fallback in other clothes — a
 * > stored High or Medium the cable could not hold this boot becomes Low,
 * > and the next boot never tries the owner's setting again.
 * > **REOPENS:** the owner deciding a failed raise should persist Low.
 */

/* The configured divider. A successful serial restart is a separate scheduler
 * result; this byte alone never proves a serviced link or a durable write. */
uint8_t era_split_link_active_level(void);
/* The level as a baud, which is the only form anything outside this unit takes
 * it in. The backend is handed the number and derives its own scale from it;
 * this unit answers no scale and no period, because a second derivation of the
 * rate is a second place for the rate to be wrong. */
uint32_t era_split_link_speed(uint8_t level);

/* The VIA surface. The pending level is RAM only and never stored: a
 * half-applied level must not survive a reset, and storing it would be a second
 * copy of the fact the agreement exists to keep single. */
uint8_t era_split_link_pending_level(void);
bool    era_split_link_set_pending_level(uint8_t level);
/* True means accepted, not completed. Equal runtime AND durable level is
 * inert. The same runtime with a failed durable write remains retryable. */
bool era_split_link_request_apply(void);

/* The last explicit local Apply's receipt, not transaction ownership and not
 * proof of the peer's NVM. Automatic reconciliation and peer-only acts do not
 * create a receipt. A repeated click while this request is pending preserves
 * its immutable target rather than replacing it with the edited dropdown. */
typedef enum {
    ERA_SPLIT_LINK_APPLY_NONE,
    ERA_SPLIT_LINK_APPLY_PENDING,
    ERA_SPLIT_LINK_APPLY_UNCHANGED,
    ERA_SPLIT_LINK_APPLY_APPLIED,
    ERA_SPLIT_LINK_APPLY_BUSY,
    ERA_SPLIT_LINK_APPLY_FAILED,
    ERA_SPLIT_LINK_APPLY_CANCELLED,
} era_split_link_apply_status_t;
era_split_link_apply_status_t era_split_link_apply_status(void);
uint8_t era_split_link_apply_target(void);
bool era_split_link_get_stored_level(uint8_t *level);
/* Timing only. Pending is silent; terminal receipts expire without being erased
 * from readback. The board supplies colour and preserves sleep/failure priority. */
bool era_split_link_apply_report_advance(era_split_link_apply_status_t *status, bool *on);

/* The complete local LINK SPEED act at T_commit: checked runtime transition,
 * then checked durable replacement. All request origins use this one path. */
bool era_split_link_apply(uint8_t level);

/* The three terms Reconciliation reads, all the scheduler's settled answers of
 * the same pass: whether a relation is serviced, whether this half is the
 * listener -- the peer-unknown bootstrap's responder, with the wire up -- and
 * whether this half is the rate winner (DUAL-HOST Left, HOST-PEER HOST). A
 * wire that is unavailable (authority not yet classified, launch capped) is
 * neither role, and a responder beside a known peer is not a listener: it has
 * nothing to listen for. The winner bit is a projection of the settled mode,
 * not a second planner. `fresh_meeting` is the scheduler's relation-continuity
 * verdict for a serviced edge: true after a real departure, false for a
 * same-pair reopen inside its fast recovery window (Reconciliation above).
 * `peer_rate_searched` is the peer's SESSION_STATUS answer's discovery fact,
 * from the same consumed session record that produced the edge: the listener
 * this half met changed its running rate to hear it. `local_is_initiator` is
 * the scheduler's settled wire-role projection. That side alone requests the
 * success report on a searched serviced edge, whether the search fact was
 * local or received; no second role planner or stored role flag is added. */
void era_split_link_note_relation(bool serviced, bool listening, bool local_is_rate_winner, bool fresh_meeting,
                                  bool peer_rate_searched, bool local_is_initiator);

/* The listener's discovery fact for its SESSION_STATUS answer, which
 * era_split_scheduler_session.c builds in: true from the step taken because
 * of undecodable traffic until the relation it found opens, or the listening
 * episode ends without one. The peer-unknown initiator never steps, so a
 * probe never carries it. Presentation only, like the report it raises. */
bool era_split_link_rate_searched(void);

/* The step is decided here and performed by the scheduler, because the divider
 * change has to happen with the backend owner torn down and the scheduler is
 * what owns that window. `next_level` is the level to run when it returns
 * true. The listener's ring and a fallback revert to Low both use it. */
bool era_split_link_step_due(uint8_t *next_level);
void era_split_link_note_level_selected(uint8_t level);

/* True when no agreement or reconciliation rate change is outstanding. The
 * storage initiator separately requires a live owner; this is not a lease. */
bool era_split_link_runtime_settled(void);

/* The one-shot fallback report. Timing only — the board paints. Starts on
 * the first call after the session fallback latches and never re-arms.
 * Returns whether it still owns the status frame; `on` is written every
 * call, idle included. A caller that skips this while the core1 launch
 * report is live is what keeps the two from sharing the field. */
bool era_split_link_fallback_report_advance(bool *on);

/* Presentation-only success report. A searched serviced edge requests one
 * peer rendezvous; neither a local serviced edge nor first advance starts it.
 * Ordinary boot Low -> target and same-rate attach stay silent. */
/* Dispatch-only: requires a currently held agreed deadline. No divider,
 * persistence, reset or local standalone start is performed. */
bool era_split_link_reconcile_success_report_commit(void);
bool era_split_link_reconcile_success_report_advance(bool *on);

/* The open half of Reconciliation, polled on the maintenance pass beside the
 * agreement's own task. The winner may raise a link request here; the loser
 * does not. Adoption writes this unit's own EEPROM and nothing else. */
void era_split_link_task(void);
