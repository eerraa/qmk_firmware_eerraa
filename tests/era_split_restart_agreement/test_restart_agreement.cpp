// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>

#include "gtest/gtest.h"

#define _Static_assert static_assert
extern "C" {
#include "hardware/structs/timer.h"
#include "keyboards/era/common/split/communication_core/era_split_communication_core_standing.h"
#include "keyboards/era/common/split/era_split_restart_agreement.h"
#include "keyboards/era/common/split/era_split_wire_payload.h"
#include "timer.h"

void set_time(uint32_t time);
void advance_time(uint32_t ms);
}
#undef _Static_assert

namespace {

bool     g_prepare_ok;
bool     g_arm_ready;
bool     g_quiet;
bool     g_storage_busy;
bool     g_quarantine_ready;
uint32_t g_prepare_count;
uint32_t g_reset_count;
uint8_t  g_last_prepare_act;
uint8_t  g_last_prepare_param;

era_split_transaction_engine_result_t g_standing_result;
uint32_t                              g_standing_transact_count;
uint16_t                              g_standing_relation_generation;
uint8_t                               g_standing_payloads[8][ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN];
uint8_t                               g_standing_payload_lens[8];

struct ArmBody {
    uint8_t  act;
    uint8_t  param;
    uint32_t commit_ms;
};

ArmBody arm_body() {
    ArmBody body{};
    era_split_restart_agreement_arm_section(&body.act, &body.param, &body.commit_ms);
    return body;
}

era_split_wire_authority_section_t clean_authority(uint8_t param, bool armed) {
    era_split_wire_authority_section_t authority{};
    authority.restart_act   = ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN;
    authority.restart_param = param;
    authority.restart_armed = armed;
    return authority;
}

era_split_wire_authority_section_t link_authority(uint8_t param, bool armed) {
    era_split_wire_authority_section_t authority{};
    authority.restart_act   = ERA_SPLIT_RESTART_ACT_LINK_SPEED;
    authority.restart_param = param;
    authority.restart_armed = armed;
    return authority;
}

era_split_wire_authority_section_t local_authority() {
    era_split_wire_authority_section_t authority{};
    era_split_restart_agreement_fill_authority(&authority);
    return authority;
}

void expect_idle_arm() {
    const ArmBody arm = arm_body();
    EXPECT_EQ(arm.act, ERA_SPLIT_RESTART_ACT_NONE);
    EXPECT_EQ(arm.param, 0U);
    EXPECT_EQ(arm.commit_ms, 0U);
}

void select_and_prepare_initiator_clean() {
    ASSERT_TRUE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                                    ERA_SPLIT_RESTART_CLEAN_PARAM_REQUEST));
    era_split_restart_agreement_task();
    const ArmBody prepare = arm_body();
    ASSERT_EQ(prepare.act, ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN);
    ASSERT_EQ(prepare.param, ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED);
    ASSERT_EQ(prepare.commit_ms, 0U);
    era_split_restart_agreement_task();
}

bool authority_tuple_expected(uint8_t act, uint8_t param, bool armed) {
    switch (act) {
        case ERA_SPLIT_RESTART_ACT_NONE:
            return param == 0 && !armed;
        case ERA_SPLIT_RESTART_ACT_LINK_SPEED:
            return param <= 2;
        case ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN:
            return (!armed && param == ERA_SPLIT_RESTART_CLEAN_PARAM_REQUEST) ||
                   (armed && (param == ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED ||
                              param == ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT));
        default:
            return false;
    }
}

bool arm_tuple_expected(uint8_t act, uint8_t param, uint32_t commit_ms) {
    switch (act) {
        case ERA_SPLIT_RESTART_ACT_NONE:
            return param == 0 && commit_ms == 0;
        case ERA_SPLIT_RESTART_ACT_LINK_SPEED:
            return param <= 2 && commit_ms != 0;
        case ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN:
            return (param == ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED && commit_ms == 0) ||
                   (param == ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT && commit_ms != 0);
        default:
            return false;
    }
}

bool classify_authority_tuple(uint8_t act, uint8_t param, bool armed, bool response) {
    uint8_t payload[ERA_SPLIT_WIRE_MAX_PAYLOAD_LEN] = {0};
    payload[0] = ERA_SPLIT_WIRE_CONTROL_EXT | 1U;
    payload[1] = response ? ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP : ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH;
    payload[2] = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY;

    era_split_wire_authority_section_t authority{};
    authority.accepted_no_host = true;
    authority.restart_act      = act;
    authority.restart_param    = param;
    authority.restart_armed    = armed;
    era_split_wire_encode_authority_body(&authority, &payload[3]);

    era_split_wire_payload_kind_t kind = ERA_SPLIT_WIRE_PAYLOAD_INVALID;
    return era_split_wire_classify_payload(payload, 3U + ERA_SPLIT_WIRE_AUTHORITY_BYTES,
                                           ERA_SPLIT_WIRE_FRAME_LANE_COMPACT, &kind) &&
           kind == (response ? ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER_HOST_SOURCE_RSP : ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER);
}

bool classify_arm_tuple(uint8_t act, uint8_t param, uint32_t commit_ms) {
    uint8_t payload[3U + ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ARM_BYTES] = {0};
    payload[0] = ERA_SPLIT_WIRE_CONTROL_EXT | 1U;
    payload[1] = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH;
    payload[2] = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM;
    payload[3] = (uint8_t)((param & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_PARAM_MASK) |
                           ((uint8_t)(act << ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_SHIFT) &
                            ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_MASK));
    era_split_wire_put32(&payload[4], commit_ms);

    era_split_wire_payload_kind_t kind = ERA_SPLIT_WIRE_PAYLOAD_INVALID;
    return era_split_wire_classify_payload(payload, sizeof(payload), ERA_SPLIT_WIRE_FRAME_LANE_COMPACT, &kind) &&
           kind == ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER;
}

