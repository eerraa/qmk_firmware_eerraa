// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "era_split_wire_protocol.h"

/* The agreed restart: both halves of a pair commit the same act at one
 * instant, having prepared for the same thing.
 *
 * One mechanism and two users. A user hands over an `(act, param)` and this
 * unit does the rest -- the raw-HID quiet gate, the two-phase agreement over
 * the wire, the shared-clock deadline, and the degrade to acting alone when
 * there is no peer to agree with. **It knows neither user**: what an act *is*
 * (four properties, `era_split_restart_act_rules[]`) and what an act *does*
 * (`era_split_restart_prepare_local()`) are both declared here by name and
 * defined in era_split_keyboard.c, the one unit that knows both users -- so
 * this unit names no user and no user's constant, and no user names another.
 *
 * **The commanded half requests; the relation's initiator arms.** One rule, no
 * relation-specific branch. In HOST-PEER only the HOST has USB, so it is always
 * HOST to PEER and the HOST is the responder of that relation; in DUAL-HOST
 * either half may command. The request rides the AUTHORITY flags byte because
 * the response section mask has all eight markers assigned; the arm rides the
 * RESTART_ARM push section, which is the initiator's direction, because the
 * initiator is what owns the deadline.
 *
 * The two facts are mutually exclusive on the wire -- arming consumes the
 * request -- so one `armed` bit carries the phase and the act and param fields
 * are shared between them.
 *
 * **What the raw-HID quiet gate covers is the requesting half's own raise, and
 * that is the whole of it.** The initiator's arm for a peer's request and the
 * commit instant itself are not held on this half's own quiet: two halves have
 * two VIA applications and one shared deadline, so no instant is quiet on both
 * by construction. On a half acting alone the gate is adjacent -- quiet
 * observed, prepare, reset -- and on an agreed pair it is best-effort: the
 * commanded half's traffic is the one the gate was built against, and the
 * window on the other half is minimised rather than closed.
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
    ERA_SPLIT_RESTART_ACT_NONE         = 0,
    ERA_SPLIT_RESTART_ACT_LINK_SPEED   = 1,
    ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN = 2,
} era_split_restart_act_t;
/* Three of the four code points the two-bit field can hold are assigned; the
 * fourth is refused by both validators rather than reserved for a meaning
 * nobody has chosen, so a captured 3 is a malformed frame and never an act
 * from an era this image does not know. */
#define ERA_SPLIT_RESTART_ACT_MAX ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN
_Static_assert(ERA_SPLIT_RESTART_ACT_MAX <= ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_VALUE_MAX,
               "The act set must fit the two-bit field both wire carriers give it.");

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
 * **One value for every act, and that is a property of where the prepare
 * runs.** Each act's own work happens at the commit — immediately before the
 * reset when the act resets, or as the runtime apply when it does not — so
 * no act's preparation is inside the window this delay bounds. A per-act
 * delay would only be needed if some act had to do its work *before*
 * answering, which is the ordering trick this arrangement exists to not
 * need. */
#ifndef ERA_SPLIT_RESTART_COMMIT_DELAY_MS
#    define ERA_SPLIT_RESTART_COMMIT_DELAY_MS 120
#endif
/* How long a request stays advertised before the half that made it gives up.
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
 * can hold values no act means: the fourth act code point, and a param outside
 * what the act it accompanies takes. This answers for both, and it is the one
 * thing this unit says about an act besides the two properties in its table --
 * a param range is the act's wire contract, so it belongs beside the act's
 * declaration and not inside the codec that happens to read the bits.
 *
 * `ERA_SPLIT_RESTART_ACT_NONE` is accepted with a zero param, because the idle
 * body is a valid body. */
bool era_split_restart_intent_valid(uint8_t act, uint8_t param);

/* **The whole user-facing surface.** Returns false if the act is not one of
 * the above or if another restart is already pending, which is what makes two
 * acts interleaving impossible rather than unlikely. Returning true means the
 * restart was accepted, not that it has happened: the quiet gate, the
 * agreement and the deadline all still have to run. */
