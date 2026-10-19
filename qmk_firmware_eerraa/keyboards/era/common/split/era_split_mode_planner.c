// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_mode_planner.h"

#include <string.h>

static bool era_split_mode_local_host_open(const era_split_mode_planner_input_t *input) {
    return input != NULL &&
           input->local_authority.valid &&
           input->local_authority.usb_state == ERA_AUTH_USB_HOST_OPEN;
}

static bool era_split_mode_local_no_host(const era_split_mode_planner_input_t *input) {
    return input != NULL &&
           input->local_authority.valid &&
           input->local_authority.usb_state == ERA_AUTH_USB_NO_HOST;
}

static bool era_split_mode_peer_host_open(const era_split_mode_planner_input_t *input) {
    return input != NULL &&
           input->peer_session.known &&
           input->peer_session.accepted_host_open &&
           !input->peer_session.accepted_no_host;
}

static bool era_split_mode_peer_no_host(const era_split_mode_planner_input_t *input) {
    return input != NULL &&
           input->peer_session.known &&
           !input->peer_session.accepted_host_open &&
           input->peer_session.accepted_no_host;
}

static era_split_mode_t era_split_mode_planner_next_mode(const era_split_mode_planner_input_t *input) {
    if (input == NULL || input->secondary_stale) {
        return ERA_SPLIT_MODE_LOCAL_NO_LINK;
    }

    bool local_host_open = era_split_mode_local_host_open(input);
    bool local_no_host   = era_split_mode_local_no_host(input);
    bool peer_host_open  = era_split_mode_peer_host_open(input);
    bool peer_no_host    = era_split_mode_peer_no_host(input);

    if (local_host_open && peer_host_open) {
        if (input->local_side == ERA_SPLIT_AUTHORITY_SIDE_LEFT) {
            return ERA_SPLIT_MODE_DUAL_HOST_LEFT;
        }
        if (input->local_side == ERA_SPLIT_AUTHORITY_SIDE_RIGHT) {
            return ERA_SPLIT_MODE_DUAL_HOST_RIGHT;
        }
        return ERA_SPLIT_MODE_LOCAL_NO_LINK;
    }

    if (local_host_open && peer_no_host) {
        return ERA_SPLIT_MODE_HOST_PEER_HOST;
    }

    if (local_no_host && peer_host_open) {
        return ERA_SPLIT_MODE_HOST_PEER_PEER;
    }

    /* **Neither half claiming a host is the absence of the fact that assigns
       the roles, not a fact that unassigns them.** Every rule above names a
       half that owns a USB session and derives the relation from it; with no
       such half there is nothing here to re-decide, so the relation the pair
       already has is the only answer available and it is held.

       Returning LOCAL_NO_LINK instead tore down a HOST-PEER pair whose roles
       were never in doubt, every time the HOST's computer slept: the reducer
       closes host-open a grace window into any sustained suspend
       (era_authority_contract.md), and the teardown took the PEER's
       source-push and the HOST's matrix projection with it -- which is the
       input the HOST's own USB remote wake runs on, so a PEER key could no
       longer wake the machine while a HOST key still could.

       The hold cannot strand a role, because nothing in this state can claim
       one: a PEER with no host does not become the HOST because the HOST's
       host went away. What ends it is a half that actually enumerates, which
       lands on one of the four rules above and re-roles the pair in one plan,
       or a peer that goes stale, which is the first rule. */
    if (local_no_host && peer_no_host) {
        return input->current_mode;
    }

    return ERA_SPLIT_MODE_LOCAL_NO_LINK;
}

/* The wire role is a projection of the relation, and this table is
   era_authority_contract.md's Initiator Authority: the PEER initiates
   HOST-PEER, Left initiates DUAL-HOST, and everything else runs the Left-only
   peer-unknown bootstrap. */