era_split_communication_core_standing_plan_t standing_restart_plan(uint8_t act, uint8_t param, uint32_t commit_ms) {
    era_split_communication_core_standing_plan_t plan{};
    plan.owner_epoch            = 7;
    plan.relation_generation    = g_standing_relation_generation;
    plan.poll_period_ms         = 1;
    plan.liveness_period_ms     = 50;
    plan.enabled                = 1;
    plan.eligible_push_sections = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM;
    plan.restart_act            = act;
    plan.restart_param          = param;
    plan.restart_commit_ms      = commit_ms;
    return plan;
}

ArmBody captured_standing_arm(uint32_t exchange) {
    ArmBody arm{};
    if (exchange >= g_standing_transact_count || exchange >= 8 || g_standing_payload_lens[exchange] < 8 ||
        (g_standing_payloads[exchange][2] & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM) == 0) {
        return arm;
    }
    const uint8_t *body = &g_standing_payloads[exchange][3];
    arm.act = (uint8_t)((body[0] & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_MASK) >>
                        ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_SHIFT);
    arm.param     = body[0] & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_PARAM_MASK;
    arm.commit_ms = era_split_wire_get32(&body[1]);
    return arm;
}

class EraSplitRestartAgreement : public ::testing::Test {
   protected:
    void SetUp() override {
        era_split_restart_agreement_test_reset();
        era_split_communication_core_clear_standing();
        set_time(1000U);
        timer_hw->timerawl = 1000000U;
        g_prepare_ok        = true;
        g_arm_ready         = true;
        g_quiet             = true;
        g_storage_busy      = false;
        g_quarantine_ready  = true;
        g_prepare_count     = 0;
        g_reset_count       = 0;
        g_last_prepare_act  = ERA_SPLIT_RESTART_ACT_NONE;
        g_last_prepare_param = 0;
        g_standing_result          = ERA_SPLIT_TRANSACTION_RESULT_OK;
        g_standing_transact_count  = 0;
        std::memset(g_standing_payloads, 0, sizeof(g_standing_payloads));
        std::memset(g_standing_payload_lens, 0, sizeof(g_standing_payload_lens));
        g_standing_relation_generation++;
        if (g_standing_relation_generation == 0) {
            g_standing_relation_generation = 1;
        }
    }
};

} // namespace

extern "C" {

era_restart_test_timer_hw_t  g_era_restart_test_timer_hw{};
era_restart_test_timer_hw_t *timer_hw = &g_era_restart_test_timer_hw;

uint32_t era_restart_test_get_core_num(void) {
    return 0;
}

void era_restart_test_dmb(void) {}

void era_restart_test_sev(void) {}

extern const era_split_restart_act_rules_t era_split_restart_act_rules[ERA_SPLIT_RESTART_ACT_MAX + 1] = {
    {false, false, false, 0},
    {true, true, false, 2},
    {true, true, true, 0},
};

bool era_split_restart_prepare_local(era_split_restart_act_t act, uint8_t param) {
    g_prepare_count++;
    g_last_prepare_act   = static_cast<uint8_t>(act);
    g_last_prepare_param = param;
    return g_prepare_ok;
}

bool era_split_restart_arm_ready(era_split_restart_act_t act) {
    (void)act;
    return g_arm_ready;
}

bool era_via_system_restart_quiet_ok(uint32_t requested_ms) {
    (void)requested_ms;
    return g_quiet;
}

bool era_host_peer_storage_restart_should_wait(void) {
    return g_storage_busy;
}

bool era_host_peer_storage_restart_quarantine_ready(void) {
    return g_quarantine_ready;
}

void __wrap_soft_reset_keyboard(void) {
    g_reset_count++;
}

bool era_split_transaction_engine_prepare_control(bool ext, uint8_t *control, uint8_t *tx_seq) {
    if (control == nullptr || tx_seq == nullptr) {
        return false;
    }
    *tx_seq  = 1;
    *control = (uint8_t)(1U | (ext ? ERA_SPLIT_WIRE_CONTROL_EXT : 0U));
    return true;
}

void era_split_transaction_engine_commit_received_frame(const era_split_wire_frame_t *frame) {
    (void)frame;
}

era_split_transaction_engine_result_t era_split_transaction_engine_transact_compact_owned(
    era_split_wire_direction_t request_direction,
    const uint8_t *payload,
    uint8_t payload_len,
    uint8_t tx_seq,
    era_split_wire_payload_kind_t expected_kind,
    era_split_wire_payload_kind_t alternate_expected_kind,
    uint16_t response_window_ms,
    uint16_t owner_epoch,
    era_split_wire_frame_t *response,
    bool *request_sent,
    era_split_transaction_failure_t *failure) {
    (void)request_direction;
    (void)tx_seq;
    (void)expected_kind;
    (void)alternate_expected_kind;
    (void)response_window_ms;
    (void)owner_epoch;

    const uint32_t exchange = g_standing_transact_count++;
    if (exchange < 8U) {
        g_standing_payload_lens[exchange] = payload_len;
        std::memcpy(g_standing_payloads[exchange], payload, payload_len);
    }
    if (response != nullptr) {
        *response = {};
        response->kind = ERA_SPLIT_WIRE_PAYLOAD_GRANT_ACK;
    }
    if (request_sent != nullptr) {
        *request_sent = true;
    }
    if (failure != nullptr) {
        *failure = g_standing_result == ERA_SPLIT_TRANSACTION_RESULT_OK ?
                       ERA_SPLIT_TRANSACTION_FAILURE_NONE :
                       ERA_SPLIT_TRANSACTION_FAILURE_RESPONSE_TIMEOUT;
    }
    return g_standing_result;
}

void era_split_transaction_backend_arm_core1_idle_wake(uint32_t deadline_us) {
    (void)deadline_us;
}

bool era_host_peer_transaction_encode_rgb_state_body(
    const era_host_peer_rgb_state_t *state,
    uint8_t payload[ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES]) {
    if (state == nullptr || payload == nullptr) {
        return false;
    }
    std::memset(payload, 0, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES);
    return true;
}

bool era_host_peer_transaction_extract_sections(const era_split_wire_frame_t *response,
                                                 era_host_peer_transaction_result_t *result) {
    (void)response;
    if (result == nullptr) {
        return false;
    }
    *result = {};
    return true;
}

} // extern "C"

