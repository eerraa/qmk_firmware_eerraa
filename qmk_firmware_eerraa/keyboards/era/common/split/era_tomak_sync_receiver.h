// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "era_tomak_sync_transition.h"
#include "era_tomak_sync_wire.h"

/* Core0-owned consumer. Core1 only receives copies of its proof.
 * A binding contains both endpoint channels, not equal numeric epochs. */
typedef struct {
    uint16_t local_owner;
    uint16_t local_relation;
    uint16_t peer_owner;
    uint16_t peer_relation;
    uint16_t peer_usb_epoch;
    uint16_t peer_host_open;
    uint16_t peer_host_close;
} era_tomak_sync_binding_t;

typedef struct {
    era_tomak_sync_binding_t binding;
    era_tomak_sync_transition_t edge;
    era_tomak_sync_wire_t proof;
} era_tomak_sync_receiver_t;

bool era_tomak_sync_binding_equal(const era_tomak_sync_binding_t *a, const era_tomak_sync_binding_t *b);
void era_tomak_sync_receiver_bind(era_tomak_sync_receiver_t *receiver, const era_tomak_sync_binding_t *binding);
bool era_tomak_sync_receiver_accept(era_tomak_sync_receiver_t *receiver, const era_tomak_sync_binding_t *captured,
                                    const era_tomak_sync_wire_t *request, uint32_t shared_now, uint32_t local_now,
                                    uint16_t min_lead_ms, uint16_t max_lead_ms);
void era_tomak_sync_receiver_output(era_tomak_sync_receiver_t *receiver, bool succeeded);

/* The initiator uses the same single edge/proof storage. peer_progress is the
 * matching consumer receipt, never inferred from the exchange succeeding. */
bool era_tomak_sync_coordinator_request(era_tomak_sync_receiver_t *endpoint, uint8_t *peer_progress, bool work,
                                       uint32_t shared_now, uint32_t local_now, uint16_t lead_ms,
                                       era_tomak_sync_wire_t *request);
bool era_tomak_sync_coordinator_reply(era_tomak_sync_receiver_t *endpoint, uint8_t *peer_progress,
                                     const era_tomak_sync_wire_t *reply, uint32_t local_now);
bool era_tomak_sync_coordinator_ready(const era_tomak_sync_receiver_t *endpoint, uint8_t peer_progress);
bool era_tomak_sync_receiver_ready(const era_tomak_sync_receiver_t *endpoint);
