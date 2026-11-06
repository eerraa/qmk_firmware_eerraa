// SPDX-License-Identifier: GPL-2.0-or-later
// Keep the existing storage fixture's platform substitutes, but use this
// target's real EEPROM adapter, NVM, VIA dispatcher and keymap geometry.
#define ERA_TEST_REAL_EEPROM
#define ERA_TOMAK_SYNC_STATE_DIAGNOSTICS
#define ERA_STORAGE_QUIET_DEFER_MS 500
#define RGB_MATRIX_LED_COUNT 1
#define era_host_peer_storage_execution_pending era_test_storage_execution_pending
#define era_host_peer_storage_note_eeprom_commit era_test_storage_note_eeprom_commit
#define era_host_peer_storage_note_eeprom_mask era_test_storage_note_eeprom_mask
#define era_host_peer_storage_note_macro_commit_ready era_test_storage_note_macro_commit_ready
#include "../era_host_peer_storage_news/storage_under_test.c"

static rgb_config_t test_deferred_config;
void eeconfig_read_rgb_matrix(rgb_config_t *config) {
    eeprom_read_block(config, (const void *)(uintptr_t)ERA_STORAGE_EECONFIG_RGB_MATRIX_ADDR, sizeof(*config));
}
void eeconfig_update_rgb_matrix(const rgb_config_t *config) {
    eeprom_update_block(config, (void *)(uintptr_t)ERA_STORAGE_EECONFIG_RGB_MATRIX_ADDR, sizeof(*config));
}
EECONFIG_QUIET_DEBOUNCE_HELPER(rgb_matrix, test_deferred_config);
void era_test_storage_deferred_approve(bool first) {
    if (first) {
        deferred_rgb_matrix = dirty_rgb_matrix = 0;
        deferred_operation_rgb_matrix = 0;
        memset(&test_deferred_config, 0, sizeof(test_deferred_config));
    }
    eeconfig_schedule_deferred_flush_rgb_matrix();
}
void era_test_storage_deferred_flush(void) { eeconfig_run_deferred_flush_rgb_matrix(); }
void era_test_storage_approve_then_fail_immediate(void) {
    EECONFIG_SEMANTIC_BEGIN();
    era_test_storage_deferred_approve(true);
    eeprom_update_byte((void *)(uintptr_t)ERA_STORAGE_EECONFIG_RGB_MATRIX_ADDR, 1);
    EECONFIG_SEMANTIC_END(eeprom_persistence_ok());
}

uint8_t era_test_storage_intent(void) { return g_era_host_peer_storage_local.semantic_intent_mask; }
uint8_t era_test_storage_dirty(void) { return g_era_host_peer_storage_local.dirty_domain_mask; }
uint8_t era_test_storage_failed(void) { return g_era_host_peer_storage_local.failed_domain_mask; }
uint32_t era_test_storage_revision(void) { return g_era_host_peer_storage_local.source_revision_counter; }
unsigned era_test_storage_starts(void) { return attempt_starts; }
unsigned era_test_storage_failures(void) { return failure_receipts; }

static era_host_peer_storage_runtime_context_t test_context;
static uint16_t failed_transaction;

static void test_runtime(void) {
    test_context.now_ms = timer_read32();
    era_host_peer_storage_runtime_task(&test_context);
}

void era_test_storage_hold_input_recovery(bool pending) {
    test_context.input_recovery_pending = pending;
    test_runtime();
}

void era_test_storage_agree_baseline(void) {
    for (uint8_t domain = 0; domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT; ++domain) {
        assert(era_host_peer_storage_capture_domain(domain));
        era_host_peer_storage_note_domain_converged(domain, g_era_host_peer_storage_manifest[domain].image_crc32);
    }
}

static bool summary_responder;
static era_split_communication_core_storage_initiator_request_t summary_request;
static era_split_communication_core_storage_responder_snapshot_t summary_snapshot;