TEST_F(EraSplitRestartAgreement, CarrierValidatorsAcceptOnlyCanonicalPhases) {
    EXPECT_TRUE(era_split_restart_intent_valid(ERA_SPLIT_RESTART_ACT_NONE, 0));
    EXPECT_FALSE(era_split_restart_intent_valid(ERA_SPLIT_RESTART_ACT_NONE, 1));
    EXPECT_TRUE(era_split_restart_intent_valid(ERA_SPLIT_RESTART_ACT_LINK_SPEED, 2));
    EXPECT_FALSE(era_split_restart_intent_valid(ERA_SPLIT_RESTART_ACT_LINK_SPEED, 3));
    EXPECT_TRUE(era_split_restart_intent_valid(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                               ERA_SPLIT_RESTART_CLEAN_PARAM_REQUEST));
    EXPECT_FALSE(era_split_restart_intent_valid(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                                ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED));
    EXPECT_FALSE(era_split_restart_intent_valid(3, 0));

    EXPECT_TRUE(era_split_restart_authority_valid(ERA_SPLIT_RESTART_ACT_NONE, 0, false));
    EXPECT_FALSE(era_split_restart_authority_valid(ERA_SPLIT_RESTART_ACT_NONE, 0, true));
    EXPECT_TRUE(era_split_restart_authority_valid(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                                  ERA_SPLIT_RESTART_CLEAN_PARAM_REQUEST, false));
    EXPECT_FALSE(era_split_restart_authority_valid(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                                   ERA_SPLIT_RESTART_CLEAN_PARAM_REQUEST, true));
    EXPECT_TRUE(era_split_restart_authority_valid(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                                  ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, true));
    EXPECT_FALSE(era_split_restart_authority_valid(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                                   ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, false));
    EXPECT_TRUE(era_split_restart_authority_valid(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                                  ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT, true));
    EXPECT_FALSE(era_split_restart_authority_valid(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                                   ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT, false));
    EXPECT_FALSE(era_split_restart_authority_valid(3, 0, false));

    EXPECT_TRUE(era_split_restart_arm_valid(ERA_SPLIT_RESTART_ACT_NONE, 0, 0));
    EXPECT_FALSE(era_split_restart_arm_valid(ERA_SPLIT_RESTART_ACT_NONE, 0, 1));
    EXPECT_TRUE(era_split_restart_arm_valid(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                            ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, 0));
    EXPECT_FALSE(era_split_restart_arm_valid(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                             ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, 1));
    EXPECT_TRUE(era_split_restart_arm_valid(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                            ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT, 1));
    EXPECT_FALSE(era_split_restart_arm_valid(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                             ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT, 0));
    EXPECT_FALSE(era_split_restart_arm_valid(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                             ERA_SPLIT_RESTART_CLEAN_PARAM_REQUEST, 0));
    EXPECT_FALSE(era_split_restart_arm_valid(3, 0, 0));
}

TEST_F(EraSplitRestartAgreement, StandaloneCleanResetsOnlyAfterSuccessfulPrepare) {
    era_split_restart_agreement_note_relation(false, false, true);
    ASSERT_TRUE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN, 0));
    era_split_restart_agreement_task();

    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_last_prepare_act, ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN);
    EXPECT_EQ(g_last_prepare_param, ERA_SPLIT_RESTART_CLEAN_PARAM_REQUEST);
    EXPECT_EQ(g_reset_count, 1U);
    EXPECT_EQ(timer_read32(), 1000U);
    EXPECT_FALSE(era_split_restart_agreement_commit_agreed());
    expect_idle_arm();
}

TEST_F(EraSplitRestartAgreement, StandalonePrepareFailureDoesNotReset) {
    g_prepare_ok = false;
    era_split_restart_agreement_note_relation(false, false, true);
    ASSERT_TRUE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN, 0));
    era_split_restart_agreement_task();

    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_reset_count, 0U);
    EXPECT_TRUE(era_split_restart_agreement_storage_quarantined());
}

TEST_F(EraSplitRestartAgreement, InitiatorNeedsBothPreparedVotesAndCommitEchoBeforeDeadline) {
    era_split_restart_agreement_note_relation(true, true, true);
    select_and_prepare_initiator_clean();

    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_TRUE(era_split_restart_agreement_storage_quarantined());
    auto authority = local_authority();
    EXPECT_EQ(authority.restart_act, ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN);
    EXPECT_EQ(authority.restart_param, ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED);
    EXPECT_TRUE(authority.restart_armed);

    era_split_restart_agreement_task();
    ArmBody arm = arm_body();
    EXPECT_EQ(arm.param, ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED);
    EXPECT_EQ(arm.commit_ms, 0U);
    EXPECT_EQ(g_reset_count, 0U);

    auto peer_prepared = clean_authority(ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, true);
    era_split_restart_agreement_note_peer_authority(&peer_prepared);
    era_split_restart_agreement_task();
    arm = arm_body();
    ASSERT_EQ(arm.act, ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN);
    ASSERT_EQ(arm.param, ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT);
    ASSERT_NE(arm.commit_ms, 0U);
    EXPECT_EQ(g_reset_count, 0U);

    auto peer_commit = clean_authority(ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT, true);
    era_split_restart_agreement_note_peer_authority(&peer_commit);

    set_time(arm.commit_ms - 1U);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_reset_count, 0U);

    set_time(arm.commit_ms);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_reset_count, 1U);
}

TEST_F(EraSplitRestartAgreement, ResponderPreparesThenEchoesAdoptedCommit) {
    era_split_restart_agreement_note_relation(true, false, false);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, 0);
    EXPECT_TRUE(era_split_restart_agreement_storage_quarantined());
    EXPECT_EQ(g_prepare_count, 0U);

    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    auto authority = local_authority();
    EXPECT_EQ(authority.restart_act, ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN);
    EXPECT_EQ(authority.restart_param, ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED);
    EXPECT_TRUE(authority.restart_armed);

    const uint32_t commit_ms = 1000U + ERA_SPLIT_RESTART_COMMIT_DELAY_MS;
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT, commit_ms);
    authority = local_authority();
    EXPECT_EQ(authority.restart_param, ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT);
    EXPECT_TRUE(authority.restart_armed);

    set_time(commit_ms - 1U);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_reset_count, 0U);
    set_time(commit_ms);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_reset_count, 1U);
}

