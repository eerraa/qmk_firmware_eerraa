// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include "wait.h"

// Keep firmware-only feature defines local to this source inclusion. The host
// runner does not need a second VIA stack or an actual RGB driver.
#ifndef ERA_TEST_REAL_EEPROM
#define VIA_ENABLE
#define EECONFIG_KB_DATA_SIZE 280
#define DYNAMIC_KEYMAP_LAYER_COUNT 6
#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE 16384
#endif
#define SPLIT_KEYBOARD
#define RGB_MATRIX_ENABLE
#define ERA_SRAM_RESIDENT_IMAGE
#ifndef ERA_HOST_PEER_STORAGE_V1_ENABLE
#define ERA_HOST_PEER_STORAGE_V1_ENABLE
#endif
#define ERA_TOMAK_SYNC_ENABLE

// A 64-bit host cannot satisfy RP2040 pointer/layout assertions. The supported
// TOMAK build checks those; this test executes the unmodified production logic.
#define _Static_assert(...)
#define __DMB() __sync_synchronize()
#define __SEV() ((void)0)
#ifndef ERA_TEST_STORAGE_SOURCE
#    define ERA_TEST_STORAGE_SOURCE "keyboards/era/common/split/era_host_peer_storage.c"
#endif
#include ERA_TEST_STORAGE_SOURCE
#include "keyboards/era/common/split/era_split_eeprom_sync.c"
#include "keyboards/era/common/split/communication_core/era_split_communication_core_storage.c"
#include "keyboards/era/common/split/era_split_wire_frame.c"
#undef __SEV
#undef __DMB
#undef _Static_assert
#include "storage_under_test.h"

static bool macro_open;
static uint32_t macro_generation;
static unsigned replace_calls;
static bool retirement_ready = true;
static unsigned attempt_starts, failure_receipts;
void era_tomak_sync_attempt_started_kb(void) { ++attempt_starts; }
void era_tomak_sync_failure_report_kb(void) { ++failure_receipts; }
void era_test_input_start_held(void);
void era_test_input_close_for_transfer(void);
bool era_test_input_recover_release(void);
static timer_hw_t test_timer;
timer_hw_t *timer_hw = &test_timer;
void era_test_news_result_due(bool due) {
    if (due) {
        era_split_communication_core_storage_initiator_request_t request = {
            .owner_epoch = 1, .relation_generation = 2, .request_generation = 1, .transaction_generation = 1,
            .policy_generation = 1, .image_size = ERA_HOST_PEER_STORAGE_DOMAIN_ERA_CONFIG_BYTES,
            .response_window_ms = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONSE_WINDOW_MS,
            .domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG, .schema = ERA_HOST_PEER_STORAGE_SCHEMA,
            .operation = ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ,
        };
        assert(era_split_communication_core_storage_reserve_initiator_result(1));
        assert(era_split_communication_core_storage_publish_initiator_request(&request, 25000));
        assert(era_split_communication_core_storage_claim_initiator_request(&request));
        era_split_communication_core_storage_initiator_result_t *result =
            era_split_communication_core_storage_begin_initiator_result(1);
        assert(result != NULL);
        result->request_generation = 1;
        assert(era_split_communication_core_storage_publish_initiator_result(result));
    } else {
        era_split_communication_core_storage_release_initiator_result(1);
    }
}
void era_test_news_execution(bool active) { g_era_host_peer_storage_relation.active_due = active; }
bool era_test_news_execution_pending(void) { return era_host_peer_storage_execution_pending(); }
#ifndef ERA_TEST_REAL_EEPROM
bool era_eeprom_driver_macro_transaction_open(void) { return macro_open; }
#endif
void era_test_news_macro_open(bool open) {
    macro_open = open;
    if (open) macro_generation = era_host_peer_storage_operation_admit(
        1U << ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_MACRO, true);
    else era_host_peer_storage_operation_end(macro_generation, false);
}
uint32_t era_test_news_retry_delay(void) {
    g_era_host_peer_storage_runtime.retry_count = 0;
    era_host_peer_storage_peer_retry_or_abort(timer_read32(), false);
    return g_era_host_peer_storage_runtime.retry_deadline_ms - timer_read32();
}

