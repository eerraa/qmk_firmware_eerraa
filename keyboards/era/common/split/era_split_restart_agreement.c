// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "era_split_restart_agreement.h"

#ifdef ERA_VIA_SYSTEM_ENABLE
#    include "../system/era_via_system.h"
#endif
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "era_host_peer_storage.h"
#endif
#include "sync_timer.h"
#include "timer.h"
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
#    include "print.h"
#    ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#        include "communication_core/era_split_communication_core_storage.h"
#    endif
#endif
#ifdef ERA_SPLIT_RESTART_AGREEMENT_TEST
#    include <string.h>
#endif

/* No act table here, and no user's header: what an act *is* reaches this unit
   the same way what an act *does* reaches it -- through
   era_split_restart_act_rules[], declared by this unit's header and defined
   beside era_split_restart_prepare_local() in era_split_keyboard.c, the one
   unit that knows both users. This file names no user's constant. */

bool era_split_restart_intent_valid(uint8_t act, uint8_t param) {
    return act <= ERA_SPLIT_RESTART_ACT_MAX && param <= era_split_restart_act_rules[act].param_max;
}

bool era_split_restart_authority_valid(uint8_t act, uint8_t param, bool armed) {
    if (act > ERA_SPLIT_RESTART_ACT_MAX) {
        return false;
    }
    if (act == ERA_SPLIT_RESTART_ACT_NONE) {
        return param == 0 && !armed;
    }
    if (act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN) {
        return (!armed && param == ERA_SPLIT_RESTART_CLEAN_PARAM_REQUEST) ||
               (armed && (param == ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED ||
                          param == ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT));
    }
    return param <= era_split_restart_act_rules[act].param_max;
}

bool era_split_restart_arm_valid(uint8_t act, uint8_t param, uint32_t commit_ms) {
    if (act > ERA_SPLIT_RESTART_ACT_MAX) {
        return false;
    }
    if (act == ERA_SPLIT_RESTART_ACT_NONE) {
        return param == 0 && commit_ms == 0;
    }
    if (act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN) {
        return (param == ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED && commit_ms == 0) ||
               (param == ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT && commit_ms != 0);
    }
    return param <= era_split_restart_act_rules[act].param_max && commit_ms != 0;
}

static struct {
    /* The accepted request, still waiting on this half's own raw-HID quiet.
     * Both users hand their act over here and neither runs a gate of its own,
     * which is also what makes "one pending fact" one test. */
    bool     pending;
    uint8_t  pending_act;
    uint8_t  pending_param;
    uint32_t pending_requested_ms;

    /* The intent this half advertises. At most one of this and the commit below
     * is published, because arming consumes the request -- which is what lets
     * both share one act field and one param field in the AUTHORITY flags. */
    bool     request_live;
    uint8_t  request_act;
    uint8_t  request_param;
    uint32_t request_started_ms;

    /* The commit, and there is exactly one of these however it was reached:
     * adopted from an initiator's arm, confirmed by a responder's answer, or
     * set locally by a half that has nobody to agree with. `commit_agreed`
     * records which of those three it was, because for at least one act the
     * difference is a fact worth storing. */
    bool     commit_armed;
    bool     commit_agreed;
    uint8_t  commit_act;
    uint8_t  commit_param;
    uint32_t commit_ms;

    /* The initiator's advertised arm. `commit_armed` above is the deadline and
     * survives a relation rotation once confirmed; this is the wire fact. */
    bool     arm_live;
    uint8_t  arm_act;
    uint8_t  arm_param;
    uint32_t arm_started_ms;

    /* The peer's advertised intent, latest-state off the AUTHORITY section. */
    uint8_t peer_act;
    uint8_t peer_param;
    bool    peer_armed;

    bool relation_serviced;
    bool relation_initiator;
    bool relation_local_left;

    /* After a commit, a peer request that was already in flight must not be
     * the next arm. Cleared when the peer advertises idle. Boot is false, so
     * the first peer request of a relation can be armed. */
    bool peer_request_suppressed;

    /* CLEAN is prepared before either deadline exists. Once selected, storage
     * stays quarantined even if the link rotates or the physical replay fails:
     * a half whose boot predicate may already be OFF must never re-advertise
     * its old portable image. A failed reboot-durable prepare is deliberately
     * sticky; the local backing state is not a publishable old image. */
    bool clean_selected;
    bool clean_standalone;
    bool clean_local_prepared;
    bool clean_prepare_failed;
    bool clean_waiting_disarm;
    bool clean_disarm_observed_prepared;
} g_era_split_restart_agreement;

enum {
    ERA_SPLIT_RESTART_CLEAN_TRACE_REQUEST = 1,
    ERA_SPLIT_RESTART_CLEAN_TRACE_SELECTED,
    ERA_SPLIT_RESTART_CLEAN_TRACE_PREPARE_RX,
    ERA_SPLIT_RESTART_CLEAN_TRACE_PREPARED,
    ERA_SPLIT_RESTART_CLEAN_TRACE_PREPARE_FAIL,
    ERA_SPLIT_RESTART_CLEAN_TRACE_COMMIT_TX,
    ERA_SPLIT_RESTART_CLEAN_TRACE_COMMIT_RX,
    ERA_SPLIT_RESTART_CLEAN_TRACE_COMMIT_ECHO,
    ERA_SPLIT_RESTART_CLEAN_TRACE_DISARM,
    ERA_SPLIT_RESTART_CLEAN_TRACE_RESET,
};