TEST_F(EraSplitRestartAgreement, ResponderPrepareFailurePublishesNoVoteOrDeadline) {
    g_prepare_ok = false;
    era_split_restart_agreement_note_relation(true, false, false);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, 0);
    EXPECT_TRUE(era_split_restart_agreement_storage_quarantined());

    era_split_restart_agreement_task();

    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_TRUE(era_split_restart_agreement_storage_quarantined());
    EXPECT_EQ(local_authority().restart_act, ERA_SPLIT_RESTART_ACT_NONE);
    expect_idle_arm();

    const uint32_t stale_commit_ms = 1000U + ERA_SPLIT_RESTART_COMMIT_DELAY_MS;
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT,
                                              stale_commit_ms);
    EXPECT_FALSE(era_split_restart_agreement_commit_agreed());
    expect_idle_arm();

    set_time(stale_commit_ms);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_reset_count, 0U);
}

TEST_F(EraSplitRestartAgreement, PrepareFailureIsStickyQuarantinedAndNeverRetried) {
    g_prepare_ok = false;
    era_split_restart_agreement_note_relation(true, true, true);
    ASSERT_TRUE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN, 0));
    era_split_restart_agreement_task();
    era_split_restart_agreement_task();

    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_TRUE(era_split_restart_agreement_storage_quarantined());
    expect_idle_arm();

    g_prepare_ok = true;
    advance_time(ERA_SPLIT_RESTART_REQUEST_LIFETIME_MS + ERA_SPLIT_RESTART_COMMIT_DELAY_MS);
    for (uint8_t i = 0; i < 4; i++) {
        era_split_restart_agreement_task();
    }
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_reset_count, 0U);
    EXPECT_FALSE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN, 0));
}

TEST_F(EraSplitRestartAgreement, QuarantineRetirementDelaysCheckedPrepare) {
    g_storage_busy     = false;
    g_quarantine_ready = false;
    era_split_restart_agreement_note_relation(true, true, true);
    ASSERT_TRUE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN, 0));
    era_split_restart_agreement_task();

    EXPECT_TRUE(era_split_restart_agreement_storage_quarantined());
    EXPECT_EQ(g_prepare_count, 0U);
    for (uint8_t i = 0; i < 3; i++) {
        advance_time(1U);
        era_split_restart_agreement_task();
    }
    EXPECT_EQ(g_prepare_count, 0U);

    g_quarantine_ready = true;
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(local_authority().restart_param, ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED);
}

TEST_F(EraSplitRestartAgreement, LegacyStorageBusyDoesNotDelayCleanAfterQuarantineRetires) {
    g_storage_busy     = true;
    g_quarantine_ready = true;
    era_split_restart_agreement_note_relation(true, true, true);

    select_and_prepare_initiator_clean();

    EXPECT_TRUE(era_split_restart_agreement_storage_quarantined());
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(local_authority().restart_param, ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED);
    EXPECT_EQ(g_reset_count, 0U);
}

TEST_F(EraSplitRestartAgreement, RelationRotationRollsPreparedStateForward) {
    era_split_restart_agreement_note_relation(true, true, true);
    select_and_prepare_initiator_clean();
    ASSERT_EQ(g_prepare_count, 1U);

    auto peer_prepared = clean_authority(ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, true);
    era_split_restart_agreement_note_peer_authority(&peer_prepared);
    era_split_restart_agreement_note_relation_rotation();
    expect_idle_arm();
    EXPECT_TRUE(era_split_restart_agreement_storage_quarantined());

    era_split_restart_agreement_note_relation(true, true, true);
    era_split_restart_agreement_note_peer_authority(&peer_prepared);
    era_split_restart_agreement_task();

    const ArmBody arm = arm_body();
    EXPECT_EQ(arm.act, ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN);
    EXPECT_EQ(arm.param, ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT);
    EXPECT_NE(arm.commit_ms, 0U);
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_reset_count, 0U);
}

TEST_F(EraSplitRestartAgreement, DuplicatePrepareRunsCheckedPrepareOnlyOnce) {
    era_split_restart_agreement_note_relation(true, false, false);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, 0);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, 0);
    era_split_restart_agreement_task();

    EXPECT_EQ(g_prepare_count, 1U);
    auto authority = local_authority();
    EXPECT_EQ(authority.restart_act, ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN);
    EXPECT_EQ(authority.restart_param, ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED);
    EXPECT_TRUE(authority.restart_armed);

    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, 0);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_reset_count, 0U);
}

TEST_F(EraSplitRestartAgreement, CommitBeforeLocalPreparedIsRejected) {
    g_quarantine_ready = false;
    era_split_restart_agreement_note_relation(true, false, false);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, 0);

    const uint32_t premature_commit_ms = 1000U + ERA_SPLIT_RESTART_COMMIT_DELAY_MS;
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT,
                                              premature_commit_ms);
    expect_idle_arm();
    EXPECT_EQ(local_authority().restart_act, ERA_SPLIT_RESTART_ACT_NONE);

    set_time(premature_commit_ms);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 0U);
    EXPECT_EQ(g_reset_count, 0U);

    g_quarantine_ready = true;
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    const auto authority = local_authority();
    EXPECT_EQ(authority.restart_act, ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN);
    EXPECT_EQ(authority.restart_param, ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED);
    EXPECT_TRUE(authority.restart_armed);
    EXPECT_EQ(g_reset_count, 0U);
}