static bool era_split_mode_planner_initiator_for_mode(era_split_mode_t mode, bool local_left) {
    switch (mode) {
        case ERA_SPLIT_MODE_HOST_PEER_PEER:
            return true;
        case ERA_SPLIT_MODE_HOST_PEER_HOST:
            return false;
        default:
            return local_left;
    }
}

/* Decided from the relation this plan just settled, not re-derived from the
   authority pairs that relation is settled from. The two agreed for as long as
   the mode was a pure function of those pairs, and stopped agreeing the moment
   it stopped being one: against a held relation the old both-no-host arm
   answered `local_left`, which hands the wire to a side the held HOST-PEER
   assignment contradicts -- a Left HOST would have taken the initiator role
   from under a PEER that is still pushing to it. Two derivations of one fact
   is the defect class the hold rule above exists to close, so the wire role
   leaves this unit as a field of the same result instead of as a second entry
   point the caller has to keep in step.

   No local-authority guard here, deliberately: whether the wire may run at
   all - authority valid and classified, the core1 launch not capped - is the
   scheduler's wire-availability fact, ANDed at this result's one consumer
   (update_mode's next_local_wire_available). This field answers only who
   initiates when it runs; repeating the availability terms here was a second
   copy of another owner's fact. */
static bool era_split_mode_planner_peer_unknown(const era_split_mode_planner_input_t *input) {
    return input->secondary_stale || !input->peer_session.known;
}

static bool era_split_mode_planner_wire_initiator(const era_split_mode_planner_input_t *input, era_split_mode_t next_mode) {
    bool local_left = input->local_side == ERA_SPLIT_AUTHORITY_SIDE_LEFT;

    if (era_split_mode_planner_peer_unknown(input)) {
        return local_left;
    }

    /* A known peer whose status is neither host-open nor no-host is not a
       relation to project from. This half stays responder, unchanged. */
    if (!era_split_mode_peer_host_open(input) && !era_split_mode_peer_no_host(input)) {
        return false;
    }

    return era_split_mode_planner_initiator_for_mode(next_mode, local_left);
}

void era_split_mode_planner_decide(const era_split_mode_planner_input_t *input, era_split_mode_planner_result_t *result) {
    if (result == NULL) {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->next_mode = ERA_SPLIT_MODE_LOCAL_NO_LINK;
    if (input == NULL) {
        return;
    }

    result->next_mode            = era_split_mode_planner_next_mode(input);
    result->mode_changed         = input->current_mode != result->next_mode;
    result->local_wire_initiator = era_split_mode_planner_wire_initiator(input, result->next_mode);
    result->peer_unknown         = era_split_mode_planner_peer_unknown(input);

    bool relation_invalidated = result->mode_changed ||
                                input->local_authority_changed ||
                                input->peer_generation_changed ||
                                input->secondary_stale;

    result->peer_matrix_flush_required  = relation_invalidated;
    result->peer_session_forget_required = input->secondary_stale;

    /* Slice 11.6: a role revalidation *inside* a relation whose own lane
       carries the AUTHORITY section does not need a `SESSION_STATUS` round
       trip. The section already moved the fact, in both directions, so the
       frame would re-send what the peer is holding -- and moving the fact
       without moving the revalidation is what would have left this lane
       carrying a carrier nothing relied on.

       Two cases keep the frame, and neither is a hedge. A relation that
       *changed* is a different relation, so its lane, its eligibility and its
       roles are all re-decided and the frame is what re-decides them -- which
       is also why the role handover runs the path it was observed on. And
       staleness is what this frame is for: it is the recovery target, and a
       relation that is doubtful is exactly the one whose own lane may not be
       carrying anything.

       The matrix flush is deliberately not narrowed with it. It costs a cache
       invalidation rather than a wire exchange, and a peer whose authority
       just moved is a peer whose matrix this half should stop projecting. */
    bool revalidated_on_relation_lane = !result->mode_changed &&
                                        !input->secondary_stale &&
                                        input->relation_authority_lane_live;
    result->local_status_required        = relation_invalidated && !revalidated_on_relation_lane;
}
