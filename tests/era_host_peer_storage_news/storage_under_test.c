// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

// Keep firmware-only feature defines local to this source inclusion. The host
// runner does not need a second VIA stack or an actual RGB driver.
#define VIA_ENABLE
#define SPLIT_KEYBOARD
#define RGB_MATRIX_ENABLE
#define ERA_SRAM_RESIDENT_IMAGE
#define ERA_HOST_PEER_STORAGE_V1_ENABLE
#define EECONFIG_KB_DATA_SIZE 256
#define DYNAMIC_KEYMAP_LAYER_COUNT 4
#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 16384

// A 64-bit host cannot satisfy RP2040 pointer/layout assertions. The supported
// TOMAK build checks those; this test executes the unmodified production logic.
#define _Static_assert(...)
#define __DMB() __sync_synchronize()
#define __SEV() ((void)0)
#include "keyboards/era/common/split/era_host_peer_storage.c"
#include "keyboards/era/common/split/era_split_eeprom_sync.c"
#undef __SEV
#undef __DMB
#undef _Static_assert
#include "storage_under_test.h"

bool era_eeprom_driver_macro_transaction_open(void) { return false; }

void era_test_news_reset(void) {
    memset(&g_era_host_peer_storage_local, 0, sizeof(g_era_host_peer_storage_local));
    memset(&g_era_host_peer_storage_relation, 0, sizeof(g_era_host_peer_storage_relation));
    memset(&g_era_host_peer_storage_runtime, 0, sizeof(g_era_host_peer_storage_runtime));
    memset(&era_split_eeprom_sync_state, 0, sizeof(era_split_eeprom_sync_state));
    g_era_host_peer_storage_local.initialized = 1;
    g_era_host_peer_storage_relation.indicator_bits = ERA_HOST_PEER_STORAGE_INDICATOR_GATE;
    g_era_host_peer_storage_relation.runtime_service_active = 1;
    g_era_host_peer_storage_relation.deferred_summary_domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    g_era_host_peer_storage_runtime.role = ERA_HOST_PEER_STORAGE_ROLE_PEER;
    g_era_host_peer_storage_runtime.domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
}

void era_test_news_receive(uint8_t news) { era_host_peer_storage_note_host_news(news); }
void era_test_news_begin_audit(void) { era_host_peer_storage_begin_relation_audit(timer_read32()); }
void era_test_news_rotate_relation(void) { era_host_peer_storage_note_relation_rotation(); }

void era_test_news_seed_pair_obligations(void) {
    g_era_host_peer_storage_local.settled_news_value = 9;
    era_host_peer_storage_note_peer_pending(true);
    era_host_peer_storage_note_local_pending_sent(true);
}

uint8_t era_test_news_local_value(void) { return era_host_peer_storage_settled_news_value(); }

void era_test_news_finish_unchanged_audit(void) {
    // Model the completed, all-MATCH audit at its external boundary. No dirty
    // domains, transfer, or retry fault were injected. This does not implement
    // a second news consumer or presenter: both remain production functions.
    g_era_host_peer_storage_relation.arbitration_flags = ERA_HOST_PEER_STORAGE_ARB_FLAG_SUMMARY_DONE;
}

bool era_test_news_summary_pending(void) {
    return (g_era_host_peer_storage_relation.arbitration_flags & ERA_HOST_PEER_STORAGE_ARB_FLAG_SUMMARY_PENDING) != 0;
}
bool era_test_news_pending(void) { return era_host_peer_storage_indicator_pending(); }
bool era_test_news_visible(void) { return era_split_eeprom_sync_indicator_visible_advance(); }

// Hardware boundary substitutes; reducers, token selector, and presenter are
// the production functions above, not a second storage implementation.
bool era_split_link_runtime_settled(void) { return true; }
void era_split_transport_scheduler_force_storage_recovery(bool revalidate) { (void)revalidate; }