TEST_F(EraSplitRestartAgreement, LostCommitEchoDisarmsBeforeAFreshPreparedRearms) {
    era_split_restart_agreement_note_relation(true, true, true);
    select_and_prepare_initiator_clean();

    const auto peer_prepared = clean_authority(ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, true);
    era_split_restart_agreement_note_peer_authority(&peer_prepared);
    era_split_restart_agreement_task();
    const ArmBody first_commit = arm_body();
    ASSERT_EQ(first_commit.param, ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT);
    ASSERT_NE(first_commit.commit_ms, 0U);

    advance_time(ERA_SPLIT_RESTART_ARM_TIMEOUT_MS);
    era_split_restart_agreement_task();
    expect_idle_arm();
    EXPECT_EQ(g_reset_count, 0U);

    era_split_restart_agreement_note_peer_authority(&peer_prepared);
    era_split_restart_agreement_task();
    const ArmBody second_commit = arm_body();
    EXPECT_EQ(second_commit.act, ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN);
    EXPECT_EQ(second_commit.param, ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT);
    EXPECT_GT(second_commit.commit_ms, first_commit.commit_ms);
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_reset_count, 0U);
}

TEST_F(EraSplitRestartAgreement, LostCleanCommitEchoAcrossRelationRotationDisarmsBeforeFreshCommit) {
    era_split_restart_agreement_note_relation(true, true, true);
    select_and_prepare_initiator_clean();

    const auto peer_prepared = clean_authority(ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, true);
    era_split_restart_agreement_note_peer_authority(&peer_prepared);
    era_split_restart_agreement_task();
    const ArmBody first_commit = arm_body();
    ASSERT_EQ(first_commit.act, ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN);
    ASSERT_EQ(first_commit.param, ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT);
    ASSERT_NE(first_commit.commit_ms, 0U);

    auto plan = standing_restart_plan(first_commit.act, first_commit.param, first_commit.commit_ms);
    ASSERT_TRUE(era_split_communication_core_publish_standing_plan(&plan));
    ASSERT_TRUE(era_split_communication_core_standing_service_once(7));
    ASSERT_EQ(g_standing_payload_lens[0], 8U);
    EXPECT_EQ(captured_standing_arm(0).commit_ms, first_commit.commit_ms);

    era_split_restart_agreement_note_relation_rotation();
    g_standing_relation_generation++;
    if (g_standing_relation_generation == 0) {
        g_standing_relation_generation = 1;
    }
    era_split_restart_agreement_note_relation(true, true, true);
    era_split_restart_agreement_task();
    expect_idle_arm();

    plan = standing_restart_plan(ERA_SPLIT_RESTART_ACT_NONE, 0, 0);
    ASSERT_TRUE(era_split_communication_core_publish_standing_plan(&plan));
    ASSERT_TRUE(era_split_communication_core_standing_service_once(7));
    ASSERT_EQ(g_standing_payload_lens[1], 8U);
    ASSERT_NE(g_standing_payloads[1][2] & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM, 0U);
    const ArmBody disarm = captured_standing_arm(1);
    EXPECT_EQ(disarm.act, ERA_SPLIT_RESTART_ACT_NONE);
    EXPECT_EQ(disarm.param, 0U);
    EXPECT_EQ(disarm.commit_ms, 0U);

    const auto stale_commit = clean_authority(ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT, true);
    era_split_restart_agreement_note_peer_authority(&stale_commit);
    era_split_restart_agreement_task();
    expect_idle_arm();
    EXPECT_FALSE(era_split_restart_agreement_commit_agreed());
    EXPECT_EQ(g_reset_count, 0U);

    advance_time(10U);
    era_split_restart_agreement_note_peer_authority(&peer_prepared);
    era_split_restart_agreement_task();
    const ArmBody second_commit = arm_body();
    ASSERT_EQ(second_commit.act, ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN);
    ASSERT_EQ(second_commit.param, ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT);
    ASSERT_GT(second_commit.commit_ms, first_commit.commit_ms);
    EXPECT_EQ(g_prepare_count, 1U);

    const auto peer_commit = clean_authority(ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT, true);
    era_split_restart_agreement_note_peer_authority(&peer_commit);
    EXPECT_TRUE(era_split_restart_agreement_commit_agreed());

    set_time(first_commit.commit_ms);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_reset_count, 0U);
    set_time(second_commit.commit_ms);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_reset_count, 1U);
    EXPECT_EQ(g_prepare_count, 1U);
}

TEST_F(EraSplitRestartAgreement, DuplicateCommitEchoIsIdempotent) {
    era_split_restart_agreement_note_relation(true, true, true);
    select_and_prepare_initiator_clean();

    const auto peer_prepared = clean_authority(ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, true);
    era_split_restart_agreement_note_peer_authority(&peer_prepared);
    era_split_restart_agreement_task();
    const ArmBody commit = arm_body();
    ASSERT_EQ(commit.param, ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT);

    const auto peer_commit = clean_authority(ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT, true);
    era_split_restart_agreement_note_peer_authority(&peer_commit);
    era_split_restart_agreement_note_peer_authority(&peer_commit);
    EXPECT_TRUE(era_split_restart_agreement_commit_agreed());
    EXPECT_EQ(local_authority().restart_param, ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT);

    set_time(commit.commit_ms);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_reset_count, 1U);
}

TEST_F(EraSplitRestartAgreement, DuplicateResponderCommitIsIdempotent) {
    era_split_restart_agreement_note_relation(true, false, false);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, 0);
    era_split_restart_agreement_task();
    ASSERT_EQ(g_prepare_count, 1U);

    const uint32_t commit_ms = 1000U + ERA_SPLIT_RESTART_COMMIT_DELAY_MS;
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT, commit_ms);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT, commit_ms);
    EXPECT_TRUE(era_split_restart_agreement_commit_agreed());
    EXPECT_EQ(local_authority().restart_param, ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT);

    set_time(commit_ms);
    era_split_restart_agreement_task();
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_reset_count, 1U);
}

TEST_F(EraSplitRestartAgreement, DelayedNonCleanArmIsRejectedAfterCleanSelection) {
    g_quarantine_ready = false;
    era_split_restart_agreement_note_relation(true, false, false);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, 0);

    const uint32_t delayed_commit_ms = 1000U + ERA_SPLIT_RESTART_COMMIT_DELAY_MS;
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_SPEED, 1,
                                              delayed_commit_ms);
    EXPECT_EQ(local_authority().restart_act, ERA_SPLIT_RESTART_ACT_NONE);

    set_time(delayed_commit_ms);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 0U);
    EXPECT_EQ(g_reset_count, 0U);

    g_quarantine_ready = true;
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_last_prepare_act, ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN);
    EXPECT_EQ(local_authority().restart_param, ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED);
}