#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
static void era_split_restart_agreement_clean_trace(uint8_t event) {
    uint32_t core_claim    = 0;
    uint32_t core_tx       = 0;
    uint32_t core_rx       = 0;
    uint32_t core_publish  = 0;
    uint32_t core_fail     = 0;
    uint16_t request_claim = 0;
    uint16_t result_claim  = 0;
    uint16_t result_ready  = 0;
    uint32_t transfer      = 0;
    uint32_t apply         = 0;
    uint32_t complete      = 0;
    uint32_t abort         = 0;
    uint32_t timeout       = 0;
    uint8_t  storage_active = 0;
#    ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    era_split_communication_core_storage_probe_diagnostics_t core;
    era_host_peer_storage_diagnostics_t                      storage;
    era_split_communication_core_storage_get_probe_diagnostics(&core);
    era_host_peer_storage_get_diagnostics_snapshot(&storage);
    core_claim     = core.claim_count;
    core_tx        = core.tx_count;
    core_rx        = core.rx_count;
    core_publish   = core.publish_count;
    core_fail      = core.failure_count;
    request_claim  = core.request_claim_generation;
    result_claim   = core.result_claim_generation;
    result_ready   = core.result_ready_generation;
    transfer       = storage.transfer_count;
    apply          = storage.apply_count;
    complete       = storage.complete_count;
    abort          = storage.abort_count;
    timeout        = storage.timeout_count;
    storage_active = storage.active;
#    endif
    uprintf("wire clean ev=%u t=%lu i=%u l=%u lp=%u pp=%u pa=%u ap=%u dl=%lu cq=%lu/%lu/%lu/%lu/%lu/%u/%u/%u hs=%lu/%lu/%lu/%lu/%lu/%u\r\n",
            (unsigned)event, (unsigned long)timer_read32(),
            (unsigned)g_era_split_restart_agreement.relation_initiator,
            (unsigned)g_era_split_restart_agreement.relation_local_left,
            (unsigned)g_era_split_restart_agreement.clean_local_prepared,
            (unsigned)(g_era_split_restart_agreement.peer_act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN &&
                       g_era_split_restart_agreement.peer_param == ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED &&
                       g_era_split_restart_agreement.peer_armed),
            (unsigned)g_era_split_restart_agreement.peer_param,
            (unsigned)(g_era_split_restart_agreement.arm_live ? g_era_split_restart_agreement.arm_param : 0),
            (unsigned long)g_era_split_restart_agreement.commit_ms,
            (unsigned long)core_claim, (unsigned long)core_tx,
            (unsigned long)core_rx, (unsigned long)core_publish,
            (unsigned long)core_fail, (unsigned)request_claim,
            (unsigned)result_claim, (unsigned)result_ready,
            (unsigned long)transfer, (unsigned long)apply,
            (unsigned long)complete, (unsigned long)abort,
            (unsigned long)timeout, (unsigned)storage_active);
}
#else
#    define era_split_restart_agreement_clean_trace(event) ((void)(event))
#endif

static bool era_split_restart_agreement_act_valid(uint8_t act) {
    return act != ERA_SPLIT_RESTART_ACT_NONE && act <= ERA_SPLIT_RESTART_ACT_MAX;
}

static bool era_split_restart_agreement_quiet(uint32_t requested_ms) {
#ifdef ERA_VIA_SYSTEM_ENABLE
    /* One policy, owned by era_via_system.c, asked by every VIA cut. */
    return era_via_system_restart_quiet_ok(requested_ms);
#else
    /* No raw-HID surface is compiled here, so there is no application traffic
       for the reset to fall between -- and no way to ask for a restart either,
       since every requester is a VIA control. The gate reads as already quiet
       rather than standing as a second guard on a path nothing reaches. This
       unit still compiles for every split board, because the wire carries the
       section whether or not this build can raise one. */
    (void)requested_ms;
    return true;
#endif
}

static bool era_split_restart_agreement_storage_busy(void) {
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    return era_host_peer_storage_restart_should_wait();
#else
    return false;
#endif
}

static bool era_split_restart_agreement_storage_quarantine_ready(void) {
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    return era_host_peer_storage_restart_quarantine_ready();
#else
    return true;
#endif
}

static void era_split_restart_agreement_arm_commit(uint8_t act, uint8_t param, uint32_t commit_ms, bool agreed) {
    g_era_split_restart_agreement.commit_armed  = true;
    g_era_split_restart_agreement.commit_agreed = agreed;
    g_era_split_restart_agreement.commit_act    = act;
    g_era_split_restart_agreement.commit_param  = param;
    g_era_split_restart_agreement.commit_ms     = commit_ms;
}

bool era_split_restart_agreement_commit_agreed(void) {
    return g_era_split_restart_agreement.commit_agreed;
}

bool era_split_restart_agreement_request(era_split_restart_act_t act, uint8_t param) {
    if (!era_split_restart_agreement_act_valid((uint8_t)act) ||
        !era_split_restart_intent_valid((uint8_t)act, param)) {
        return false;
    }
    /* **One pending fact.** A second request while one is in flight is refused
       rather than queued, which is what makes two acts interleaving impossible
       instead of unlikely -- and the refusal reaches the owner as a control
       that did nothing, which is the right answer for a board that is about to
       reset for the first thing they asked for. */
    if (g_era_split_restart_agreement.pending || g_era_split_restart_agreement.request_live ||
        g_era_split_restart_agreement.commit_armed || g_era_split_restart_agreement.clean_selected ||
        g_era_split_restart_agreement.clean_prepare_failed) {
        return false;
    }
    g_era_split_restart_agreement.pending              = true;
    g_era_split_restart_agreement.pending_act          = (uint8_t)act;
    g_era_split_restart_agreement.pending_param        = param;
    g_era_split_restart_agreement.pending_requested_ms = timer_read32();
    return true;
}

