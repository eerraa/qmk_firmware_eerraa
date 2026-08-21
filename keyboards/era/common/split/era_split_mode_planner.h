// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "era_split_authority_reducer.h"

/* Five values, and they are the whole relation set: no link, the two
 * HOST-PEER roles, the two DUAL-HOST roles. A HOST_HOST pair once occupied 5
 * and 6 and was held un-reused so old captures decoded; nothing holds them
 * now, so 5 is the next relation's if there ever is one. */
typedef enum {
    ERA_SPLIT_MODE_LOCAL_NO_LINK = 0,
    ERA_SPLIT_MODE_HOST_PEER_HOST = 1,
    ERA_SPLIT_MODE_HOST_PEER_PEER = 2,
    ERA_SPLIT_MODE_DUAL_HOST_LEFT = 3,
    ERA_SPLIT_MODE_DUAL_HOST_RIGHT = 4,
} era_split_mode_t;

typedef struct {
    bool     known;
    bool     accepted_host_open;
    bool     accepted_no_host;
    bool     matrix_ready;
    bool     bulk_page_supported;
    uint16_t usb_epoch;
    uint16_t host_open_generation;
    uint16_t host_close_generation;
} era_split_mode_peer_session_t;

typedef struct {
    era_authority_snapshot_t      local_authority;
    uint8_t                       local_side;
    era_split_mode_peer_session_t peer_session;
    era_split_mode_t              current_mode;
    bool                          local_authority_changed;
    bool                          peer_generation_changed;
    bool                          secondary_stale;
    /* The current relation carries the AUTHORITY section on its own lane, in
       both directions, and that lane is live. Supplied rather than derived
       here because which sections a relation carries is the eligibility
       table's answer and this unit knows nothing about the wire; what the
       planner owns is the consequence, which is that a role revalidation
       inside such a relation needs no `SESSION_STATUS` round trip. */
    bool                          relation_authority_lane_live;
} era_split_mode_planner_input_t;

typedef struct {
    era_split_mode_t next_mode;
    bool             mode_changed;
    /* The wire role of `next_mode`, decided with it rather than beside it: the
       initiator table is a projection of the relation
       (`era_authority_contract.md`'s Initiator Authority), and while it was a
       second entry point the caller had to keep in step the two could answer
       for different relations. */
    bool             local_wire_initiator;
    /* Whether this plan is the peer-unknown bootstrap -- no confirmed peer, or
       one this half has stopped believing in. It is the state the wire role
       above is decided *from* when it is true (Left initiates, Right responds),
       and it is exposed because one consumer needs the state and not only the
       role: the link lane's listener is the bootstrap responder, and a half that
       is merely a responder beside a known peer -- the held HOST-PEER roles, or
       a peer whose status is neither -- has nothing to listen for. */
    bool             peer_unknown;
    bool             peer_matrix_flush_required;
    bool             peer_session_forget_required;
    bool             local_status_required;
} era_split_mode_planner_result_t;

void era_split_mode_planner_decide(const era_split_mode_planner_input_t *input, era_split_mode_planner_result_t *result);