void era_test_storage_hold_equal_summary(bool responder) {
    summary_responder = responder;
    test_context = (era_host_peer_storage_runtime_context_t){
        .owner_epoch=1, .relation_generation=2, .policy_generation=1,
        .peer_usb_epoch=1, .peer_host_open_generation=1, .peer_host_close_generation=1,
        .mode=responder ? ERA_SPLIT_MODE_HOST_PEER_HOST : ERA_SPLIT_MODE_HOST_PEER_PEER,
        .owner_ready=1, .local_initiator=!responder, .local_policy_requested=1,
        .local_bulk_page_supported=1, .peer_known=1, .peer_host_open=!responder,
        .peer_no_host=responder, .peer_bulk_page_supported=1,
    };
    g_era_host_peer_storage_relation.audit_relation_generation=2;
    g_era_host_peer_storage_relation.audit_policy_generation=1;
    era_host_peer_storage_task(timer_read32());
    test_runtime();
    if (responder) {
        assert(era_split_communication_core_storage_claim_responder_snapshot(&summary_snapshot));
        assert(summary_snapshot.allowed && summary_snapshot.recency_baseline_valid &&
               summary_snapshot.recency_changed_mask == 0);
    } else {
        assert(era_split_communication_core_storage_claim_initiator_request(&summary_request));
        assert(summary_request.operation == ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ);
    }
}

static void test_deliver_summary(uint8_t peer_mask) {
    if (summary_responder) {
        era_split_communication_core_storage_initiator_request_t request = {
            .owner_epoch=1, .relation_generation=2, .request_generation=1,
            .transaction_generation=1, .policy_generation=1, .chunk_id=1, .detail=peer_mask,
            .domain=ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE, .schema=ERA_HOST_PEER_STORAGE_SCHEMA,
            .operation=ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ,
        };
        era_split_communication_core_storage_responder_result_t result;
        assert(era_split_communication_core_storage_plan_responder(&summary_snapshot, &request, NULL, &result));
        assert(era_split_communication_core_storage_reserve_responder_result(summary_snapshot.snapshot_generation));
        result.response_sent=1;
        result.result=ERA_SPLIT_TRANSACTION_RESULT_OK;
        assert(era_split_communication_core_storage_publish_responder_result(&result));
    } else {
        era_split_communication_core_storage_initiator_result_t *result =
            era_split_communication_core_storage_begin_initiator_result(summary_request.request_generation);
        assert(result);
        *result = (era_split_communication_core_storage_initiator_result_t){
            .owner_epoch=summary_request.owner_epoch, .relation_generation=summary_request.relation_generation,
            .request_generation=summary_request.request_generation, .policy_generation=summary_request.policy_generation,
            .transaction_generation=summary_request.transaction_generation, .domain=summary_request.domain,
            .schema=summary_request.schema, .operation=ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_RSP,
            .result=ERA_SPLIT_TRANSACTION_RESULT_OK, .data_length=9,
        };
        result->data[1]=1;
        result->data[0]=peer_mask;
        assert(era_split_communication_core_storage_publish_initiator_result(result));
    }
    test_runtime();
}

void era_test_storage_deliver_equal_summary(void) { test_deliver_summary(0); }

static uint32_t promotion_peer_crc[ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT];
static uint16_t promotion_peer_transaction;
static uint8_t promotion_peer_news;

void era_test_storage_promotion_peer_begin(void) {
    era_test_storage_agree_baseline();
    era_test_storage_hold_equal_summary(true);
    era_test_storage_deliver_equal_summary();
    for (uint8_t domain = 0; domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT; ++domain)
        promotion_peer_crc[domain] = g_era_host_peer_storage_manifest[domain].image_crc32;
    promotion_peer_transaction = 1;
    promotion_peer_news = era_host_peer_storage_settled_news_value();
}

static era_split_communication_core_storage_responder_result_t promotion_peer_exchange(
    const era_split_communication_core_storage_initiator_request_t *request) {
    era_split_communication_core_storage_responder_snapshot_t snapshot;
    era_split_communication_core_storage_responder_result_t result;
    assert(era_split_communication_core_storage_claim_responder_snapshot(&snapshot));
    assert(era_split_communication_core_storage_plan_responder(&snapshot, request, NULL, &result));
    assert(era_split_communication_core_storage_reserve_responder_result(snapshot.snapshot_generation));
    result.response_sent = 1;
    result.result = ERA_SPLIT_TRANSACTION_RESULT_OK;
    assert(era_split_communication_core_storage_publish_responder_result(&result));
    test_runtime();
    return result;
}