void era_test_news_reset(void) {
    era_split_communication_core_storage_capacity_init();
    macro_open = false;
    retirement_ready = true;
    attempt_starts = failure_receipts = 0;
    memset(&g_era_host_peer_storage_local, 0, sizeof(g_era_host_peer_storage_local));
    memset(&g_era_host_peer_storage_relation, 0, sizeof(g_era_host_peer_storage_relation));
    memset(&g_era_host_peer_storage_runtime, 0, sizeof(g_era_host_peer_storage_runtime));
    memset(g_era_host_peer_storage_manifest, 0, sizeof(g_era_host_peer_storage_manifest));
    memset(g_era_host_peer_storage_image, 0, sizeof(g_era_host_peer_storage_image));
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
    era_host_peer_storage_operation_proved(g_era_host_peer_storage_local.operation.generation,
                                           ERA_HOST_PEER_STORAGE_ALL_DOMAINS_MASK);
    era_host_peer_storage_runtime_context_t context = {0};
    era_host_peer_storage_operation_release(&context);
}

bool era_test_news_summary_pending(void) {
    return (g_era_host_peer_storage_relation.arbitration_flags & ERA_HOST_PEER_STORAGE_ARB_FLAG_SUMMARY_PENDING) != 0;
}
bool era_test_news_exhaust_arbitration(bool conflict) {
    era_host_peer_storage_runtime_context_t context = {
        .now_ms = 1, .owner_epoch = 1, .relation_generation = 2, .policy_generation = 1,
        .peer_usb_epoch = 1, .peer_host_open_generation = 1, .peer_host_close_generation = 1,
        .mode = ERA_SPLIT_MODE_HOST_PEER_PEER, .owner_ready = 1, .local_initiator = 1,
        .local_policy_requested = 1, .local_bulk_page_supported = 1, .peer_known = 1,
        .peer_host_open = 1, .peer_bulk_page_supported = 1,
    };
    for (unsigned deadline = 0; deadline < 2; ++deadline) {
        era_test_news_reset();
        g_era_host_peer_storage_relation.audit_relation_generation = 2;
        g_era_host_peer_storage_relation.audit_policy_generation = 1;
        g_era_host_peer_storage_relation.idle_due = 1;
        g_era_host_peer_storage_relation.idle_due_kind =
            conflict ? ERA_HOST_PEER_STORAGE_TOKEN_CONFLICT : ERA_HOST_PEER_STORAGE_TOKEN_SUMMARY;
        g_era_host_peer_storage_relation.idle_due_domain =
            conflict ? ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP : ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
        if (conflict) {
            g_era_host_peer_storage_manifest[ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP] =
                (era_host_peer_storage_manifest_entry_t){
                    .source_revision = 1, .schema = ERA_HOST_PEER_STORAGE_SCHEMA,
                    .image_size = ERA_HOST_PEER_STORAGE_DOMAIN_DYNAMIC_KEYMAP_BYTES};
        }
        context.now_ms = 1;
        era_host_peer_storage_runtime_task(&context);
        for (unsigned i = 0; i < (deadline ? 1U : ERA_HOST_PEER_STORAGE_MAX_FAILURES); ++i) {
            era_split_communication_core_storage_initiator_request_t request;
            if (!era_split_communication_core_storage_claim_initiator_request(&request)) return false;
            era_split_communication_core_storage_initiator_result_t *result =
                era_split_communication_core_storage_begin_initiator_result(request.request_generation);
            *result = (era_split_communication_core_storage_initiator_result_t){
                .owner_epoch = request.owner_epoch, .relation_generation = request.relation_generation,
                .request_generation = request.request_generation, .policy_generation = request.policy_generation,
                .transaction_generation = request.transaction_generation, .domain = request.domain, .schema = request.schema,
                .result = ERA_SPLIT_TRANSACTION_RESULT_MISS, .failure = ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT,
            };
            if (!era_split_communication_core_storage_publish_initiator_result(result)) return false;
            if (deadline) wait_ms(ERA_HOST_PEER_STORAGE_EPISODE_MS + 1U);
            context.now_ms += deadline ? ERA_HOST_PEER_STORAGE_EPISODE_MS + 1U : ERA_HOST_PEER_STORAGE_RETRY_MS;
            era_host_peer_storage_runtime_task(&context);
            if (era_host_peer_storage_route_exclusive()) return false;
            context.now_ms += ERA_HOST_PEER_STORAGE_RETRY_MS;
            if (i + 1U < ERA_HOST_PEER_STORAGE_MAX_FAILURES && !deadline)
                era_host_peer_storage_runtime_task(&context);
        }
        context.now_ms += ERA_HOST_PEER_STORAGE_RETRY_MS;
        era_host_peer_storage_runtime_task(&context);
        if (era_host_peer_storage_initiator_request_pending() || era_host_peer_storage_route_exclusive() ||
            era_split_communication_core_storage_result_due() || era_test_news_summary_pending()) return false;
        /* Production relation revalidation must not recreate unchanged work. */
        era_host_peer_storage_note_relation_rotation();
        ++context.relation_generation;
        context.now_ms += 10000;
        era_host_peer_storage_runtime_task(&context);
        if (era_host_peer_storage_initiator_request_pending()) return false;
        context.relation_generation = 2;
    }
    return true;
}
bool era_test_news_pending(void) { return era_host_peer_storage_indicator_pending(); }
bool era_test_news_visible(void) { return era_split_eeprom_sync_indicator_visible_advance(); }