TEST_F(EraSplitRestartAgreement, StandaloneCleanPromotesToInitiatorAgreementIfRelationReopens) {
    g_quarantine_ready = false;
    era_split_restart_agreement_note_relation(false, false, true);
    ASSERT_TRUE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN, 0));
    era_split_restart_agreement_task();
    EXPECT_TRUE(era_split_restart_agreement_storage_quarantined());
    EXPECT_EQ(g_prepare_count, 0U);
    EXPECT_EQ(g_reset_count, 0U);

    era_split_restart_agreement_note_relation(true, true, true);
    g_quarantine_ready = true;
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_reset_count, 0U);
    ArmBody arm = arm_body();
    ASSERT_EQ(arm.param, ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED);
    ASSERT_EQ(arm.commit_ms, 0U);

    const auto peer_prepared = clean_authority(ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, true);
    era_split_restart_agreement_note_peer_authority(&peer_prepared);
    era_split_restart_agreement_task();
    arm = arm_body();
    ASSERT_EQ(arm.param, ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT);
    ASSERT_NE(arm.commit_ms, 0U);
    EXPECT_EQ(g_reset_count, 0U);

    const auto peer_commit = clean_authority(ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT, true);
    era_split_restart_agreement_note_peer_authority(&peer_commit);
    set_time(arm.commit_ms);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_reset_count, 1U);
}

TEST_F(EraSplitRestartAgreement, StandaloneCleanPromotesToResponderAgreementIfRelationReopens) {
    g_quarantine_ready = false;
    era_split_restart_agreement_note_relation(false, false, false);
    ASSERT_TRUE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN, 0));
    era_split_restart_agreement_task();
    EXPECT_TRUE(era_split_restart_agreement_storage_quarantined());
    EXPECT_EQ(g_prepare_count, 0U);
    EXPECT_EQ(g_reset_count, 0U);

    era_split_restart_agreement_note_relation(true, false, false);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED, 0);
    g_quarantine_ready = true;
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_reset_count, 0U);
    const auto prepared = local_authority();
    EXPECT_EQ(prepared.restart_param, ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED);
    EXPECT_TRUE(prepared.restart_armed);

    const uint32_t commit_ms = timer_read32() + ERA_SPLIT_RESTART_COMMIT_DELAY_MS;
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,
                                              ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT, commit_ms);
    set_time(commit_ms - 1U);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_reset_count, 0U);
    set_time(commit_ms);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_reset_count, 1U);
}

TEST_F(EraSplitRestartAgreement, CarrierTupleMatrixMatchesProductionCodec) {
    constexpr uint32_t live_deadline = 0x12345678U;

    for (uint8_t act = 0; act <= 3; act++) {
        for (uint8_t param = 0; param <= 3; param++) {
            for (uint8_t armed = 0; armed <= 1; armed++) {
                SCOPED_TRACE(::testing::Message() << "authority act=" << unsigned(act)
                                                  << " param=" << unsigned(param)
                                                  << " armed=" << unsigned(armed));
                const bool expected = authority_tuple_expected(act, param, armed != 0);
                EXPECT_EQ(era_split_restart_authority_valid(act, param, armed != 0), expected);
                EXPECT_EQ(classify_authority_tuple(act, param, armed != 0, false), expected);
                EXPECT_EQ(classify_authority_tuple(act, param, armed != 0, true), expected);
            }

            for (const uint32_t deadline : {0U, live_deadline}) {
                SCOPED_TRACE(::testing::Message() << "arm act=" << unsigned(act)
                                                  << " param=" << unsigned(param)
                                                  << " deadline=" << deadline);
                const bool expected = arm_tuple_expected(act, param, deadline);
                EXPECT_EQ(era_split_restart_arm_valid(act, param, deadline), expected);
                EXPECT_EQ(classify_arm_tuple(act, param, deadline), expected);
            }
        }
    }
}