bool era_split_restart_agreement_in_flight(void) {
    return g_era_split_restart_agreement.pending || g_era_split_restart_agreement.request_live ||
           g_era_split_restart_agreement.commit_armed || g_era_split_restart_agreement.arm_live ||
           g_era_split_restart_agreement.clean_selected || g_era_split_restart_agreement.clean_prepare_failed;
}

bool era_split_restart_agreement_storage_quarantined(void) {
    return g_era_split_restart_agreement.clean_selected || g_era_split_restart_agreement.clean_prepare_failed;
}

void era_split_restart_agreement_fill_authority(era_split_wire_authority_section_t *authority) {
    if (authority == NULL) {
        return;
    }
    /* A prepared CLEAN outranks its original request. It is the durable vote
       the initiator must see before it may create a deadline; COMMIT is the
       echo that confirms adoption of that deadline. */
    if (g_era_split_restart_agreement.clean_selected && g_era_split_restart_agreement.clean_local_prepared) {
        authority->restart_act   = ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN;
        authority->restart_param = g_era_split_restart_agreement.commit_armed ?
                                       ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT :
                                       ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED;
        authority->restart_armed = true;
        return;
    }

    /* **The request outranks the armed fact in what this half publishes**, and
       the order is load-bearing rather than arbitrary: a half that holds a
       local deadline for a no-confirmation act holds both at once, and
       publishing `armed` there would hide the request the initiator has to see
       before it can arm. Adopting an arm clears the request, so the two are
       exclusive from that moment on and the phase reads unambiguously. */
    if (g_era_split_restart_agreement.request_live) {
        authority->restart_act   = g_era_split_restart_agreement.request_act;
        authority->restart_param = g_era_split_restart_agreement.request_param;
        authority->restart_armed = false;
        return;
    }
    if (g_era_split_restart_agreement.commit_armed) {
        authority->restart_act   = g_era_split_restart_agreement.commit_act;
        authority->restart_param = g_era_split_restart_agreement.commit_param;
        authority->restart_armed = true;
        return;
    }
    authority->restart_act   = ERA_SPLIT_RESTART_ACT_NONE;
    authority->restart_param = 0;
    authority->restart_armed = false;
}

void era_split_restart_agreement_note_peer_authority(const era_split_wire_authority_section_t *authority) {
    if (authority == NULL) {
        return;
    }
    g_era_split_restart_agreement.peer_act   = authority->restart_act;
    g_era_split_restart_agreement.peer_param = authority->restart_param;
    g_era_split_restart_agreement.peer_armed = authority->restart_armed;
    if (authority->restart_act == ERA_SPLIT_RESTART_ACT_NONE) {
        g_era_split_restart_agreement.peer_request_suppressed = false;
    }

    if (g_era_split_restart_agreement.clean_waiting_disarm &&
        authority->restart_act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN &&
        authority->restart_param == ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED && authority->restart_armed) {
        g_era_split_restart_agreement.clean_disarm_observed_prepared = true;
    }

    /* CLEAN's first arm has no deadline. Its AUTHORITY PREPARED answer is
       consumed by the task only after this half's own reboot-durable prepare also
       succeeded. The second arm carries T_commit; only the matching COMMIT
       echo lets the initiator adopt that same deadline. */
    if (g_era_split_restart_agreement.arm_live &&
        g_era_split_restart_agreement.arm_act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN) {
        if (g_era_split_restart_agreement.arm_param == ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT &&
            authority->restart_act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN &&
            authority->restart_param == ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT && authority->restart_armed &&
            !g_era_split_restart_agreement.commit_armed) {
            era_split_restart_agreement_arm_commit(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                                   ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT,
                                                   g_era_split_restart_agreement.commit_ms, true);
            era_split_restart_agreement_clean_trace(ERA_SPLIT_RESTART_CLEAN_TRACE_COMMIT_ECHO);
        }
        return;
    }

    /* The initiator's confirmation, and the one place it commits to a deadline.
     * Past this point neither half aborts: a dead wire from here on leaves both
     * holding the same deadline and they meet on the far side of it.
     *
     * Both fields are compared and not just the armed bit. The responder echoes
     * what it armed for, and the echo costs nothing because the act and param
     * fields exist in this byte either way -- so the check that the two halves
     * armed for the same thing is free. Without it, a responder still holding
     * `armed` from a previous episode would confirm the current one. */
    if (g_era_split_restart_agreement.arm_live && g_era_split_restart_agreement.peer_armed &&
        g_era_split_restart_agreement.peer_act == g_era_split_restart_agreement.arm_act &&
        g_era_split_restart_agreement.peer_param == g_era_split_restart_agreement.arm_param) {
        if (!g_era_split_restart_agreement.commit_armed) {
            era_split_restart_agreement_arm_commit(g_era_split_restart_agreement.arm_act,
                                                   g_era_split_restart_agreement.arm_param,
                                                   g_era_split_restart_agreement.commit_ms, true);
        } else {
            /* An act that needs no confirmation committed the moment it armed,
               so the branch above never runs for it -- but the answer still
               arrived, and the deadline it is holding is the one the peer
               adopted. Recording that keeps `commit_agreed` a statement about
               the pair rather than about which branch happened to set it. */
            g_era_split_restart_agreement.commit_agreed = true;
        }
    }
}

