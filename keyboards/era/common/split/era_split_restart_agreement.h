// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "era_split_wire_protocol.h"

/* The agreed restart: both halves rendezvous on the same act/deadline.
 * This is not a distributed durability receipt or proof against lost final
 * messages. LINK's checked local act/failure contract is in era_split_link.h.
 *
 * One mechanism and three acts. A user hands over an `(act, param)` and this
 * unit does the rest -- the raw-HID quiet gate, the agreement over the wire,
 * the shared-clock deadline, and (unless peer-only) the degrade to acting
 * alone without a peer. **It knows neither user**: what an act *is*
 * (the properties in `era_split_restart_act_rules[]`) and what an act *does*
 * (`era_split_restart_prepare_local()`) are both declared here by name and
 * defined in era_split_keyboard.c, the one unit that knows both users -- so
 * this unit names no user and no user's constant, and no user names another.
 *
 * **The requesting half requests; the relation's initiator arms.** Owner
 * commands in HOST-PEER originate at the USB HOST, the wire responder; in
 * DUAL-HOST either half may command. Automatic notification can originate at
 * either wire role. The request rides the AUTHORITY flags byte because
 * the response section mask has all eight markers assigned; the arm rides the
 * RESTART_ARM push section, which is the initiator's direction, because the
 * initiator is what owns the deadline.
 *
 * Ordinary acts use the armed bit to distinguish request from adopted
 * deadline. CLEAN additionally uses its otherwise-unused param values for a
 * deadline-free PREPARED vote and a COMMIT_ARMED echo. That makes both checked
 * boot-predicate writes precede the first deadline without adding a section or
 * widening either body.
 *
 * **The raw-HID quiet gate protects an act that interrupts the USB session.**
 * LINK_SPEED and presentation explicitly skip it through the act table; both
 * still yield to admitted storage. CLEAN keeps the gate. The initiator's arm for a peer's request and the
 * commit instant itself are not held on this half's own quiet: two halves have
 * two VIA applications and one shared deadline, so no instant is quiet on both
 * by construction. On a half acting alone the gate is adjacent -- quiet
 * observed, checked prepare, reset. On a serviced CLEAN it precedes selection;
 * both halves then quarantine storage and finish checked prepare before the
 * shared deadline exists. The commanded half's traffic is the one the quiet
 * gate was built against, and the window on the other half is minimised rather
 * than closed.
 *
 * **When both halves ask at once, Left wins.** It is reached by one owner
 * action on each half inside one arm window, and by the link lane's boot
 * raise: the winner requests `ERA_SPLIT_RESTART_ACT_LINK_SPEED` with its
 * stored level after the standing surface is up (`era_split_link.h`'s
 * **Reconciliation**). That reopens the 2026-08-19 ruling that the link
 * lane raises no request. CLEAN still resets; the link act no longer does.
 * A commit retires this half's leftover request and will not arm a peer
 * request until that peer has advertised idle: the link act does not reset,
 * so without that a second Apply still in flight would be the next arm and
 * Left-wins would last one raise.
 *
 * The answer is arbitrary -- the owner gave two contradictory values and either
 * is equally theirs -- so all it can be is **the same arbitrary answer every
 * time, and the same one the rest of this firmware already gives**. Left is
 * already the primary half wherever handedness is read: it initiates DUAL-HOST
 * and the peer-unknown bootstrap (`era_split_mode_planner.c`), it owns the low
 * matrix rows (`era_host_peer_response.c`), and the storage engine's conflict
 * cell breaks its own tie to Left (`era_host_peer_storage.c`,
 * `era_host_peer_storage_contract.md`'s **Arbitration**). A second convention
 * pointing the other way would be one more rule to hold for nothing.
 *
 * **Only the initiator arms, so the rule is enforced from whichever side that
 * is**, and the two cases are not symmetric:
 *
 * - **A Left initiator takes its own request first**, and while that request
 *   is still behind its own raw-HID quiet gate it arms nothing else: it already
 *   holds the answer, so it waits for it rather than arming the peer's request
 *   in the meantime. Without that hold "Left wins" would depend on whether
 *   Left's VIA happened to be talking when the two requests met -- the peer's
 *   would cross first and Left would arm it -- which is the same race the
 *   Right-side window below exists to close, met from the other side.
 * - **A Right initiator arms the peer's request when one is visible, and its
 *   own otherwise.** No window: it does not wait for a Left request that might
 *   still be behind Left's quiet gate, because it holds no request of its own
 *   to hold against one. A Right initiator exists only in HOST-PEER, where the
 *   initiator is the half with no USB session (era_split_mode_planner.c) and
 *   therefore no VIA. A Right initiator advertising a request is either the
 *   HOST-PEER boot raise (PEER is the initiator, HOST is the winner, so this
 *   half arms the peer's request) or a request accepted on its VIA while it
 *   was still a hosted responder and carried across the relation change that
 *   made it the initiator -- an owner click followed within seconds by
 *   unplugging that half's USB -- and that request is served at once. What is
 *   given up is Left-wins in the corner where Left's owner also clicks inside
 *   that same window and Left's request is still gated when Right arms; the
 *   derived hold that closed it, the raw-HID quiet bound plus one arm timeout,
 *   was a clock in a rule that otherwise has none, retired 2026-08-19 by
 *   owner decision.
 *
 * The Left hold cannot delay an owner action on its own: it runs only while
 * Left itself has a request in flight, so what it postpones is the peer's
 * request in exactly the both-asked-at-once case the rule decides, and by at
 * most the gate's own bound. */