bool era_test_storage_promotion_peer_take_news(void) {
    test_runtime();
    uint8_t news = era_host_peer_storage_settled_news_value();
    /* The wire peer requests a fresh summary only for a new nonzero value. */
    if (!news || news == promotion_peer_news) return false;
    promotion_peer_news = news;
    era_split_communication_core_storage_initiator_request_t request = {
        .owner_epoch=1, .relation_generation=2, .policy_generation=1,
        .transaction_generation=++promotion_peer_transaction,
        .domain=ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE, .schema=ERA_HOST_PEER_STORAGE_SCHEMA,
        .operation=ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ, .chunk_id=1,
    };
    (void)promotion_peer_exchange(&request);
    return true;
}

void era_test_storage_promotion_peer_complete_pull(uint8_t domain, void (*interleave)(void)) {
    assert(domain < ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT);
    era_split_communication_core_storage_initiator_request_t request = {
        .owner_epoch=1, .relation_generation=2, .policy_generation=1,
        .transaction_generation=++promotion_peer_transaction,
        .domain=domain, .schema=ERA_HOST_PEER_STORAGE_SCHEMA,
        .operation=ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ,
        .image_size=g_era_host_peer_storage_domains[domain].size,
        .image_crc32=promotion_peer_crc[domain],
    };
    era_split_communication_core_storage_responder_result_t result = promotion_peer_exchange(&request);
    assert(result.status == ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY);
    result = promotion_peer_exchange(&request);
    assert(result.status == ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER);
    if (interleave) interleave();
    request.source_revision = result.source_revision;
    request.image_crc32 = result.image_crc32;
    request.operation = ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ;
    for (uint8_t chunk = 0;; ++chunk) {
        uint8_t length = era_host_peer_storage_chunk_length(request.image_size, chunk);
        if (!length) break;
        request.chunk_id = chunk;
        request.detail = length;
        result = promotion_peer_exchange(&request);
        assert(result.operation == ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_RSP && result.data_length == length);
    }
    request.chunk_id = request.detail = 0;
    request.operation = ERA_SPLIT_EEPROM_SYNC_OP_APPLY_REQ;
    result = promotion_peer_exchange(&request);
    assert(result.status == ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY);
    /* The simulated peer supplies its durable completion at the wire boundary. */
    request.operation = ERA_SPLIT_EEPROM_SYNC_OP_COMPLETE_REQ;
    result = promotion_peer_exchange(&request);
    assert(result.status == ERA_SPLIT_EEPROM_SYNC_STATUS_COMPLETE);
    promotion_peer_crc[domain] = result.image_crc32;
}

bool era_test_storage_retire_and_claim_successor(void) {
    test_runtime();
    era_host_peer_storage_task(timer_read32());
    era_host_peer_storage_task(timer_read32());
    test_runtime();
    era_split_communication_core_storage_initiator_request_t request;
    return era_split_communication_core_storage_claim_initiator_request(&request) &&
           request.operation == ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ;
}

static era_split_communication_core_storage_initiator_request_t superseded_probe;
void era_test_storage_hold_probe(void) {
    era_test_storage_hold_equal_summary(false);
    test_deliver_summary(1U << ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP);
    wait_ms(25);
    era_host_peer_storage_task(timer_read32());
    test_runtime();
    assert(era_split_communication_core_storage_claim_initiator_request(&superseded_probe));
    assert(superseded_probe.operation == ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ);
}

void era_test_storage_deliver_superseded_match(void) {
    era_split_communication_core_storage_initiator_result_t *result =
        era_split_communication_core_storage_begin_initiator_result(superseded_probe.request_generation);
    assert(result);
    *result = (era_split_communication_core_storage_initiator_result_t){
        .owner_epoch=superseded_probe.owner_epoch, .relation_generation=superseded_probe.relation_generation,
        .request_generation=superseded_probe.request_generation, .policy_generation=superseded_probe.policy_generation,
        .transaction_generation=superseded_probe.transaction_generation, .domain=superseded_probe.domain,
        .schema=superseded_probe.schema, .operation=ERA_SPLIT_EEPROM_SYNC_OP_PROOF_RSP,
        .result=ERA_SPLIT_TRANSACTION_RESULT_OK, .status=ERA_SPLIT_EEPROM_SYNC_STATUS_MATCH,
    };
    assert(era_split_communication_core_storage_publish_initiator_result(result));
    test_runtime();
    assert(g_era_host_peer_storage_local.retiring);
    test_runtime();
    test_runtime();
}