void era_split_restart_agreement_arm_section(uint8_t *act, uint8_t *param, uint32_t *commit_ms) {
    if (act == NULL || param == NULL || commit_ms == NULL) {
        return;
    }
    /* The idle form is one canonical all-zero body, which is what gives the
     * validator a reserved-zero rule to enforce on a section whose live form
     * carries a deadline. */
    *act       = g_era_split_restart_agreement.arm_live ? g_era_split_restart_agreement.arm_act : ERA_SPLIT_RESTART_ACT_NONE;
    *param     = g_era_split_restart_agreement.arm_live ? g_era_split_restart_agreement.arm_param : 0;
    *commit_ms = g_era_split_restart_agreement.arm_live ? g_era_split_restart_agreement.commit_ms : 0;
}

void era_split_restart_agreement_note_peer_arm(uint8_t act, uint8_t param, uint32_t commit_ms) {
    if (g_era_split_restart_agreement.relation_initiator) {
        /* The initiator originates this section and never consumes one. A
         * frame carrying it here is the eligibility table failing, not a
         * handshake. */
        return;
    }
    if (act == ERA_SPLIT_RESTART_ACT_NONE) {
        /* The disarm, and it is what closes the hole an unanswered arm leaves.
         * The responder cannot know its answer arrived, so it does not decide:
         * it holds the deadline only while the initiator still advertises the
         * arm, and the initiator retires that on its own timeout well before
         * the deadline.
         *
         * An act that needs no confirmation is not disarmed by it. Its deadline
         * was not the initiator's to give -- the commanded half set its own the
         * moment it asked -- so retiring the arm withdraws the agreement and
         * leaves the degrade, which for that act is the proven behaviour. */
        if (g_era_split_restart_agreement.commit_armed &&
            era_split_restart_act_rules[g_era_split_restart_agreement.commit_act].requires_confirmation) {
            g_era_split_restart_agreement.commit_armed = false;
        }
        return;
    }
    if (!era_split_restart_agreement_act_valid(act)) {
        return;
    }
    if ((g_era_split_restart_agreement.clean_selected ||
         g_era_split_restart_agreement.clean_prepare_failed) &&
        act != ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN) {
        /* CLEAN selection is monotonic for this boot. A delayed arm from an
           earlier act cannot interleave persistence or a second deadline with
           a half whose boot predicate may already be OFF. */
        return;
    }

    if (act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN) {
        if (param == ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED && commit_ms == 0) {
            /* PREPARE is intentionally deadline-free. Receipt quarantines the
               storage engine at once; the cold task waits for any admitted
               episode to reach a coherent boundary before doing the checked
               one-word write. A duplicate is idempotent. */
            if (g_era_split_restart_agreement.commit_armed || g_era_split_restart_agreement.clean_prepare_failed) {
                return;
            }
            bool first_prepare = !g_era_split_restart_agreement.clean_selected;
            g_era_split_restart_agreement.clean_selected = true;
            g_era_split_restart_agreement.pending        = false;
            g_era_split_restart_agreement.request_live   = false;
            if (first_prepare) {
                era_split_restart_agreement_clean_trace(ERA_SPLIT_RESTART_CLEAN_TRACE_PREPARE_RX);
            }
            return;
        }
        if (param != ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT || commit_ms == 0 ||
            !g_era_split_restart_agreement.clean_selected ||
            !g_era_split_restart_agreement.clean_local_prepared ||
            g_era_split_restart_agreement.clean_prepare_failed) {
            return;
        }
        /* Both durable PREPARED votes crossed before the initiator could emit
           COMMIT. Only now may this responder adopt its shared-clock deadline. */
        int32_t remaining_ms = (int32_t)(commit_ms - sync_timer_read32());
        if (remaining_ms <= 0 || remaining_ms > ERA_SPLIT_RESTART_COMMIT_DELAY_MS) {
            return;
        }
        era_split_restart_agreement_arm_commit(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                               ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT, commit_ms, true);
        g_era_split_restart_agreement.request_live = false;
        era_split_restart_agreement_clean_trace(ERA_SPLIT_RESTART_CLEAN_TRACE_COMMIT_RX);
        return;
    }

    /* The deadline is on the shared clock, and the responder is its source --
       so this half's reading is the authority and a deadline that does not sit
       inside the initiator's own commit window means the initiator was working
       from an unanchored clock. Refusing is the safe answer: the initiator
       times out, retires the arm, and the owner retries once the anchor has
       crossed, which happens once per relation open. Taking it instead would
       commit the two halves at instants seconds apart. */
    int32_t remaining_ms = (int32_t)(commit_ms - sync_timer_read32());
    if (remaining_ms <= 0 || remaining_ms > ERA_SPLIT_RESTART_COMMIT_DELAY_MS) {
        return;
    }
    era_split_restart_agreement_arm_commit(act, param, commit_ms, true);
    g_era_split_restart_agreement.request_live = false;
}