typedef enum {
    ERA_SPLIT_RESTART_ACT_NONE           = 0,
    ERA_SPLIT_RESTART_ACT_LINK_SPEED     = 1,
    ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN   = 2,
    ERA_SPLIT_RESTART_ACT_LINK_RECOVERED = 3,
} era_split_restart_act_t;
/* All four act code points are assigned. LINK_RECOVERED is a peer-only,
 * presentation-only rendezvous: param zero, no divider, NVM write or reset. */
#define ERA_SPLIT_RESTART_ACT_MAX ERA_SPLIT_RESTART_ACT_LINK_RECOVERED
_Static_assert(ERA_SPLIT_RESTART_ACT_MAX <= ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_VALUE_MAX,
               "The act set must fit the two-bit field both wire carriers give it.");

/* CLEAN has no user parameter. Inside its two existing wire carriers the same
 * two-bit field instead names the bilateral protocol phase. Keeping these
 * values here lets the carrier validators and the agreement state machine ask
 * one owner rather than widening either body. */
#define ERA_SPLIT_RESTART_CLEAN_PARAM_REQUEST 0
#define ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED 1
#define ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT 2
_Static_assert(ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT <= ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_PARAM_MASK,
               "Every CLEAN phase must fit both carriers' shared two-bit param field.");

/* How long the initiator waits for the responder to answer an arm before it
 * retires the arm. It covers the worst HOST-PEER round trip -- publish, poll,
 * snapshot, poll, drain, each bounded by that relation's response poll period
 * -- and the responder disarms one poll after the retire crosses, so a
 * responder that armed on an answer nobody received is disarmed well before the
 * deadline rather than resetting alone. */
#ifndef ERA_SPLIT_RESTART_ARM_TIMEOUT_MS
#    define ERA_SPLIT_RESTART_ARM_TIMEOUT_MS 60
#endif
/* How far ahead of the arm the commit deadline is placed, on the shared clock.
 * It has to clear the arm timeout plus the retire's own crossing, or the
 * recovery above has no room to run -- the assert that holds it against the
 * relation's poll period is in era_split_transport_scheduler.c, which is where
 * that period lives.
 *
 * **One value for every act.** LINK_SPEED performs its work at the commit.
 * CLEAN's checked prepare happens earlier and creates no deadline; only after
 * both PREPARED votes exist does this delay begin. Neither act puts its own
 * synchronous flash work before the physical transition inside this interval;
 * concurrent unrelated work and physical skew still require device validation. */
#ifndef ERA_SPLIT_RESTART_COMMIT_DELAY_MS
#    define ERA_SPLIT_RESTART_COMMIT_DELAY_MS 120
#endif
/* How long either a pending quiet wait or an advertised request may live.
 * Without it a request the initiator never manages to arm against is retried
 * for the life of the relation, because the request is latest-state and a half
 * that keeps advertising one keeps being armed against. It is generous rather
 * than tight: an arm attempt costs the arm timeout, and a storage episode that
 * defers the arm outright can run for a second or two. */
#ifndef ERA_SPLIT_RESTART_REQUEST_LIFETIME_MS
#    define ERA_SPLIT_RESTART_REQUEST_LIFETIME_MS 5000
#endif
_Static_assert(ERA_SPLIT_RESTART_REQUEST_LIFETIME_MS > ERA_SPLIT_RESTART_ARM_TIMEOUT_MS,
               "A request that expires before one arm attempt completes can never be agreed.");
_Static_assert(ERA_SPLIT_RESTART_COMMIT_DELAY_MS > ERA_SPLIT_RESTART_ARM_TIMEOUT_MS,
               "The commit deadline must sit past the arm timeout, or the retire has no room to reach a responder that armed on a lost answer.");

