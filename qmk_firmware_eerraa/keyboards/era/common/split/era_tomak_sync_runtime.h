// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "era_tomak_sync_receiver.h"

/* Core0 adapters. The wire actor sees only immutable snapshots/results. */
void era_tomak_sync_runtime_bind(uint16_t local_owner, uint16_t local_relation);
void era_tomak_sync_runtime_snapshot(era_tomak_sync_binding_t *binding, era_tomak_sync_wire_t *proof);
void era_tomak_sync_runtime_receive(const era_tomak_sync_binding_t *captured, const era_tomak_sync_wire_t *request);
bool era_tomak_sync_runtime_launch(bool *target);
void era_tomak_sync_runtime_output(bool succeeded);
bool era_tomak_sync_runtime_protected(void);
/* Accepted edge work includes the final bilateral receipt after output. */
bool era_tomak_sync_runtime_transition_pending(void);
bool era_tomak_sync_runtime_request(bool work, era_tomak_sync_wire_t *request);
void era_tomak_sync_runtime_reply(const era_tomak_sync_wire_t *reply);
bool era_tomak_sync_runtime_pair_ready(void);
bool era_tomak_sync_runtime_controls_output(void);
bool era_tomak_sync_runtime_target(void);
bool era_tomak_sync_runtime_output_pending(void);
/* -1 discards a stale prepared frame, 0 holds it, 1 permits submission. */
int8_t era_tomak_sync_runtime_output_gate(bool matching_frame, bool driver_ready);
void era_tomak_sync_runtime_preempt(void);
void era_tomak_sync_runtime_relation(bool paired);
bool era_tomak_sync_runtime_paired(void);
bool era_tomak_sync_runtime_failed(void);