void era_split_restart_agreement_note_relation(bool serviced, bool initiator, bool local_left) {
    g_era_split_restart_agreement.relation_serviced   = serviced;
    g_era_split_restart_agreement.relation_initiator  = initiator;
    g_era_split_restart_agreement.relation_local_left = local_left;

    if (serviced && g_era_split_restart_agreement.clean_selected &&
        g_era_split_restart_agreement.clean_standalone) {
        /* A no-relation CLEAN may wait at its Core1 drain barrier. If a peer
           becomes serviced in that interval, local-only reset is no longer a
           safe degrade: the peer's still-valid store could restore the old
           image after reboot. Promotion into bilateral roll-forward is
           monotonic; a later relation loss waits for reopen rather than
           downgrading this obligation to standalone again. */
        g_era_split_restart_agreement.clean_standalone = false;
    }

    /* **A commit's wire face follows the initiator role.** A half that holds a
       commit a peer produced -- adopted from an arm, or an own arm the peer
       confirmed -- and finds itself the initiator with no arm advertised is a
       half whose roles flipped inside the commit window: the arm it adopted was
       the old initiator's, and that half is now the responder that consumes
       arms rather than the one that carries them. Left as it was, the commit
       would have no carrier for the rest of the window: an unconfirmed arm
       dropped at the rotation on the other side leaves that half with nothing,
       and this half resets alone at the deadline it adopted; a confirmed one
       survives there, but the idle body this half would otherwise be sending
       reads to it as a disarm. So the commit is re-advertised from here, with
       the deadline it already holds -- the responder validates it against its
       own clock as it validates any arm and re-adopts or keeps it -- and both
       halves meet at the instant they had agreed. Only peer-produced commits:
       a local deadline (`commit_agreed` false) is the degrade or the
       no-confirmation act's own floor, and each of those has its carrier
       already. Idempotent, and it costs nothing on the passes that are not a
       flip. */
    if (initiator && g_era_split_restart_agreement.commit_armed && g_era_split_restart_agreement.commit_agreed &&
        !g_era_split_restart_agreement.arm_live) {
        g_era_split_restart_agreement.arm_live       = true;
        g_era_split_restart_agreement.arm_act        = g_era_split_restart_agreement.commit_act;
        g_era_split_restart_agreement.arm_param      = g_era_split_restart_agreement.commit_param;
        g_era_split_restart_agreement.arm_started_ms = timer_read32();
    }
}

void era_split_restart_agreement_note_relation_rotation(void) {
    /* The wire facts drop and the commit does not.
     *
     * **An arm survives a rotation once it has been confirmed**, and that is
     * the whole of this function's subtlety. Before confirmation the arm is a
     * proposal and dropping it is the abort both halves agree on -- the
     * responder sees the idle body on the reopened relation and disarms with
     * it. After confirmation the arm is the commit's wire face: the responder
     * is holding the same deadline, and retiring the arm would read to it as an
     * abort it must not take, leaving the initiator to reset alone. Any USB
     * authority edge produces a rotation, so this is a case the pair reaches,
     * not one it might.
     *
     * The sent shadow is cleared by the same rotation, so a surviving arm
     * re-crosses on the new relation and the responder re-validates its
     * deadline against its own clock. The peer's cache does not survive,
     * because a reopened peer holds nothing and a stale shadow would leave it
     * at zero; the request does, bounded by its own lifetime, so a rotation
     * immediately after the owner's click does not silently drop the action.
     *
     * A CLEAN COMMIT whose echo was lost is the one unconfirmed proposal that
     * may already be a live deadline on the peer. Rotation still retires that
     * proposal, but it must preserve the disarm wait: the reopened relation's
     * forced idle body cancels the peer's old deadline, and only a PREPARED
     * answer observed after that idle permits a fresh COMMIT. */
    bool clean_disarm_required =
        g_era_split_restart_agreement.clean_waiting_disarm ||
        (g_era_split_restart_agreement.clean_selected &&
         g_era_split_restart_agreement.clean_local_prepared &&
         !g_era_split_restart_agreement.commit_armed &&
         g_era_split_restart_agreement.arm_live &&
         g_era_split_restart_agreement.arm_act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN &&
         g_era_split_restart_agreement.arm_param == ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT);

    if (!g_era_split_restart_agreement.commit_armed) {
        g_era_split_restart_agreement.arm_live = false;
    }
    g_era_split_restart_agreement.peer_act   = ERA_SPLIT_RESTART_ACT_NONE;
    g_era_split_restart_agreement.peer_param = 0;
    g_era_split_restart_agreement.peer_armed = false;
    g_era_split_restart_agreement.clean_waiting_disarm            = clean_disarm_required;
    g_era_split_restart_agreement.clean_disarm_observed_prepared  = false;
}

static bool era_split_restart_agreement_peer_clean_prepared(void) {
    return g_era_split_restart_agreement.peer_act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN &&
           g_era_split_restart_agreement.peer_param == ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED &&
           g_era_split_restart_agreement.peer_armed;
}

static void era_split_restart_agreement_clean_arm_prepare(void) {
    g_era_split_restart_agreement.arm_live       = true;
    g_era_split_restart_agreement.arm_act        = ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN;
    g_era_split_restart_agreement.arm_param      = ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED;
    g_era_split_restart_agreement.commit_ms      = 0;
    g_era_split_restart_agreement.arm_started_ms = timer_read32();
    g_era_split_restart_agreement.request_live   = false;
}

static void era_split_restart_agreement_clean_arm_commit(void) {
    g_era_split_restart_agreement.arm_live       = true;
    g_era_split_restart_agreement.arm_act        = ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN;
    g_era_split_restart_agreement.arm_param      = ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT;
    g_era_split_restart_agreement.commit_ms      = sync_timer_read32() + ERA_SPLIT_RESTART_COMMIT_DELAY_MS;
    g_era_split_restart_agreement.arm_started_ms = timer_read32();
    g_era_split_restart_agreement.clean_waiting_disarm           = false;
    g_era_split_restart_agreement.clean_disarm_observed_prepared = false;
    era_split_restart_agreement_clean_trace(ERA_SPLIT_RESTART_CLEAN_TRACE_COMMIT_TX);
}