bool era_split_restart_agreement_request(era_split_restart_act_t act, uint8_t param);

/* True from an accepted request through T_commit (or the request's lifetime).
   The VIA Apply USB reattach asks this so `restart_usb_driver()` cannot land
   inside the commit window (`split/era_split_via_link.c`). */
bool era_split_restart_agreement_in_flight(void);

/* **What an act is: four properties, and nothing else about an act lives in
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
     * rates, so a failed agreement must do nothing at all. An EEPROM clean
     * may: a lone erase-and-reset *is* the behaviour this tree already proves,
     * so a failed agreement must still clean the half that was commanded.
     * Requiring confirmation for it would turn a degrade into a command that
     * silently did not happen. */
    bool requires_confirmation;
    /* Whether the arm waits for a storage episode to finish.
     *
     * The link switch does: applying through the owner's VIA save loses the
     * save, and the setting can wait a second. It does not wait on the boot
     * changed-shadow a CLEAN leaves: that lamp is what the relation-open
     * audit clears, and the audit waits for this raise. The clean does not
     * yield: the store is what is being discarded, and a deferred arm would
     * let the commanded half's own deadline fire first and clean one half of
     * the pair. */
    bool yields_to_storage;
    /* Whether the service resets both MCUs after prepare. The clean does; the
     * link switch does not — its prepare is the runtime divider change. */
    bool resets;
    /* The widest param this act may carry on the wire. Both carriers give the
     * field two bits, so an act with no parameter still has to say so or the
     * three values it never means would be accepted. */
    uint8_t param_max;
} era_split_restart_act_rules_t;
extern const era_split_restart_act_rules_t era_split_restart_act_rules[ERA_SPLIT_RESTART_ACT_MAX + 1];

/* The act's own work, run on both halves at the commit instant. When
 * `resets` is set it runs immediately before the reset; when it is not, it
 * *is* the commit. **Declared here and defined in era_split_keyboard.c**, so
 * this unit names no user and no user names another.
 *
 * Every act's preparation belongs here rather than at the request, and the link
 * switch is why: a half that stored its level at the arm and then disarmed
 * would hold a level its peer does not. Nothing is persisted until the pair has
 * agreed, and the pair agreeing is what this instant means. */
void era_split_restart_prepare_local(era_split_restart_act_t act, uint8_t param);
/* Whether the initiator may arm this act now. **Declared here and defined in
 * era_split_keyboard.c** beside prepare. The link act waits for the
 * time-anchor to have been applied on the adopting half; the clean does not
 * wait. */
bool era_split_restart_arm_ready(era_split_restart_act_t act);

/* **Whether a peer produced the commit now running.** Valid only inside
 * `era_split_restart_prepare_local()`, which is the one moment it is asked.
 *
 * True when this half's deadline came from an initiator's arm or from a
 * responder's answer; false when it set its own, which happens on the
 * no-relation degrade and on an act that requires no confirmation. **It is a
 * fact about the deadline and not about the relation**, so an agreement whose
 * wire died inside the commit window still reports true -- both halves hold the
 * same deadline and both will run their prepare.
 *
 * An act stores it only if the difference means something to that act. The link
 * switch does: an unagreed level is one half's claim that the other has never
 * seen, and the record keeps that fact even though nothing acts on it today
 * (`era_split_link.h`'s **Reconciliation**). */
bool era_split_restart_agreement_commit_agreed(void);

/* The wire. This half's request-or-armed fact fills the AUTHORITY section in
 * both directions; the initiator's arm fills the RESTART_ARM push section. An
 * act of NONE is the idle form of each. */
void era_split_restart_agreement_fill_authority(era_split_wire_authority_section_t *authority);
void era_split_restart_agreement_note_peer_authority(const era_split_wire_authority_section_t *authority);
void era_split_restart_agreement_arm_section(uint8_t *act, uint8_t *param, uint32_t *commit_ms);
void era_split_restart_agreement_note_peer_arm(uint8_t act, uint8_t param, uint32_t commit_ms);

/* Lifecycle. The relation term is the arm's own precondition; the rotation
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