void era_test_storage_prepare_apply(bool push) {
    const uint8_t domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_MACRO_OPTIONS;
    uint8_t candidate[ERA_HOST_PEER_STORAGE_DOMAIN_MACRO_OPTIONS_BYTES] = {0x37};
    uint32_t crc = era_split_wire_crc32(candidate, sizeof(candidate));
    test_context = (era_host_peer_storage_runtime_context_t){
        .owner_epoch=1, .relation_generation=2, .policy_generation=1,
        .peer_usb_epoch=1, .peer_host_open_generation=1, .peer_host_close_generation=1,
        .mode=push ? ERA_SPLIT_MODE_DUAL_HOST_RIGHT : ERA_SPLIT_MODE_HOST_PEER_PEER,
        .owner_ready=1, .local_initiator=!push, .local_policy_requested=1,
        .local_bulk_page_supported=1, .peer_known=1, .peer_host_open=1,
        .peer_bulk_page_supported=1,
    };
    assert(era_host_peer_storage_capture_domain(domain));
    g_era_host_peer_storage_relation.audit_relation_generation=2;
    g_era_host_peer_storage_relation.audit_policy_generation=1;
    if (!push) {
        g_era_host_peer_storage_relation.idle_due=1;
        g_era_host_peer_storage_relation.idle_due_kind=ERA_HOST_PEER_STORAGE_TOKEN_PROBE;
        g_era_host_peer_storage_relation.idle_due_domain=domain;
        test_runtime();
        for (unsigned phase=0; phase<3; ++phase) {
            era_split_communication_core_storage_initiator_request_t request;
            assert(era_split_communication_core_storage_claim_initiator_request(&request));
            assert(request.operation == (phase==0 ? ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ :
                                         phase==1 ? ERA_SPLIT_EEPROM_SYNC_OP_CHUNK_REQ :
                                                    ERA_SPLIT_EEPROM_SYNC_OP_APPLY_REQ));
            era_split_communication_core_storage_initiator_result_t *result =
                era_split_communication_core_storage_begin_initiator_result(request.request_generation);
            assert(result != NULL);
            *result=(era_split_communication_core_storage_initiator_result_t){
                .owner_epoch=request.owner_epoch, .relation_generation=request.relation_generation,
                .request_generation=request.request_generation, .policy_generation=request.policy_generation,
                .transaction_generation=request.transaction_generation, .domain=request.domain, .schema=request.schema,
                .result=ERA_SPLIT_TRANSACTION_RESULT_OK, .source_revision=2, .image_crc32=crc,
                .operation=era_split_eeprom_sync_response_operation(request.operation),
                .status=phase==2 ? ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY : ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER,
            };
            if (phase==1) {
                result->data_length=sizeof(candidate);
                memcpy(result->data,candidate,sizeof(candidate));
            }
            assert(era_split_communication_core_storage_publish_initiator_result(result));
            if (phase!=2) test_runtime();
        }
        assert(g_era_host_peer_storage_runtime.staged_bytes==sizeof(candidate));
    } else {
        test_runtime();
        for (unsigned phase=0; phase<4; ++phase) {
            era_split_communication_core_storage_responder_snapshot_t snapshot;
            assert(era_split_communication_core_storage_claim_responder_snapshot(&snapshot));
            assert(snapshot.allowed);
            if (phase!=0) {
                assert(snapshot.pinned && snapshot.transaction_generation==1 && snapshot.domain==domain);
                assert(snapshot.source_revision==2 && snapshot.valid);
                assert(snapshot.expected_operation==(phase==2 ? ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CHUNK_REQ :
                                                               ERA_SPLIT_EEPROM_SYNC_OP_PUSH_CTL_REQ));
            }
            // Core1's shared ABI stores pointers in 32 bits. Supply its
            // transport completion and chunk copy here on the 64-bit runner;
            // all Core0 pin/progress/Apply decisions remain production-owned.
            era_split_communication_core_storage_responder_result_t result={
                .owner_epoch=snapshot.owner_epoch, .relation_generation=snapshot.relation_generation,
                .snapshot_generation=snapshot.snapshot_generation, .policy_generation=snapshot.policy_generation,
                .request_fingerprint=phase+1,
                .transaction_generation=1, .domain=domain, .schema=ERA_HOST_PEER_STORAGE_SCHEMA,
                .source_revision=2, .image_crc32=crc, .image_size=sizeof(candidate),
                .request_source_revision=2, .request_image_crc32=crc,
                .request_policy_generation=1, .request_image_size=sizeof(candidate),
                .operation=ERA_SPLIT_EEPROM_SYNC_OP_PUSH_RSP,
                .status=phase==0 ? ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY :
                        phase==3 ? ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY : ERA_SPLIT_EEPROM_SYNC_STATUS_TRANSFER,
            };
            if (phase==2) {
                assert(snapshot.expected_chunk_id==0);
                memcpy(g_era_host_peer_storage_image,candidate,sizeof(candidate));
            }
            assert(era_split_communication_core_storage_reserve_responder_result(snapshot.snapshot_generation));
            result.response_sent=1;
            result.result=ERA_SPLIT_TRANSACTION_RESULT_OK;
            assert(era_split_communication_core_storage_publish_responder_result(&result));
            if (phase!=3) test_runtime();
        }
        assert(g_era_host_peer_storage_runtime.state==ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PUSH_APPLY_WAIT);
        assert(era_host_peer_storage_chunk_length(sizeof(candidate),g_era_host_peer_storage_runtime.next_chunk)==0);
    }
    assert(era_host_peer_storage_apply_publication_preflight(domain));
    assert(!g_era_host_peer_storage_local.image_stale);
    assert(!(g_era_host_peer_storage_runtime.flags & ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_TARGET_DIRTY));
    assert(g_era_host_peer_storage_runtime.expected_crc32==crc);
    assert(memcmp(g_era_host_peer_storage_image,candidate,sizeof(candidate))==0);
}