static void era_split_restart_agreement_commit(void);

/* Returns true once CLEAN owns the agreement service for this boot. */
static bool era_split_restart_agreement_clean_task(void) {
    if (!g_era_split_restart_agreement.clean_selected) {
        return false;
    }
    if (g_era_split_restart_agreement.clean_prepare_failed) {
        return true;
    }

    if (!g_era_split_restart_agreement.clean_local_prepared) {
        /* Quarantine was raised before this test. The cold capture task sees
           it before this point; the runtime task later in housekeeping rolls
           an admitted Apply to coherence and tears every other episode down.
           The dedicated Core1 publication barrier therefore becomes ready on
           this or a later restart-task entry, never before those owners have
           released their state. */
        if (!era_split_restart_agreement_storage_quarantine_ready()) {
            return true;
        }
        if (!era_split_restart_prepare_local(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                             ERA_SPLIT_RESTART_CLEAN_PARAM_REQUEST)) {
            g_era_split_restart_agreement.clean_prepare_failed = true;
            g_era_split_restart_agreement.arm_live             = false;
            g_era_split_restart_agreement.commit_armed          = false;
            g_era_split_restart_agreement.request_live          = false;
            g_era_split_restart_agreement.pending               = false;
            era_split_restart_agreement_clean_trace(ERA_SPLIT_RESTART_CLEAN_TRACE_PREPARE_FAIL);
            return true;
        }
        g_era_split_restart_agreement.clean_local_prepared = true;
        era_split_restart_agreement_clean_trace(ERA_SPLIT_RESTART_CLEAN_TRACE_PREPARED);
    }

    if (g_era_split_restart_agreement.clean_standalone) {
        /* No peer and no shared clock mean there is nothing to schedule. The
           requesting half already passed its raw-HID quiet gate; checked
           prepare is the final fallible boundary, so reset immediately after
           it succeeds. */
        era_split_restart_agreement_arm_commit(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                               ERA_SPLIT_RESTART_CLEAN_PARAM_REQUEST,
                                               sync_timer_read32(), false);
        era_split_restart_agreement_commit();
        return true;
    }

    if (!g_era_split_restart_agreement.relation_serviced ||
        !g_era_split_restart_agreement.relation_initiator ||
        g_era_split_restart_agreement.commit_armed) {
        return true;
    }

    if (g_era_split_restart_agreement.arm_live) {
        if (g_era_split_restart_agreement.arm_param == ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED) {
            if (era_split_restart_agreement_peer_clean_prepared() &&
                era_split_restart_arm_ready(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN)) {
                era_split_restart_agreement_clean_arm_commit();
            }
            return true;
        }
        if (g_era_split_restart_agreement.arm_param == ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT) {
            if (timer_elapsed32(g_era_split_restart_agreement.arm_started_ms) >= ERA_SPLIT_RESTART_ARM_TIMEOUT_MS) {
                /* Retire the unanswered deadline and expose idle long enough
                   for a PREPARED answer observed after this point to prove the
                   responder processed that retirement. Both boot predicates
                   are already OFF, so even a responder that reaches the old
                   deadline cannot restore pre-CLEAN storage. */
                g_era_split_restart_agreement.arm_live                        = false;
                g_era_split_restart_agreement.clean_waiting_disarm            = true;
                g_era_split_restart_agreement.clean_disarm_observed_prepared  = false;
                era_split_restart_agreement_clean_trace(ERA_SPLIT_RESTART_CLEAN_TRACE_DISARM);
            }
            return true;
        }
    }

    if (g_era_split_restart_agreement.clean_waiting_disarm) {
        if (!g_era_split_restart_agreement.clean_disarm_observed_prepared) {
            return true;
        }
        g_era_split_restart_agreement.clean_waiting_disarm = false;
    }

    if (era_split_restart_agreement_peer_clean_prepared() &&
        era_split_restart_arm_ready(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN)) {
        era_split_restart_agreement_clean_arm_commit();
    } else {
        era_split_restart_agreement_clean_arm_prepare();
    }
    return true;
}

static void era_split_restart_agreement_commit(void) {
    uint8_t act      = g_era_split_restart_agreement.commit_act;
    bool    prepared = true;
    if (act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN && g_era_split_restart_agreement.clean_selected) {
        prepared = g_era_split_restart_agreement.clean_local_prepared &&
                   !g_era_split_restart_agreement.clean_prepare_failed;
    } else {
        prepared = era_split_restart_prepare_local((era_split_restart_act_t)act,
                                                   g_era_split_restart_agreement.commit_param);
    }
    g_era_split_restart_agreement.commit_armed            = false;
    g_era_split_restart_agreement.arm_live                = false;
    g_era_split_restart_agreement.request_live            = false;
    g_era_split_restart_agreement.pending                 = false;
    g_era_split_restart_agreement.peer_request_suppressed = true;
    if (!prepared) {
        if (act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN) {
            g_era_split_restart_agreement.clean_prepare_failed = true;
        }
        return;
    }
    if (era_split_restart_act_rules[act].resets) {
        if (act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN) {
            era_split_restart_agreement_clean_trace(ERA_SPLIT_RESTART_CLEAN_TRACE_RESET);
        }
        soft_reset_keyboard();
    }
}