// Hardware boundary substitutes; reducers, token selector, and presenter are
// the production functions above, not a second storage implementation.
bool era_split_link_runtime_settled(void) { return true; }
void era_split_transport_scheduler_force_storage_recovery(bool revalidate) { (void)revalidate; }
bool era_split_transport_scheduler_rotate_storage_relation(void) {
    if (!retirement_ready) return false;
    era_split_communication_core_storage_capacity_init();
    return true;
}
uint32_t era_split_transport_scheduler_core1_request_queue_window_us(void) { return 25000; }
void era_split_communication_core_wake(void) {}
bool era_split_restart_agreement_timed_window(void) { return false; }
bool era_split_restart_agreement_storage_quarantined(void) { return false; }
#ifndef ERA_TEST_REAL_EEPROM
bool era_eeprom_driver_ready(void) { return true; }
uint32_t era_eeprom_driver_macro_operation_generation(void) { return macro_generation; }
era_nvm_result_t era_eeprom_driver_macro_abort_owned(uint32_t generation) {
    if (generation != macro_generation) return ERA_NVM_RESULT_PROTOCOL;
    macro_open = false;
    return ERA_NVM_RESULT_OK;
}
era_nvm_result_t era_eeprom_driver_replace(uint32_t offset, const void *data, size_t length, era_nvm_origin_t origin) {
    (void)offset; (void)data; (void)length; (void)origin; ++replace_calls; return ERA_NVM_RESULT_IO_ERROR;
}
era_nvm_result_t era_eeprom_driver_write_storage_metadata(uint32_t offset, const void *data, size_t length) {
    (void)offset; (void)data; (void)length; return ERA_NVM_RESULT_OK;
}
#endif
void era_state_sync_note_storage_domain(uint8_t domain) { (void)domain; }
#ifndef ERA_TEST_REAL_EEPROM
uint16_t dynamic_keymap_macro_get_buffer_size(void) { return DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE; }
void dynamic_keymap_macro_get_buffer(uint16_t offset, uint16_t size, uint8_t *data) {
    eeprom_read_block(data, (const void *)(uintptr_t)(ERA_STORAGE_DYNAMIC_MACRO_ADDR + offset), size);
}
#endif
bool era_host_peer_transaction_time_anchor_adopted(void) { return true; }
void era_split_transport_scheduler_mark_maintenance_due(uint8_t flags) { (void)flags; }
uint32_t sync_timer_read32(void) { return timer_read32(); }