void era_test_storage_deliver_expired_apply(void) {
    // The owner sampled context before the ready result arrived; ADMIT must
    // consult the live clock, not this still-valid context timestamp.
    assert(!timer_expired32(test_context.now_ms,g_era_host_peer_storage_runtime.episode_deadline_ms));
    wait_ms(g_era_host_peer_storage_runtime.episode_deadline_ms-timer_read32()+1U);
    assert(timer_expired32(timer_read32(),g_era_host_peer_storage_runtime.episode_deadline_ms));
    era_host_peer_storage_runtime_task(&test_context);
}

void era_test_storage_readonly_failure(bool rebind) {
    test_context=(era_host_peer_storage_runtime_context_t){
        .owner_epoch=1, .relation_generation=2, .policy_generation=1,
        .peer_usb_epoch=1, .peer_host_open_generation=1, .peer_host_close_generation=1,
        .mode=ERA_SPLIT_MODE_DUAL_HOST_LEFT, .owner_ready=1, .local_initiator=1,
        .local_policy_requested=1, .local_bulk_page_supported=1, .peer_known=1,
        .peer_host_open=1, .peer_bulk_page_supported=1, .local_left=1,
    };
    g_era_host_peer_storage_relation.audit_relation_generation=2;
    g_era_host_peer_storage_relation.audit_policy_generation=1;
    if (rebind) {
        era_host_peer_storage_note_relation_rotation();
        ++test_context.relation_generation;
    }
    // Only the first attempt is seeded. Subsequent admission must come from
    // the production semantic coalescer's inbox, on this same owner instance.
    if (!failure_receipts) era_host_peer_storage_arm_summary_refresh();
    era_host_peer_storage_task(timer_read32());
    era_host_peer_storage_task(timer_read32());
    test_runtime();
    era_host_peer_storage_task(timer_read32());
    test_runtime();
    era_split_communication_core_storage_initiator_request_t request;
    assert(era_split_communication_core_storage_claim_initiator_request(&request));
    assert(request.operation==ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ);
    wait_ms(ERA_HOST_PEER_STORAGE_EPISODE_MS+1U);
    era_split_communication_core_storage_initiator_result_t *result=
        era_split_communication_core_storage_begin_initiator_result(request.request_generation);
    *result=(era_split_communication_core_storage_initiator_result_t){
        .owner_epoch=request.owner_epoch, .relation_generation=request.relation_generation,
        .request_generation=request.request_generation, .policy_generation=request.policy_generation,
        .transaction_generation=request.transaction_generation, .domain=request.domain, .schema=request.schema,
        .result=ERA_SPLIT_TRANSACTION_RESULT_MISS, .failure=ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT,
    };
    assert(era_split_communication_core_storage_publish_initiator_result(result));
    test_runtime();
    test_runtime();
}