/* The request is raised here rather than in era_split_restart_agreement_request()
   because the quiet gate is a wait and a VIA handler may not wait. Two things
   are decided at the same instant, and both are consequences of one question --
   is there anyone to agree with, and does this act need their answer:

   - **No serviced relation**: nobody to agree with, so this half takes its own
     deadline and acts alone. This is not an error path. It is how a pair that
     cannot hold a link at the current rate is set at all, each half over its
     own USB, and it is how a clean behaves on a half with no cable to its peer.
   - **A serviced relation and an act that needs no confirmation**: the request
      is advertised *and* a local deadline is taken, one arm timeout further out
      so the initiator's arm has a full attempt to arrive and replace it. If it
      does, the two halves share the instant exactly; if it does not, the
      commanded half still does what it was told, bounded, rather than waiting
      out the request lifetime. No resetting act currently uses this policy.
   - **A serviced relation and an act that needs confirmation**: the request
     alone. No deadline exists until the initiator gives one. */
static void era_split_restart_agreement_raise(uint8_t act, uint8_t param) {
    if (!g_era_split_restart_agreement.relation_serviced) {
        if (act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN) {
            /* Standalone CLEAN still raises the same monotonic quarantine so
               stale dedicated publications cannot cross if a relation is
               concurrently reopening. It has no peer phase or deadline: the
               CLEAN task resets immediately after the drain barrier and the
               physical boot-replay proof. */
            g_era_split_restart_agreement.clean_selected   = true;
            g_era_split_restart_agreement.clean_standalone = true;
            era_split_restart_agreement_clean_trace(ERA_SPLIT_RESTART_CLEAN_TRACE_REQUEST);
            era_split_restart_agreement_clean_trace(ERA_SPLIT_RESTART_CLEAN_TRACE_SELECTED);
            return;
        }
        era_split_restart_agreement_arm_commit(act, param, sync_timer_read32() + ERA_SPLIT_RESTART_COMMIT_DELAY_MS, false);
        return;
    }

    g_era_split_restart_agreement.request_live       = true;
    g_era_split_restart_agreement.request_act        = act;
    g_era_split_restart_agreement.request_param      = param;
    g_era_split_restart_agreement.request_started_ms = timer_read32();
    if (act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN) {
        era_split_restart_agreement_clean_trace(ERA_SPLIT_RESTART_CLEAN_TRACE_REQUEST);
    }

    if (!era_split_restart_act_rules[act].requires_confirmation) {
        era_split_restart_agreement_arm_commit(act, param,
                                               sync_timer_read32() + ERA_SPLIT_RESTART_ARM_TIMEOUT_MS +
                                                   ERA_SPLIT_RESTART_COMMIT_DELAY_MS,
                                               false);
    }
}