/* **What a validator may accept, asked of the acts rather than guessed from
 * the field width.** Both carriers give the act and the param two bits, so both
 * need semantic validation even though all act code points are assigned:
 * a param can be outside what its act takes. Validation reads the act table --
 * a param range is the act's wire contract, so it belongs beside the act's
 * declaration and not inside the codec that happens to read the bits.
 *
 * `ERA_SPLIT_RESTART_ACT_NONE` is accepted with a zero param, because the idle
 * body is a valid body. */
bool era_split_restart_intent_valid(uint8_t act, uint8_t param);

/* Carrier-specific validation. CLEAN uses protocol phases that are not legal
 * user intent parameters: AUTHORITY carries REQUEST, PREPARED, and
 * COMMIT_ARMED, while RESTART_ARM carries only PREPARE(T=0) or COMMIT(T!=0).
 * LINK_SPEED and the canonical all-zero idle body retain their existing
 * ranges. */
bool era_split_restart_authority_valid(uint8_t act, uint8_t param, bool armed);
bool era_split_restart_arm_valid(uint8_t act, uint8_t param, uint32_t commit_ms);

/* **The whole user-facing surface.** Returns false if the act is not one of
 * the above or if another restart is already pending, which is what makes two
 * acts interleaving impossible rather than unlikely. Returning true means the
 * restart was accepted, not that it has happened: the quiet gate, the
 * agreement and the deadline all still have to run. */
bool era_split_restart_agreement_request(era_split_restart_act_t act, uint8_t param);

/* One occupancy predicate for request admission and callers that must yield.
 * It includes an unconfirmed live arm, not just requests and accepted commits. */
bool era_split_restart_agreement_in_flight(void);
/* Read-only matching intent for an action receipt. This grants no ownership,
 * cannot cancel or schedule work, and excludes a different act/parameter that
 * arbitration selected in place of the caller's original request. */
bool era_split_restart_agreement_holds_intent(era_split_restart_act_t act, uint8_t param);

/* Derived reservation for a nonzero-deadline proposal/commit. Opportunistic
 * flash work and new storage episodes yield; deadline-free CLEAN prepare and
 * already-admitted storage work still have a path to complete. */
bool era_split_restart_agreement_timed_window(void);

typedef enum {
    ERA_SPLIT_RESTART_RESULT_NONE,
    ERA_SPLIT_RESTART_RESULT_SUCCEEDED,
    ERA_SPLIT_RESTART_RESULT_ABORTED,
    ERA_SPLIT_RESTART_RESULT_FAILED,
} era_split_restart_result_t;
/* Local outcome, not a bilateral durable receipt. A fresh accepted attempt
 * clears it; retirement publishes checked prepare's result. CLEAN failure
 * remains quarantined even though its terminal outcome is FAILED. */
era_split_restart_result_t era_split_restart_agreement_last_result(void);

/* True once a serviced CLEAN has been selected for bilateral prepare, and
 * remains true through a checked-write failure or reset. Storage admission
 * asks this O(1) fact so no pre-CLEAN snapshot can cross after either half has
 * invalidated its boot predicate. */
bool era_split_restart_agreement_storage_quarantined(void);

/* **What an act is: policy properties, and nothing else about an act lives in
 * the service.** They are separate booleans rather than one, because they are
 * answers to different questions and a future act may answer them differently.
 * The table is declared here and **defined in era_split_keyboard.c beside
 * era_split_restart_prepare_local()**, so the service reads an act's properties
 * without naming the user whose constants they are -- the link switch's param
 * bound is the link level's range, and that number is the link unit's to
 * state, not this one's to include. */
typedef struct {
    /* Whether the initiator may commit without the responder's answer.
     *
     * The link switch may not: a lone divider change leaves the pair at two
     * rates. CLEAN may not either: a lone reset leaves the other half's valid
     * macro image available to storage convergence, which can restore it onto
     * the CLEANed half at relation reopen. A recovery report must not invent a
     * local epoch either. All require an answer while service exists; only
     * non-peer-only acts may take the local path without a serviced relation. */
    bool requires_confirmation;
    /* Whether the arm waits for a storage episode to finish.
     *
     * The link switch does: applying through the owner's VIA save loses the
     * save, and the setting can wait a second. It does not wait on the boot
     * changed-shadow a CLEAN leaves: that lamp is what the relation-open audit
     * clears, and the audit waits for this raise. CLEAN does yield to live pair
     * work after raising quarantine; the storage task completes an admitted
     * Apply coherently or tears the episode down before checked prepare. */
    bool yields_to_storage;
    /* Whether the service resets both MCUs after prepare. The clean does; the
     * link switch does not — its prepare is the runtime divider change. */
    bool resets;
    /* The widest param this act may carry on the wire. Both carriers give the
     * field two bits, so an act with no parameter still has to say so or the
     * three values it never means would be accepted. */
    uint8_t param_max;
    /* Peer-only acts have no standalone degrade. Losing service retires an
     * unaccepted request/proposal, not a deadline already adopted by the pair.
     * A confirmed deadline keeps the existing rotation/role-flip semantics. */
    bool requires_peer;
    /* LINK_SPEED and presentation keep USB connected. Neither waits for
     * raw-HID silence; storage drain is the separate policy above. CLEAN's
     * reset still needs the quiet gate. This never bypasses a wire vote. */
    bool skips_hid_quiet;
} era_split_restart_act_rules_t;
extern const era_split_restart_act_rules_t era_split_restart_act_rules[ERA_SPLIT_RESTART_ACT_MAX + 1];