TEST_F(EraSplitRestartAgreement, WireBodiesMasksEligibilityAndProjectedLengthsStayFixed) {
    EXPECT_EQ(ERA_SPLIT_WIRE_AUTHORITY_BYTES, 7U);
    EXPECT_EQ(ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ARM_BYTES, 5U);
    EXPECT_EQ(ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY, 0x04U);
    EXPECT_EQ(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY, 0x04U);
    EXPECT_EQ(ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM, 0x80U);
    EXPECT_EQ(ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_MASK, 0xFFU);
    EXPECT_EQ(ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_MASK, 0xFFU);

    constexpr uint8_t expected_eligibility[ERA_SPLIT_WIRE_SECTION_ELIGIBILITY_MODES][2] = {
        {0x00, 0x00},
        {0xC5, 0xFC},
        {0xC5, 0xFC},
        {0xFE, 0xF7},
        {0xFE, 0xF7},
    };
    for (uint8_t mode = 0; mode < ERA_SPLIT_WIRE_SECTION_ELIGIBILITY_MODES; mode++) {
        for (uint8_t direction = 0; direction <= ERA_SPLIT_WIRE_SECTION_DIRECTION_RSP; direction++) {
            SCOPED_TRACE(::testing::Message() << "mode=" << unsigned(mode)
                                              << " direction=" << unsigned(direction));
            EXPECT_EQ(g_era_split_wire_section_eligibility[mode][direction],
                      expected_eligibility[mode][direction]);
            EXPECT_EQ(era_split_wire_eligible_sections(mode, direction),
                      expected_eligibility[mode][direction]);
        }
    }
    EXPECT_EQ(era_split_wire_eligible_sections(ERA_SPLIT_WIRE_SECTION_ELIGIBILITY_MODES, 0), 0U);
    EXPECT_EQ(era_split_wire_eligible_sections(0, 2), 0U);

    const uint8_t authority_marker = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_AUTHORITY;
    const uint8_t restart_marker   = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_SECTION_RESTART_ARM;
    EXPECT_EQ(era_split_wire_source_push_projected_len(authority_marker), 10U);
    EXPECT_EQ(era_split_wire_source_push_projected_len(restart_marker), 8U);
    EXPECT_EQ(era_split_wire_source_push_projected_len(authority_marker | restart_marker), 15U);

    uint8_t push_payload[16] = {0};
    push_payload[0] = ERA_SPLIT_WIRE_CONTROL_EXT | 1U;
    push_payload[1] = ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH;
    push_payload[2] = authority_marker | restart_marker;
    auto authority = link_authority(2, true);
    authority.accepted_no_host = true;
    era_split_wire_encode_authority_body(&authority, &push_payload[3]);
    push_payload[10] = (uint8_t)((2U & ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_PARAM_MASK) |
                                 ((ERA_SPLIT_RESTART_ACT_LINK_SPEED <<
                                   ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_SHIFT) &
                                  ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_MASK));
    era_split_wire_put32(&push_payload[11], 0x12345678U);

    era_split_wire_section_layout_t layout{};
    ASSERT_TRUE(era_split_wire_layout_source_push(push_payload, 15, &layout));
    EXPECT_EQ(era_split_wire_section_offset(&layout, authority_marker), 3U);
    EXPECT_EQ(era_split_wire_section_offset(&layout, restart_marker), 10U);
    EXPECT_FALSE(era_split_wire_layout_source_push(push_payload, 14, &layout));
    EXPECT_FALSE(era_split_wire_layout_source_push(push_payload, 16, &layout));

    era_split_wire_payload_kind_t kind = ERA_SPLIT_WIRE_PAYLOAD_INVALID;
    EXPECT_TRUE(era_split_wire_classify_payload(push_payload, 15,
                                                ERA_SPLIT_WIRE_FRAME_LANE_COMPACT, &kind));
    EXPECT_EQ(kind, ERA_SPLIT_WIRE_PAYLOAD_HOST_PEER);

    uint8_t response_payload[11] = {0};
    response_payload[0] = ERA_SPLIT_WIRE_CONTROL_EXT | 1U;
    response_payload[1] = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP;
    response_payload[2] = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY;
    era_split_wire_encode_authority_body(&authority, &response_payload[3]);
    ASSERT_TRUE(era_split_wire_layout_host_source_rsp(response_payload, 10, &layout));
    EXPECT_EQ(era_split_wire_section_offset(
                  &layout, ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY),
              3U);
    EXPECT_FALSE(era_split_wire_layout_host_source_rsp(response_payload, 9, &layout));
    EXPECT_FALSE(era_split_wire_layout_host_source_rsp(response_payload, 11, &layout));
}

TEST_F(EraSplitRestartAgreement, LinkSpeedServicedHandshakeCommitsOnlyAfterMatchingAuthority) {
    era_split_restart_agreement_note_relation(true, true, true);
    ASSERT_TRUE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_SPEED, 2));
    era_split_restart_agreement_task();

    const ArmBody commit = arm_body();
    ASSERT_EQ(commit.act, ERA_SPLIT_RESTART_ACT_LINK_SPEED);
    ASSERT_EQ(commit.param, 2U);
    ASSERT_EQ(commit.commit_ms, 1000U + ERA_SPLIT_RESTART_COMMIT_DELAY_MS);
    EXPECT_EQ(g_prepare_count, 0U);
    EXPECT_FALSE(era_split_restart_agreement_commit_agreed());

    const auto peer_commit = link_authority(2, true);
    era_split_restart_agreement_note_peer_authority(&peer_commit);
    EXPECT_TRUE(era_split_restart_agreement_commit_agreed());
    const auto authority = local_authority();
    EXPECT_EQ(authority.restart_act, ERA_SPLIT_RESTART_ACT_LINK_SPEED);
    EXPECT_EQ(authority.restart_param, 2U);
    EXPECT_TRUE(authority.restart_armed);

    set_time(commit.commit_ms - 1U);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 0U);
    set_time(commit.commit_ms);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_last_prepare_act, ERA_SPLIT_RESTART_ACT_LINK_SPEED);
    EXPECT_EQ(g_last_prepare_param, 2U);
    EXPECT_EQ(g_reset_count, 0U);
}

TEST_F(EraSplitRestartAgreement, LinkSpeedInitiatorTimeoutRetiresUnconfirmedArm) {
    era_split_restart_agreement_note_relation(true, true, true);
    ASSERT_TRUE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_SPEED, 1));
    era_split_restart_agreement_task();
    ASSERT_EQ(arm_body().act, ERA_SPLIT_RESTART_ACT_LINK_SPEED);

    advance_time(ERA_SPLIT_RESTART_ARM_TIMEOUT_MS);
    era_split_restart_agreement_task();
    expect_idle_arm();
    EXPECT_FALSE(era_split_restart_agreement_in_flight());
    EXPECT_FALSE(era_split_restart_agreement_commit_agreed());
    EXPECT_EQ(g_prepare_count, 0U);
    EXPECT_EQ(g_reset_count, 0U);
}

TEST_F(EraSplitRestartAgreement, LinkSpeedResponderDisarmsWhenInitiatorRetiresArm) {
    era_split_restart_agreement_note_relation(true, false, false);
    const uint32_t deadline = 1000U + ERA_SPLIT_RESTART_COMMIT_DELAY_MS;
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_SPEED, 1, deadline);
    EXPECT_TRUE(era_split_restart_agreement_commit_agreed());
    EXPECT_EQ(local_authority().restart_act, ERA_SPLIT_RESTART_ACT_LINK_SPEED);

    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_NONE, 0, 0);
    EXPECT_EQ(local_authority().restart_act, ERA_SPLIT_RESTART_ACT_NONE);
    set_time(deadline);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 0U);
    EXPECT_EQ(g_reset_count, 0U);
}