void era_split_restart_agreement_task(void) {
    /* The commit, first, because everything below is a way of reaching it and
     * none of them may delay it. The comparison is signed against the shared
     * clock, which the responder sources and the initiator adopts, so both
     * halves resolve the same instant to within the anchor's accuracy. */
    if (g_era_split_restart_agreement.commit_armed &&
        (int32_t)(sync_timer_read32() - g_era_split_restart_agreement.commit_ms) >= 0) {
        era_split_restart_agreement_commit();
        return;
    }

    if (g_era_split_restart_agreement.pending &&
        era_split_restart_agreement_quiet(g_era_split_restart_agreement.pending_requested_ms)) {
        g_era_split_restart_agreement.pending = false;
        era_split_restart_agreement_raise(g_era_split_restart_agreement.pending_act,
                                          g_era_split_restart_agreement.pending_param);
    }

    if (g_era_split_restart_agreement.request_live &&
        timer_elapsed32(g_era_split_restart_agreement.request_started_ms) >= ERA_SPLIT_RESTART_REQUEST_LIFETIME_MS) {
        /* The request gives up on its own, and that is what makes the whole
           exchange terminate. A request is latest-state, so a half that keeps
           advertising one keeps being armed against -- and an arm that never
           reaches its peer retires on its own timeout and is immediately
           re-made from the same standing request. Expiring the request at the
           half that made it bounds that from the one side that knows how long
           it has been asking. */
        g_era_split_restart_agreement.request_live = false;
    }

    if (era_split_restart_agreement_clean_task()) {
        return;
    }

    if (!g_era_split_restart_agreement.relation_initiator) {
        /* The responder originates a request and nothing else. Its commit is
         * driven entirely by what the initiator advertises, or by the local
         * deadline it took when it asked, which is what keeps one half owning
         * the instant whenever there is an agreement to own. */
        return;
    }

    if (g_era_split_restart_agreement.arm_live) {
        if (!g_era_split_restart_agreement.commit_armed &&
            timer_elapsed32(g_era_split_restart_agreement.arm_started_ms) >= ERA_SPLIT_RESTART_ARM_TIMEOUT_MS) {
            /* Retiring the arm is the responder's disarm signal as well as this
             * half's abort, so the request goes with it and the owner retries
             * rather than this looping against a peer that will not answer.
             *
             * It is reached only by an act that requires confirmation: the
             * others set the commit at the same instant they arm, so the guard
             * above is false for them and their arm stands until the deadline
             * passes. */
            g_era_split_restart_agreement.arm_live     = false;
            g_era_split_restart_agreement.request_live = false;
        }
        return;
    }
    /* A commit this half holds but did not raise itself is final: it is about to
     * reset, and arming something else would be a second agreement inside the
     * first. **The test is the request and not the commit**, because a
     * no-confirmation act raised on this half holds both at once -- its local
     * deadline is the degrade, and its arm is what still has to cross for the
     * peer to join. Testing the commit here would leave the initiator's own
     * clean resetting one half of a joined pair, which is exactly the silent
     * degrade the act table exists to avoid. */
    if (g_era_split_restart_agreement.commit_armed && !g_era_split_restart_agreement.request_live) {
        return;
    }

    /* **When both halves ask at once, Left wins**, and the header carries why
     * that is the answer rather than another. Only the initiator arms, so the
     * rule is enforced from whichever side that is, and there is no clock in
     * it on either side.
     *
     * A **Left** initiator already holds the answer and takes its own request
     * first -- and while that request is still pending behind its own quiet
     * gate it arms nothing, because arming the peer's request there would let
     * the peer win whenever Left's VIA happened to be talking. That is a local
     * precedence, not a wait: Left knows it has a request in flight without
     * asking the wire. A **Right** initiator has to be given the answer, so it
     * arms the peer's request when one is visible and its own otherwise. It
     * used to hold its own for a derived window first, against a Left request
     * that might still be behind Left's quiet gate; the window is gone because
     * a Right initiator holds no request to hold (the header carries what that
     * rests on and what it gives up).
     *
     * The armed bit is what separates a request from the answer to an arm this
     * half has already made. An initiator's own request is left advertised
     * while it arms for the peer's: nothing takes it, the commit resets both
     * halves out of the state a moment later, and suppressing it would cost a
     * second place where the two requests are compared. */
    if (g_era_split_restart_agreement.relation_local_left && g_era_split_restart_agreement.pending) {
        return;
    }
    bool peer_requesting = !g_era_split_restart_agreement.peer_request_suppressed &&
                           !g_era_split_restart_agreement.peer_armed &&
                           era_split_restart_agreement_act_valid(g_era_split_restart_agreement.peer_act);
    bool peer_clean_prepared = era_split_restart_agreement_peer_clean_prepared();
    bool own_ready = g_era_split_restart_agreement.request_live;

    uint8_t target_act   = ERA_SPLIT_RESTART_ACT_NONE;
    uint8_t target_param = 0;
    bool    take_own     = g_era_split_restart_agreement.relation_local_left ? own_ready : (!peer_requesting && own_ready);
    if (peer_clean_prepared) {
        /* A prepared peer may be the surviving half of a lost COMMIT or a role
           rotation. It is already unable to rejoin storage, so roll forward
           outranks a fresh user request and restores a bilateral endpoint. */
        target_act   = ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN;
        target_param = ERA_SPLIT_RESTART_CLEAN_PARAM_REQUEST;
    } else if (take_own) {
        target_act   = g_era_split_restart_agreement.request_act;
        target_param = g_era_split_restart_agreement.request_param;
    } else if (peer_requesting) {
        target_act   = g_era_split_restart_agreement.peer_act;
        target_param = g_era_split_restart_agreement.peer_param;
    }
    if (target_act == ERA_SPLIT_RESTART_ACT_NONE) {
        return;
    }
    if (target_act == ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN) {
        /* Selection raises quarantine before either reboot-durable prepare. PREPARE has
           no deadline and may cross immediately, causing the responder to
           raise the same quarantine while both storage tasks drain safely. */
        g_era_split_restart_agreement.clean_selected = true;
        g_era_split_restart_agreement.pending        = false;
        g_era_split_restart_agreement.request_live   = false;
        era_split_restart_agreement_clean_arm_prepare();
        era_split_restart_agreement_clean_trace(ERA_SPLIT_RESTART_CLEAN_TRACE_SELECTED);
        return;
    }
    if (era_split_restart_act_rules[target_act].yields_to_storage && era_split_restart_agreement_storage_busy()) {
        return;
    }
    if (!era_split_restart_arm_ready((era_split_restart_act_t)target_act)) {
        return;
    }

    /* No "is this act already satisfied" test here, and its absence is
       deliberate. The requesting half owns that question -- it is the half that
       holds the current value the act would change -- and it answers it before
       raising anything. A second copy of the test here would have to be
       act-shaped, and the shape it would take for the link switch (refuse a
       param equal to the running level) reads a clean's zero param as a
       satisfied request and refuses it. */
    g_era_split_restart_agreement.arm_act        = target_act;
    g_era_split_restart_agreement.arm_param      = target_param;
    g_era_split_restart_agreement.commit_ms      = sync_timer_read32() + ERA_SPLIT_RESTART_COMMIT_DELAY_MS;
    g_era_split_restart_agreement.arm_live       = true;
    g_era_split_restart_agreement.arm_started_ms = timer_read32();
    g_era_split_restart_agreement.request_live   = false;
    if (!era_split_restart_act_rules[target_act].requires_confirmation) {
        /* Nothing to wait for, so the deadline this arm carries is also this
           half's own. The arm keeps crossing until the commit, which is what
           lets a peer that has not yet answered still adopt the instant. */
        era_split_restart_agreement_arm_commit(target_act, target_param, g_era_split_restart_agreement.commit_ms, false);
    }
}

#ifdef ERA_SPLIT_RESTART_AGREEMENT_TEST
void era_split_restart_agreement_test_reset(void) {
    memset(&g_era_split_restart_agreement, 0, sizeof(g_era_split_restart_agreement));
}
#endif