void era_test_storage_terminal_attempt(bool dual, bool left) {
    era_test_news_reset();
    const uint8_t domain = ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP;
    assert(era_host_peer_storage_capture_domain(domain));
    era_host_peer_storage_note_domain_converged(domain, g_era_host_peer_storage_manifest[domain].image_crc32);
    test_context = (era_host_peer_storage_runtime_context_t){
        .owner_epoch=1, .relation_generation=2, .policy_generation=1,
        .peer_usb_epoch=1, .peer_host_open_generation=1, .peer_host_close_generation=1,
        .mode=dual ? ERA_SPLIT_MODE_DUAL_HOST_LEFT : ERA_SPLIT_MODE_HOST_PEER_PEER,
        .owner_ready=1, .local_initiator=1, .local_policy_requested=1,
        .local_bulk_page_supported=1, .peer_known=1, .peer_host_open=1,
        .peer_bulk_page_supported=1, .local_left=left,
    };
    g_era_host_peer_storage_relation.audit_relation_generation=2;
    g_era_host_peer_storage_relation.audit_policy_generation=1;
    g_era_host_peer_storage_relation.idle_due=1;
    g_era_host_peer_storage_relation.idle_due_kind=ERA_HOST_PEER_STORAGE_TOKEN_PROBE;
    g_era_host_peer_storage_relation.idle_due_domain=domain;
    test_runtime();
    era_split_communication_core_storage_initiator_request_t request;
    assert(era_split_communication_core_storage_claim_initiator_request(&request));
    assert(request.operation == ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ);
    failed_transaction=request.transaction_generation;
    era_split_communication_core_storage_initiator_result_t *result =
        era_split_communication_core_storage_begin_initiator_result(request.request_generation);
    *result=(era_split_communication_core_storage_initiator_result_t){
        .owner_epoch=request.owner_epoch, .relation_generation=request.relation_generation,
        .request_generation=request.request_generation, .policy_generation=request.policy_generation,
        .transaction_generation=request.transaction_generation, .domain=request.domain, .schema=request.schema,
        .operation=ERA_SPLIT_EEPROM_SYNC_OP_PROOF_RSP, .status=ERA_SPLIT_EEPROM_SYNC_STATUS_POLICY_CLOSED,
        .result=ERA_SPLIT_TRANSACTION_RESULT_OK,
    };
    assert(era_split_communication_core_storage_publish_initiator_result(result));
    test_runtime();
    assert(g_era_host_peer_storage_local.retiring);
    retirement_ready=false;
}

uint32_t era_test_storage_attempt_deadline(void) {
    return g_era_host_peer_storage_runtime.episode_deadline_ms;
}

void era_test_storage_retire(void) {
    retirement_ready=true;
    test_runtime();
    assert(!g_era_host_peer_storage_local.retiring);
}

bool era_test_storage_retry_open(void) {
    for (unsigned step=0; step<4; ++step) {
        wait_ms(25);
        era_host_peer_storage_task(timer_read32());
        test_runtime();
        era_split_communication_core_storage_initiator_request_t request;
        if (!era_split_communication_core_storage_claim_initiator_request(&request)) continue;
        if (request.operation == ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ) {
            return request.transaction_generation > failed_transaction;
        }
        assert(request.operation == ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ &&
               request.domain == ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE);
        era_split_communication_core_storage_initiator_result_t *result =
            era_split_communication_core_storage_begin_initiator_result(request.request_generation);
        *result=(era_split_communication_core_storage_initiator_result_t){
            .owner_epoch=request.owner_epoch, .relation_generation=request.relation_generation,
            .request_generation=request.request_generation, .policy_generation=request.policy_generation,
            .transaction_generation=request.transaction_generation, .domain=request.domain, .schema=request.schema,
            .operation=ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_RSP, .result=ERA_SPLIT_TRANSACTION_RESULT_OK,
        };
        result->data[0]=1U << ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP;
        result->data[1]=1;
        assert(era_split_communication_core_storage_publish_initiator_result(result));
        test_runtime();
    }
    return false;
}

