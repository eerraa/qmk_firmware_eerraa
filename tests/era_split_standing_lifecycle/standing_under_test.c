// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
// Run the production Core1 standing service. Only its hardware/transaction and
// section-decoder boundary is substituted; serialization uses the real codec.
#define SPLIT_KEYBOARD
#define ERA_HOST_PEER_STORAGE_V1_ENABLE
#define _Static_assert(...)
static unsigned test_core;
#define get_core_num() test_core
#define __DMB() __sync_synchronize()
#define __SEV() ((void)0)
#include "keyboards/era/common/split/communication_core/era_split_communication_core_standing.c"
#include "keyboards/era/common/split/era_split_wire_payload.c"
#undef _Static_assert
#include "standing_under_test.h"

static timer_hw_t test_timer;
timer_hw_t *timer_hw = &test_timer;
static era_host_peer_transaction_result_t test_reply;
static bool test_success;
static uint16_t rotate_owner, rotate_relation;
static uint32_t transaction_count;
static uint8_t last_sections;

void era_test_standing_reset(void) {
    memset(&g_era_split_communication_core_standing_plan, 0, sizeof(g_era_split_communication_core_standing_plan));
    memset(&g_era_split_communication_core_standing_state, 0, sizeof(g_era_split_communication_core_standing_state));
    memset(&g_era_split_communication_core_standing_private, 0, sizeof(g_era_split_communication_core_standing_private));
    g_era_split_communication_core_standing_plan_seq = 0;
    g_era_split_communication_core_standing_state_seq = 0;
    g_era_split_communication_core_standing_state_change_seq = 0;
    memset(&test_reply, 0, sizeof(test_reply));
    test_core = 0;
    test_timer.timerawl = 0;
    test_success = true;
    rotate_owner = rotate_relation = 0;
    transaction_count = last_sections = 0;
}

void era_test_standing_publish(uint16_t owner, uint16_t relation, bool pending, bool enabled) {
    test_core = 0;
    era_split_communication_core_standing_plan_t plan = {0};
    plan.owner_epoch = owner;
    plan.relation_generation = relation;
    plan.poll_period_ms = 10;
    plan.liveness_period_ms = 50;
    plan.enabled = enabled;
    plan.eligible_push_sections = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_STORAGE_PENDING;
    plan.eligible_rsp_sections = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS |
                                 ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_VISUAL_RESYNC;
    plan.storage_pending = pending;
    (void)era_split_communication_core_publish_standing_plan(&plan);
}

void era_test_standing_reply(bool valid, uint8_t news, bool success) {
    memset(&test_reply, 0, sizeof(test_reply));
    test_reply.host_source_storage_news_valid = valid;
    test_reply.host_source_storage_news = news;
    test_success = success;
}

bool era_test_standing_run(uint16_t owner) {
    test_core = 1;
    test_timer.timerawl += 100000;
    return era_split_communication_core_standing_service_once(owner);
}
void era_test_standing_revoke(void) { test_core = 0; era_split_communication_core_clear_standing(); }
void era_test_standing_rotate_during_transaction(uint16_t owner, uint16_t relation) {
    rotate_owner = owner;
    rotate_relation = relation;
}
uint16_t era_test_standing_relation(void) { return g_era_split_communication_core_standing_state.relation_generation; }
bool era_test_standing_news_valid(void) { return g_era_split_communication_core_standing_state.peer_storage_news_valid; }
uint8_t era_test_standing_news(void) { return g_era_split_communication_core_standing_state.peer_storage_news; }
uint32_t era_test_standing_change_seq(void) { return era_split_communication_core_standing_state_seq(); }
uint32_t era_test_standing_exchanges(void) { return era_split_communication_core_standing_exchange_count(); }
uint32_t era_test_standing_transactions(void) { return transaction_count; }
uint8_t era_test_standing_last_sections(void) { return last_sections; }
bool era_test_standing_stopped(void) { return g_era_split_communication_core_standing_state.stopped; }

bool era_split_transaction_engine_prepare_control(bool ext, uint8_t *control, uint8_t *seq) {
    *control = ext ? ERA_SPLIT_WIRE_CONTROL_EXT : 0;
    *seq = 0;
    return true;
}
void era_split_transaction_backend_arm_core1_idle_wake(uint32_t deadline_us) { (void)deadline_us; }
void era_split_transaction_engine_commit_received_frame(const era_split_wire_frame_t *frame) { (void)frame; }
bool era_host_peer_transaction_extract_sections(const era_split_wire_frame_t *frame, era_host_peer_transaction_result_t *out) {
    (void)frame;
    *out = test_reply;
    return true;
}
bool era_host_peer_transaction_encode_rgb_state_body(const era_host_peer_rgb_state_t *state, uint8_t payload[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES]) {
    (void)state;
    memset(payload, 0, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES);
    return true;
}
era_split_transaction_engine_result_t era_split_transaction_engine_transact_compact_owned(
    era_split_wire_direction_t direction, const uint8_t *payload, uint8_t len, uint8_t seq,
    era_split_wire_payload_kind_t expected, era_split_wire_payload_kind_t alternate,
    uint16_t window, uint16_t owner, era_split_wire_frame_t *response, bool *sent,
    era_split_transaction_failure_t *failure) {
    (void)direction; (void)seq; (void)expected; (void)alternate;
    (void)window; (void)owner; (void)response; (void)sent; (void)failure;
    ++transaction_count;
    last_sections = len > 1 ? payload[2] : 0;
    if (rotate_relation != 0) {
        // Exactly the live-lease scheduler interleaving: Core0 revokes and
        // republishes while the previous Core1 transaction is still in flight.
        uint16_t next_owner = rotate_owner, next_relation = rotate_relation;
        rotate_owner = rotate_relation = 0;
        era_test_standing_revoke();
        era_test_standing_publish(next_owner, next_relation, false, true);
        test_core = 1;
    }
    return test_success ? ERA_SPLIT_TRANSACTION_RESULT_OK : ERA_SPLIT_TRANSACTION_RESULT_MISS;
}

uint16_t era_test_standing_capture_stop(uint16_t owner, uint16_t relation) {
    test_core = 0;
    return era_split_communication_core_standing_stop_generation(owner, relation);
}
bool era_test_standing_resume(uint16_t owner, uint16_t relation, uint16_t stop) {
    test_core = 0;
    return era_split_communication_core_resume_standing(owner, relation, stop);
}
bool era_test_standing_snapshot_consistent(void) {
    era_split_communication_core_standing_state_t state;
    uint32_t seq = UINT32_MAX;
    return era_split_communication_core_read_standing_state(&state, &seq) &&
           seq == era_split_communication_core_standing_state_seq() &&
           state.exchange_count == era_test_standing_exchanges();
}

void era_test_standing_visual_reply(void) {
    era_test_standing_reply(false, 0, true);
    test_reply.host_source_visual_snapshot_valid = true;
}
uint8_t era_test_standing_visual_seq(void) { return g_era_split_communication_core_standing_state.peer_visual_seq; }
