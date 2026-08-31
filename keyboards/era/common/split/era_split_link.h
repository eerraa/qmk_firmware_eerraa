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

/* ## Reconciliation
 *
 * Meet at Low, raise to the winner's stored level, keep the listener as
 * recovery. Everything below is what that sentence means on one half.
 *
 * **Boot.** The stored level is read and left alone. The running level starts
 * at Low, and `era_split_transport_scheduler_start_communication_core()`
 * (`era_split_transport_scheduler.c`) hands the backend Low before the wire
 * opens. Both halves therefore probe and listen at the same rate; the
 * listener's scan ring is not on this path.
 *
 * **Surface, then raise.** A serviced relation means `SESSION_STATUS` has
 * confirmed the peer. The standing exchange then carries the response
 * sections — RGB, visual, lock, storage news, INPUT layer, and the relation
 * time-anchor. The first EEPROM SYNC is *not* in that basket: it is the
 * storage engine's relation-open audit
 * (`era_host_peer_storage_contract.md`), it owns the red lamp, and it waits
 * until `era_split_link_runtime_settled()` so a raise does not tear a
 * content-moving episode and a converged pair does not flash red at Low.
 * The time-anchor is what makes the shared-clock deadline safe to fire; the
 * initiator does not arm the link act until one has been applied
 * (`era_split_restart_arm_ready()` in `era_split_keyboard.c`).
 *
 * **The raise.** The winner requests `ERA_SPLIT_RESTART_ACT_LINK_SPEED` with
 * its stored level as param. That reopens the 2026-08-19 ruling that the
 * link lane raises no request; CLEAN still resets, the link act no longer
 * does. The commanded-half / initiator-arms two-phase, the AUTHORITY bits,
 * and the `RESTART_ARM` section are unchanged — no wire edit, no layout
 * edit, and the byte still does not enter the SYNC engine. At T_commit both
 * halves run `era_split_transport_scheduler_apply_link_level()`: owner down,
 * `set_speed`, serial recover, relation identity kept so the silence watch
 * (100 ms) is the only window the pair must fit. `wire_scale()` is recomputed
 * in that same `set_speed` (`era_split_transaction_backend_rp2040.c`).
 *
 * **Success.** The relation is still serviced after the confirm window. A
 * half whose stored level is not the running one adopts, agreed, no reset.
 * The pair now stores the winner's level.
 *
 * **Failure.** The relation is gone after the confirm window. This half
 * reverts *running* to Low, latches the fallback for the rest of the
 * session, and writes nothing. The next boot meets at Low and tries again.
 * A cable that cannot hold even Low never joins; that remainder is accepted.
 * The one-shot report — three long red pulses, then stop — starts on the
 * first `era_split_link_fallback_report_advance()` after the latch, so a
 * caller that withholds that call while the core1 launch report owns the
 * field cannot race it. It does not persist Low into EEPROM.
 *
 * **Alone.** No serviced relation: the listener (peer-unknown responder,
 * Right) still follows a talker it can hear and cannot decode, High ->
 * Medium -> Low -> High, over the dwell, and stores nothing. The talker
 * (peer-unknown initiator, Left) probes at *running* Low on a fresh boot
 * and does nothing on silence. The ring is the recovery for a pair that
 * is already at two running rates, not the common boot. An owner Apply
 * with no peer still stores on this half and applies the divider without
 * a reset; a level applied to the non-winner half alone is overwritten by
 * the winner's the moment they connect.
 *
 * **Owner Apply, joined.** Same runtime window as the auto raise. The
 * requesting half's pending level is the param; both halves store it at
 * commit (that is the owner choosing, not the auto path) and `set_speed`.
 * The MCU does not reset. Waiting for the live adoption to persist the peer
 * is what lost a Right High Apply: the confirm window is whether *running*
 * holds, not whether EEPROM remembers the choice, and a cable pull or a
 * second Apply on the peer inside that window restored the winner's old
 * stored level. An agreed commit therefore writes the param on every half
 * that does not already store it. Auto-raise of a target both already hold
 * writes nothing. The requesting half re-enumerates USB after that commit
 * (`split/era_split_via_link.c`); a request that never commits does not
 * bounce, so Enable staying on is how the owner sees that nothing ran. That
 * bounce is not a HOST close (`system/era_usb_session.c`).
 *
 * Every path terminates and the only EEPROM writes are an owner Apply, a
 * successful adoption, and the unagreed-mark observation. The fallback
 * writes nothing, which is what keeps the retired silence fallback from
 * returning in other clothes.
 *
 * **What the stored mark still means.** The no-relation apply marks its level
 * *unagreed* -- set alone, never seen by a peer -- and the open-at-stored-level
 * observation clears it. Nothing on this lane acts on the mark for control:
 * the arm that once completed an unagreed level by raising the agreement, and
 * the arm that recorded an agreed one as failed by storing High, both retired
 * 2026-08-19 with the silence fallback that produced their evidence. The bit
 * stays written because it is a true fact about the record, costs nothing, and
 * is exactly what the refusal below would need to reopen.
 *
 * > **REFUSED:** treat a listener's *unagreed* stored level as the owner's more
 * > recent action and, once the pair opens, raise the agreement for it instead
 * > of adopting the winner's.
 * > **WHY:** the winner's own mark is cleared by observation the moment the
 * > pair settles and raises nothing, so two halves set alone to different
 * > levels would always end at the **non-winner** one -- against the one
 * > answer this firmware gives every arbitrary tie -- and every such
 * > connection would spend one extra raise.
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

/* The level the wire is running at. It starts at Low and moves when the
 * listener steps, when the agreed raise applies, or when a failed raise
 * reverts; every derived timing follows the wire rather than the setting. */
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
/* The Apply toggle. An equal level is a no-op rather than a refusal, which is
 * what lets the control stay visible: VIA compares value ids on one page and
 * has no way to see the running level, so the inertness is enforced here. That
 * test is this unit's and stays here, because this is the half of the pair that
 * holds the running level -- the agreement service takes an act it does not
 * interpret and could only re-test it by learning what a link param means.
 *
 * "Equal" is equal to the *running* level, deliberately. A half sitting at
 * boot Low with a stored High must still accept Apply for that High; testing
 * the stored level would hide the control on the common path. True when the
 * agreement accepted the request. VIA re-enumerates only if that request
 * later commits. */