bool era_test_storage_responder_terminal(bool dual, bool left, void (*accepted_command)(void)) {
    era_test_news_reset();
    test_context=(era_host_peer_storage_runtime_context_t){
        .owner_epoch=1, .relation_generation=2, .policy_generation=1,
        .mode=dual ? ERA_SPLIT_MODE_DUAL_HOST_RIGHT : ERA_SPLIT_MODE_HOST_PEER_HOST,
        .owner_ready=1, .local_policy_requested=1, .local_bulk_page_supported=1,
        .peer_known=1, .peer_no_host=!dual, .peer_host_open=dual,
        .peer_bulk_page_supported=1, .local_left=left,
    };
    test_runtime();
    era_split_communication_core_storage_responder_snapshot_t snapshot;
    assert(era_split_communication_core_storage_claim_responder_snapshot(&snapshot));
    era_split_communication_core_storage_initiator_request_t request={
        .owner_epoch=1, .relation_generation=2, .request_generation=1, .transaction_generation=1,
        .policy_generation=1, .response_window_ms=ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONSE_WINDOW_MS,
        .domain=ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP, .schema=ERA_HOST_PEER_STORAGE_SCHEMA,
        .image_size=ERA_STORAGE_DYNAMIC_KEYMAP_BYTES, .operation=ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ,
    };
    era_split_communication_core_storage_responder_result_t result;
    assert(era_split_communication_core_storage_plan_responder(&snapshot,&request,NULL,&result));
    assert(result.status == ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY);
    assert(era_split_communication_core_storage_reserve_responder_result(snapshot.snapshot_generation));
    result.response_sent=1; result.result=ERA_SPLIT_TRANSACTION_RESULT_OK;
    assert(era_split_communication_core_storage_publish_responder_result(&result));
    test_runtime();
    assert(g_era_host_peer_storage_runtime.state == ERA_HOST_PEER_STORAGE_RUNTIME_HOST_PINNED);
    uint32_t crc=g_era_host_peer_storage_runtime.expected_crc32;
    wait_ms(ERA_HOST_PEER_STORAGE_EPISODE_MS+1U);
    test_runtime();
    assert(g_era_host_peer_storage_local.retiring);
    test_runtime();
    test_runtime();
    accepted_command();
    wait_ms(1000);
    era_host_peer_storage_task(timer_read32());
    assert(era_host_peer_storage_settled_news_value()!=0);
    assert(era_split_communication_core_storage_claim_responder_snapshot(&snapshot));
    // Policy and conflict truth remain live after terminal failure.
    request.operation=ERA_SPLIT_EEPROM_SYNC_OP_SYNC_STATUS_REQ;
    request.domain=ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE;
    request.image_size=0;
    assert(era_split_communication_core_storage_plan_responder(&snapshot,&request,NULL,&result));
    assert(snapshot.allowed && result.chunk_id==snapshot.recency_changed_mask);
    request.operation=ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ;
    request.domain=ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP;
    request.image_size=ERA_STORAGE_DYNAMIC_KEYMAP_BYTES;
    request.image_crc32=crc;
    assert(era_split_communication_core_storage_plan_responder(&snapshot,&request,NULL,&result));
    assert(result.status==ERA_SPLIT_EEPROM_SYNC_STATUS_STALE);
    ++request.transaction_generation;
    assert(era_split_communication_core_storage_plan_responder(&snapshot,&request,NULL,&result));
    assert(result.status==ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY);
    assert(era_split_communication_core_storage_reserve_responder_result(snapshot.snapshot_generation));
    result.response_sent=1; result.result=ERA_SPLIT_TRANSACTION_RESULT_OK;
    assert(era_split_communication_core_storage_publish_responder_result(&result));
    test_runtime();
    assert(era_split_communication_core_storage_claim_responder_snapshot(&snapshot));
    bool pinned=snapshot.pinned && snapshot.transaction_generation==request.transaction_generation &&
                snapshot.image_crc32==crc && era_host_peer_storage_content_attempt_active();
    era_split_communication_core_storage_release_responder_snapshot(snapshot.snapshot_generation);
    return pinned;
}