void era_test_news_start_summary(void) { era_host_peer_storage_begin_summary_episode(); }
void era_test_news_complete_summary(uint8_t mine, uint8_t peer) {
    era_host_peer_storage_complete_summary(timer_read32(), mine, peer, true);
}
void era_test_news_abort_summary(bool terminal) {
    g_era_host_peer_storage_runtime.state = ERA_HOST_PEER_STORAGE_RUNTIME_PEER_ABORT;
    if (terminal) g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TERMINAL_ABORT;
    era_host_peer_storage_reset_peer_episode(timer_read32(), true, false);
}
bool era_test_news_audit_if_due(uint16_t relation, uint16_t policy) {
    const era_host_peer_storage_runtime_context_t context = {
        .now_ms = timer_read32(), .relation_generation = relation, .policy_generation = policy,
    };
    return era_host_peer_storage_begin_relation_audit_if_due(&context);
}
void era_test_news_adopt_transaction_identity(uint16_t relation, uint16_t policy) {
    g_era_host_peer_storage_runtime.relation_generation = relation;
    g_era_host_peer_storage_runtime.policy_generation = policy;
}
void era_test_news_seed_direction(void) {
    g_era_host_peer_storage_relation.push_pending_mask = 1;
    g_era_host_peer_storage_relation.idle_due = 1;
    g_era_host_peer_storage_relation.idle_due_kind = ERA_HOST_PEER_STORAGE_TOKEN_PUSH;
    g_era_host_peer_storage_relation.idle_due_domain = 0;
}
bool era_test_news_idle_token(void) { return g_era_host_peer_storage_relation.idle_due; }
bool era_test_news_select_deferred_summary(void) {
    g_era_host_peer_storage_relation.idle_due = 0;
    g_era_host_peer_storage_relation.deferred_summary_domain = 0;
    g_era_host_peer_storage_relation.idle_due_deadline_ms = timer_read32();
    return era_host_peer_storage_select_due_token(timer_read32()) &&
           g_era_host_peer_storage_relation.idle_due_kind == ERA_HOST_PEER_STORAGE_TOKEN_SUMMARY;
}
uint8_t era_test_news_probe_mask(void) { return g_era_host_peer_storage_relation.probe_pending_mask; }
uint8_t era_test_news_push_mask(void) { return g_era_host_peer_storage_relation.push_pending_mask; }
uint8_t era_test_news_conflict_mask(void) { return g_era_host_peer_storage_relation.conflict_pending_mask; }

void era_test_news_pending_request(void) {
    g_era_host_peer_storage_runtime.flags |= ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_REQUEST_PENDING;
}
bool era_test_news_request_pending(void) {
    return (g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_REQUEST_PENDING) != 0;
}
bool era_test_news_take_result(uint8_t field) {
    era_split_communication_core_storage_initiator_result_t result = {
        .owner_epoch = g_era_host_peer_storage_runtime.owner_epoch,
        .relation_generation = g_era_host_peer_storage_runtime.relation_generation,
        .request_generation = g_era_host_peer_storage_runtime.request_generation,
        .policy_generation = g_era_host_peer_storage_runtime.policy_generation,
        .transaction_generation = g_era_host_peer_storage_runtime.transaction_generation,
        .domain = g_era_host_peer_storage_runtime.domain,
        .schema = ERA_HOST_PEER_STORAGE_SCHEMA_V1,
    };
    switch (field) {
        case 1: ++result.owner_epoch; break;
        case 2: ++result.relation_generation; break;
        case 3: ++result.request_generation; break;
        case 4: ++result.policy_generation; break;
        case 5: ++result.transaction_generation; break;
        case 6: ++result.domain; break;
        case 7: ++result.schema; break;
    }
    return era_host_peer_storage_take_peer_result(&result);
}

// LINK admission protects an admitted audit independently of presentation.
bool era_test_news_restart_wait(void) { return era_host_peer_storage_restart_should_wait(); }
void era_test_news_close_indicator_gate(void) {
    g_era_host_peer_storage_relation.indicator_bits &= (uint8_t)~ERA_HOST_PEER_STORAGE_INDICATOR_GATE;
}