TEST_F(EraSplitRestartAgreement, ConfirmedLinkSpeedCommitSurvivesRelationRotationAndRoleFlip) {
    era_split_restart_agreement_note_relation(true, false, false);
    const uint32_t deadline = 1000U + ERA_SPLIT_RESTART_COMMIT_DELAY_MS;
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_SPEED, 2, deadline);
    ASSERT_TRUE(era_split_restart_agreement_commit_agreed());
    expect_idle_arm();

    era_split_restart_agreement_note_relation_rotation();
    era_split_restart_agreement_note_relation(true, true, true);
    const ArmBody carried = arm_body();
    EXPECT_EQ(carried.act, ERA_SPLIT_RESTART_ACT_LINK_SPEED);
    EXPECT_EQ(carried.param, 2U);
    EXPECT_EQ(carried.commit_ms, deadline);

    set_time(deadline);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_last_prepare_act, ERA_SPLIT_RESTART_ACT_LINK_SPEED);
    EXPECT_EQ(g_last_prepare_param, 2U);
    EXPECT_EQ(g_reset_count, 0U);
}

TEST_F(EraSplitRestartAgreement, StandaloneLinkSpeedUsesLocalDeadlineWithoutWireArm) {
    era_split_restart_agreement_note_relation(false, false, true);
    ASSERT_TRUE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_SPEED, 1));
    era_split_restart_agreement_task();

    expect_idle_arm();
    const auto authority = local_authority();
    EXPECT_EQ(authority.restart_act, ERA_SPLIT_RESTART_ACT_LINK_SPEED);
    EXPECT_EQ(authority.restart_param, 1U);
    EXPECT_TRUE(authority.restart_armed);
    EXPECT_FALSE(era_split_restart_agreement_commit_agreed());
    EXPECT_EQ(g_prepare_count, 0U);

    const uint32_t deadline = 1000U + ERA_SPLIT_RESTART_COMMIT_DELAY_MS;
    set_time(deadline - 1U);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 0U);
    set_time(deadline);
    era_split_restart_agreement_task();
    EXPECT_EQ(g_prepare_count, 1U);
    EXPECT_EQ(g_last_prepare_act, ERA_SPLIT_RESTART_ACT_LINK_SPEED);
    EXPECT_EQ(g_last_prepare_param, 1U);
    EXPECT_EQ(g_reset_count, 0U);
}

TEST_F(EraSplitRestartAgreement, FailedCommitArmIsRetransmittedAfterStandingRecovery) {
    auto plan = standing_restart_plan(ERA_SPLIT_RESTART_ACT_LINK_SPEED, 2, 5000U);
    ASSERT_TRUE(era_split_communication_core_publish_standing_plan(&plan));
    ASSERT_TRUE(era_split_communication_core_standing_service_once(7));
    ASSERT_EQ(captured_standing_arm(0).commit_ms, 5000U);

    plan.restart_act       = ERA_SPLIT_RESTART_ACT_NONE;
    plan.restart_param     = 0;
    plan.restart_commit_ms = 0;
    ASSERT_TRUE(era_split_communication_core_publish_standing_plan(&plan));
    g_standing_result = ERA_SPLIT_TRANSACTION_RESULT_FAIL;
    ASSERT_TRUE(era_split_communication_core_standing_service_once(7));

    plan.enabled           = 0;
    plan.restart_act       = ERA_SPLIT_RESTART_ACT_LINK_SPEED;
    plan.restart_param     = 2;
    plan.restart_commit_ms = 5000U;
    ASSERT_TRUE(era_split_communication_core_publish_standing_plan(&plan));
    g_standing_result = ERA_SPLIT_TRANSACTION_RESULT_OK;
    ASSERT_TRUE(era_split_communication_core_standing_service_once(7));
    ASSERT_EQ(g_standing_payload_lens[2], 1U);

    plan.enabled = 1;
    ASSERT_TRUE(era_split_communication_core_publish_standing_plan(&plan));
    ASSERT_TRUE(era_split_communication_core_standing_service_once(7));
    ASSERT_EQ(g_standing_transact_count, 4U);
    ASSERT_EQ(g_standing_payload_lens[3], 8U);
    const ArmBody resent = captured_standing_arm(3);
    EXPECT_EQ(resent.act, ERA_SPLIT_RESTART_ACT_LINK_SPEED);
    EXPECT_EQ(resent.param, 2U);
    EXPECT_EQ(resent.commit_ms, 5000U);
}

TEST_F(EraSplitRestartAgreement, FailedLiveArmForcesIdleDisarmAfterStandingRecovery) {
    auto plan = standing_restart_plan(ERA_SPLIT_RESTART_ACT_NONE, 0, 0);
    ASSERT_TRUE(era_split_communication_core_publish_standing_plan(&plan));
    ASSERT_TRUE(era_split_communication_core_standing_service_once(7));
    ASSERT_EQ(captured_standing_arm(0).act, ERA_SPLIT_RESTART_ACT_NONE);

    plan.restart_act       = ERA_SPLIT_RESTART_ACT_LINK_SPEED;
    plan.restart_param     = 1;
    plan.restart_commit_ms = 5000U;
    ASSERT_TRUE(era_split_communication_core_publish_standing_plan(&plan));
    g_standing_result = ERA_SPLIT_TRANSACTION_RESULT_FAIL;
    ASSERT_TRUE(era_split_communication_core_standing_service_once(7));

    plan.enabled           = 0;
    plan.restart_act       = ERA_SPLIT_RESTART_ACT_NONE;
    plan.restart_param     = 0;
    plan.restart_commit_ms = 0;
    ASSERT_TRUE(era_split_communication_core_publish_standing_plan(&plan));
    g_standing_result = ERA_SPLIT_TRANSACTION_RESULT_OK;
    ASSERT_TRUE(era_split_communication_core_standing_service_once(7));
    ASSERT_EQ(g_standing_payload_lens[2], 1U);

    plan.enabled = 1;
    ASSERT_TRUE(era_split_communication_core_publish_standing_plan(&plan));
    ASSERT_TRUE(era_split_communication_core_standing_service_once(7));
    ASSERT_EQ(g_standing_transact_count, 4U);
    ASSERT_EQ(g_standing_payload_lens[3], 8U);
    const ArmBody disarm = captured_standing_arm(3);
    EXPECT_EQ(disarm.act, ERA_SPLIT_RESTART_ACT_NONE);
    EXPECT_EQ(disarm.param, 0U);
    EXPECT_EQ(disarm.commit_ms, 0U);
}