bool era_split_link_request_apply(void);

/* Whether this commit should persist the param. True for an owner Apply, and
 * for an agreed peer whose stored level is not the param. Valid only inside
 * `era_split_restart_prepare_local()`. */
bool era_split_link_commit_stores(void);
bool era_split_link_commit_persists(uint8_t param, bool agreed, bool owner_apply);

/* The agreement's prepare for this act, run at the commit instant on both
 * halves. It writes when `era_split_link_commit_persists()` is true. The
 * runtime divider change is the scheduler's, invoked from the act dispatch.
 *
 * `agreed` is the service's answer to "did a peer produce this commit", and it
 * is what the stored mark records. It is asked of the service rather than of
 * this unit's own relation term, because an agreement whose relation died
 * inside the commit window is still an agreement -- both halves hold the same
 * deadline and both will write. */
void era_split_link_store_level(uint8_t level, bool agreed);

/* The three terms Reconciliation reads, all the scheduler's settled answers of
 * the same pass: whether a relation is serviced, whether this half is the
 * listener -- the peer-unknown bootstrap's responder, with the wire up -- and
 * whether this half is the rate winner (DUAL-HOST Left, HOST-PEER HOST). A
 * wire that is unavailable (authority not yet classified, launch capped) is
 * neither role, and a responder beside a known peer is not a listener: it has
 * nothing to listen for. The winner bit is a projection of the settled mode,
 * not a second planner. */
void era_split_link_note_relation(bool serviced, bool listening, bool local_is_rate_winner);

/* The step is decided here and performed by the scheduler, because the divider
 * change has to happen with the backend owner torn down and the scheduler is
 * what owns that window. `next_level` is the level to run when it returns
 * true. The listener's ring and a fallback revert to Low both use it. */
bool era_split_link_step_due(uint8_t *next_level);
void era_split_link_note_step_applied(uint8_t level);

/* True when the first EEPROM SYNC may run: the boot raise has succeeded, been
 * abandoned to Low for this session, or was never owed (winner already
 * stored Low). The storage initiator asks this and does not read the level
 * byte. */
bool era_split_link_runtime_settled(void);

/* The one-shot fallback report. Timing only — the board paints. Starts on
 * the first call after the session fallback latches and never re-arms.
 * Returns whether it still owns the status frame; `on` is written every
 * call, idle included. A caller that skips this while the core1 launch
 * report is live is what keeps the two from sharing the field. */
bool era_split_link_fallback_report_advance(bool *on);

/* The open half of Reconciliation, polled on the maintenance pass beside the
 * agreement's own task. The winner may raise a link request here; the loser
 * does not. Adoption writes this unit's own EEPROM and nothing else. */
void era_split_link_task(void);