void era_test_news_start_summary(void) {
    g_era_host_peer_storage_local.episode_operation_generation = g_era_host_peer_storage_local.operation.generation;
    era_host_peer_storage_begin_summary_episode();
}
void era_test_news_complete_summary(uint8_t mine, uint8_t peer) {
    era_host_peer_storage_complete_summary(timer_read32(), mine, peer, true);
    era_host_peer_storage_runtime_context_t context = {0};
    era_host_peer_storage_operation_release(&context);
}
void era_test_news_abort_summary(bool terminal) {
    era_host_peer_storage_peer_begin_abort(terminal ? ERA_SPLIT_EEPROM_SYNC_STATUS_POLICY_CLOSED :
                                                    ERA_SPLIT_EEPROM_SYNC_STATUS_TIMEOUT, timer_read32());
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
        .schema = ERA_HOST_PEER_STORAGE_SCHEMA,
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

uint8_t era_test_options_write_domain(void) {
    return era_host_peer_storage_eeprom_domain_mask(ERA_STORAGE_MACRO_OPTIONS_ADDR, ERA_STORAGE_MACRO_OPTIONS_BYTES);
}

bool era_test_news_failed_apply_recovers_input(bool blue_ready) {
    era_test_news_reset();
    (void)blue_ready;
    replace_calls = 0;
    era_test_input_start_held();
    era_host_peer_storage_runtime_context_t context = {
        .now_ms = 1, .owner_epoch = 1, .relation_generation = 2, .policy_generation = 1,
        .peer_usb_epoch = 1, .peer_host_open_generation = 1, .peer_host_close_generation = 1,
        .mode = ERA_SPLIT_MODE_HOST_PEER_PEER, .owner_ready = 1, .local_initiator = 1,
        .local_policy_requested = 1, .local_bulk_page_supported = 1, .peer_known = 1,
        .peer_host_open = 1, .peer_bulk_page_supported = 1,
    };
    uint8_t candidate[ERA_HOST_PEER_STORAGE_DOMAIN_MACRO_OPTIONS_BYTES] = {0x37};
    uint32_t crc = era_split_wire_crc32(candidate, sizeof(candidate));
    uint8_t domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_MACRO_OPTIONS;
    g_era_host_peer_storage_manifest[domain] = (era_host_peer_storage_manifest_entry_t){
        .source_revision = 1, .schema = ERA_HOST_PEER_STORAGE_SCHEMA, .image_size = sizeof(candidate)};
    g_era_host_peer_storage_relation.audit_relation_generation = 2;
    g_era_host_peer_storage_relation.audit_policy_generation = 1;
    g_era_host_peer_storage_relation.idle_due = 1;
    g_era_host_peer_storage_relation.idle_due_kind = ERA_HOST_PEER_STORAGE_TOKEN_PROBE;
    g_era_host_peer_storage_relation.idle_due_domain = domain;
    era_host_peer_storage_runtime_task(&context);
    for (unsigned phase = 0; phase < 3; ++phase) {
        era_split_communication_core_storage_initiator_request_t request;
        if (!era_split_communication_core_storage_claim_initiator_request(&request)) return false;
        uint8_t expected = phase == 0 ? ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ :
                           phase == 1 ? ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ : ERA_SPLIT_EEPROM_SYNC_OP_APPLY_REQ;
        if (request.operation != expected) return false;
        era_split_communication_core_storage_initiator_result_t *result =
            era_split_communication_core_storage_begin_initiator_result(request.request_generation);
        *result = (era_split_communication_core_storage_initiator_result_t){
            .owner_epoch = request.owner_epoch, .relation_generation = request.relation_generation,
            .request_generation = request.request_generation, .policy_generation = request.policy_generation,
            .transaction_generation = request.transaction_generation, .domain = request.domain, .schema = request.schema,
            .result = ERA_SPLIT_TRANSACTION_RESULT_OK, .source_revision = 2, .image_crc32 = crc,
            .operation = era_split_eeprom_sync_response_operation(request.operation),
            .status = phase == 2 ? ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY : ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER,
        };
        if (phase == 1) {
            result->data_length = sizeof(candidate);
            memcpy(result->data, candidate, sizeof(candidate));
        }
        if (!era_split_communication_core_storage_publish_initiator_result(result)) return false;
        context.now_ms += 25;
        era_host_peer_storage_runtime_task(&context);
        if (phase == 0) {
            if (!era_host_peer_storage_route_exclusive()) return false;
            era_test_input_close_for_transfer();
        }
    }
    if (replace_calls != 1) return false;
    context.now_ms += 25;
    era_host_peer_storage_runtime_task(&context);
    if (era_host_peer_storage_route_exclusive() || era_host_peer_storage_initiator_request_pending() ||
        era_split_communication_core_storage_result_due() || !era_test_input_recover_release()) return false;
    for (unsigned revalidation = 0; revalidation < 4; ++revalidation) {
        era_host_peer_storage_note_relation_rotation();
        ++context.relation_generation; context.now_ms += 10000;
        era_host_peer_storage_runtime_task(&context);
        if (era_host_peer_storage_initiator_request_pending() || replace_calls != 1) return false;
    }
    return true;
}

bool era_test_news_paused_responder_admits_new_work(bool push) {
    era_test_news_reset();
    replace_calls = 0;
    era_host_peer_storage_baseline_record_t saved_baseline, unknown_baseline = {0};
    eeprom_read_block(&saved_baseline, (const void *)(uintptr_t)ERA_STORAGE_RECENCY_BASELINE_ADDR, sizeof(saved_baseline));
    unknown_baseline.guard = ~era_host_peer_storage_baseline_guard(&unknown_baseline);
    eeprom_update_block(&unknown_baseline, (void *)(uintptr_t)ERA_STORAGE_RECENCY_BASELINE_ADDR, sizeof(unknown_baseline));
    bool passed = false;
    era_split_communication_core_storage_responder_snapshot_t snapshot = {0};
    era_host_peer_storage_runtime_context_t context = {
        .now_ms = 1, .owner_epoch = 1, .relation_generation = 2, .policy_generation = 1,
        .peer_usb_epoch = 1, .peer_host_open_generation = 1, .peer_host_close_generation = 1,
        .mode = ERA_SPLIT_MODE_DUAL_HOST_RIGHT, .owner_ready = 1,
        .local_policy_requested = 1, .local_bulk_page_supported = 1, .peer_known = 1,
        .peer_host_open = 1, .peer_bulk_page_supported = 1,
    };
    g_era_host_peer_storage_runtime.role = ERA_HOST_PEER_STORAGE_ROLE_HOST;
    era_host_peer_storage_runtime_task(&context);
    ++context.now_ms;
    era_host_peer_storage_runtime_task(&context);
    if (!era_split_communication_core_storage_claim_responder_snapshot(&snapshot)) goto cleanup;

    era_split_communication_core_storage_initiator_request_t request = {
        .owner_epoch = 1, .relation_generation = 2, .request_generation = 1, .transaction_generation = 1,
        .policy_generation = 1, .response_window_ms = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONSE_WINDOW_MS,
        .domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE, .schema = ERA_HOST_PEER_STORAGE_SCHEMA,
        .operation = ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ,
    };
    era_split_communication_core_storage_responder_result_t result;
    if (!era_split_communication_core_storage_plan_responder(&snapshot, &request, NULL, &result) ||
        result.chunk_id != ERA_HOST_PEER_STORAGE_ALL_DOMAINS_MASK || result.status != 0) goto cleanup;

    request.transaction_generation = 2;
    request.domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_MACRO_OPTIONS;
    request.image_size = ERA_HOST_PEER_STORAGE_DOMAIN_MACRO_OPTIONS_BYTES;
    request.image_crc32 = 0x12345678U;
    request.source_revision = push ? 2 : 0;
    request.operation = push ? ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ : ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ;
    request.detail = push ? ERA_SPLIT_EEPROM_SYNC_PUSH_PHASE_OPEN : 0;
    if (!era_split_communication_core_storage_plan_responder(&snapshot, &request, NULL, &result) ||
        result.status != ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY ||
        !era_split_communication_core_storage_reserve_responder_result(snapshot.snapshot_generation)) goto cleanup;
    result.response_sent = 1;
    result.result = ERA_SPLIT_TRANSACTION_RESULT_OK;
    if (!era_split_communication_core_storage_publish_responder_result(&result)) goto cleanup;
    context.now_ms += 25;
    era_host_peer_storage_runtime_task(&context);
    if (!era_split_communication_core_storage_claim_responder_snapshot(&snapshot)) goto cleanup;
    passed = snapshot.allowed && snapshot.pinned && snapshot.domain == request.domain &&
             snapshot.transaction_generation == request.transaction_generation &&
             !era_host_peer_storage_route_exclusive() && replace_calls == 0;

cleanup:
    era_split_communication_core_storage_release_responder_snapshot(snapshot.snapshot_generation);
    eeprom_update_block(&saved_baseline, (void *)(uintptr_t)ERA_STORAGE_RECENCY_BASELINE_ADDR, sizeof(saved_baseline));
    return passed;
}