/* The act's own checked work. LINK_SPEED and LINK_RECOVERED run at the commit
 * instant; the latter captures that accepted deadline for presentation. A
 * serviced CLEAN runs it in the deadline-free PREPARE phase; a standalone
 * CLEAN runs it immediately before reset. **Declared here and defined in
 * era_split_keyboard.c**, so this unit names no user and no user names another.
 * False is a local FAILED outcome for any act. For CLEAN it additionally
 * creates no deadline and no reset. LINK SPEED performs runtime before NVM;
 * its failure/recovery contract is canonical in era_split_link.h. */
bool era_split_restart_prepare_local(era_split_restart_act_t act, uint8_t param);
/* Whether the initiator may arm this act now. **Declared here and defined in
 * era_split_keyboard.c** beside prepare. All acts that emit a shared-clock
 * deadline wait for the initiator's time-anchor adoption; CLEAN's PREPARE arm
 * itself carries T=0 and does not ask this predicate. */
bool era_split_restart_arm_ready(era_split_restart_act_t act);

/* Deadline provenance for act handlers/diagnostics, valid during prepare.
 * It describes peer agreement, never bilateral NVM completion. */
bool era_split_restart_agreement_commit_agreed(void);
/* The agreed local monotonic instant, available only while holding or
 * dispatching a confirmed commit. It is NOT the time the cold task happened
 * to run. Presentation uses it so late dispatch cannot rebase its phase. */
bool era_split_restart_agreement_agreed_deadline(uint32_t *local_ms);

/* The wire. This half's request-or-armed fact fills the AUTHORITY section in
 * both directions; the initiator's arm fills the RESTART_ARM push section. An
 * act of NONE is the idle form of each. */
void era_split_restart_agreement_fill_authority(era_split_wire_authority_section_t *authority);
void era_split_restart_agreement_note_peer_authority(const era_split_wire_authority_section_t *authority);
void era_split_restart_agreement_arm_section(uint8_t *act, uint8_t *param, uint32_t *commit_ms);
void era_split_restart_agreement_note_peer_arm(uint8_t act, uint8_t param, uint32_t commit_ms);

/* Execution deadlines use local monotonic time. Arm/adoption converts once;
 * wire publication projects that instant into the current shared clock. A
 * duplicate arm never replaces a held commit, and a role flip/time-anchor
 * change never moves execution. A local-only LINK deadline is promoted to a
 * serviced request if a peer joins before it fires.
 *
 * Lifecycle. The relation term is the arm's own precondition; the rotation
 * drops the peer's cache and an unconfirmed arm, and keeps everything a
 * confirmed agreement has already promised. **A commit's wire face follows the
 * initiator role**: a half that becomes the initiator while holding a
 * peer-produced commit with no arm advertised re-advertises it with the
 * deadline it holds, so a role flip inside the commit window -- any USB
 * authority edge produces one -- leaves the commit a carrier on the side that
 * carries arms, and the old initiator, now the responder, re-adopts or keeps
 * it instead of reading this half's idle body as a disarm. Both halves then
 * meet at the instant they had agreed, in either order of the flip.
 *
 * `local_left` is this half's hand, and it is passed in rather than read here
 * so that the tie-break and the storage engine's answer the same question from
 * the same fact -- the scheduler's authority snapshot, which is the one
 * derivation of the side in this tree. */
void era_split_restart_agreement_note_relation(bool serviced, bool initiator, bool local_left);
void era_split_restart_agreement_note_relation_rotation(void);
void era_split_restart_agreement_task(void);

#ifdef ERA_SPLIT_RESTART_AGREEMENT_TEST
/* Host-test reset for the file-static singleton. It is absent from every
 * firmware build and exists only so independent deterministic cases begin at
 * the same BSS-zero state as a real boot. */
void era_split_restart_agreement_test_reset(void);
#endif
