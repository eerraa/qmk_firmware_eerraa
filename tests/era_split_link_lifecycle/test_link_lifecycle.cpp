// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gtest/gtest.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
extern "C" {
#include "quantum.h"
#include "via.h"
#include "eeprom.h"
#include "link_under_test.h"
#include "keyboards/era/common/storage/era_eeprom_driver.h"
#include "keyboards/era/common/storage/era_nvm_rp2040.h"
#define _Static_assert static_assert
#include "keyboards/era/common/split/era_split_link.h"
#include "keyboards/era/common/split/era_split_restart_agreement.h"
#include "keyboards/era/common/split/era_split_transport_scheduler.h"
#include "keyboards/era/common/split/era_split_via_link.h"
#include "keyboards/era/common/system/era_via_system.h"
#undef _Static_assert
}
extern "C" void set_time(uint32_t);
extern "C" void advance_time(uint32_t);

namespace {
struct FakeNor {
    std::array<uint8_t, ERA_NVM_PHYSICAL_SIZE_BYTES> bytes{};
    uint32_t programs = 0, erases = 0, reads = 0, fail_at = 0, delay_ms = 0;
    uint32_t first_program_ms = 0, fail_read_after_program = 0;
    bool read_fail = false, check_wire = false, observed_unready = false, check_receipt = false;
    FakeNor() { bytes.fill(0xff); }
    static void CheckReceipt(const FakeNor &f) {
        if (!f.check_receipt) return;
        // Probe only in the host fixture: production NVM never calls the renderer.
        EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_PENDING);
        bool on=true;
        EXPECT_FALSE(era_split_link_apply_report_advance(nullptr,&on));
        EXPECT_FALSE(on);
    }
    static bool Init(void *) { return true; }
    static bool Read(void *ctx, uint32_t offset, void *data, size_t length) {
        auto &f = *static_cast<FakeNor *>(ctx); f.reads++;
        CheckReceipt(f);
        if (f.read_fail || (f.fail_read_after_program && f.programs >= f.fail_read_after_program) || length > f.bytes.size() || offset > f.bytes.size() - length) return false;
        std::memcpy(data, f.bytes.data() + offset, length); return true;
    }
    static bool Program(void *ctx, uint32_t offset, const void *data, size_t length) {
        auto &f = *static_cast<FakeNor *>(ctx); f.programs++;
        CheckReceipt(f);
        if (f.first_program_ms == 0) f.first_program_ms = timer_read32();
        if (f.check_wire && (!era_test_link_wire_ready() || !era_test_link_published())) f.observed_unready = true;
        advance_time(f.delay_ms);
        CheckReceipt(f);
        if (f.fail_at && f.programs >= f.fail_at) return false;
        if (!length || length > ERA_NVM_PROGRAM_PAGE_BYTES || offset > f.bytes.size() - length ||
            offset / ERA_NVM_PROGRAM_PAGE_BYTES != (offset + length - 1) / ERA_NVM_PROGRAM_PAGE_BYTES) return false;
        auto *src = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < length; ++i) if ((f.bytes[offset+i] & src[i]) != src[i]) return false;
        for (size_t i = 0; i < length; ++i) f.bytes[offset+i] &= src[i];
        return true;
    }
    static bool Erase(void *ctx, uint32_t offset) {
        auto &f = *static_cast<FakeNor *>(ctx); f.erases++;
        CheckReceipt(f);
        if (offset % ERA_NVM_ERASE_SECTOR_BYTES || offset > f.bytes.size() - ERA_NVM_ERASE_SECTOR_BYTES) return false;
        std::memset(f.bytes.data()+offset, 0xff, ERA_NVM_ERASE_SECTOR_BYTES); return true;
    }
};
std::array<FakeNor,2> flash;
FakeNor &nor() { return flash[era_test_link_side()]; }
void choose(unsigned side, uint32_t ms) { era_test_link_select(side); set_time(ms); }
void seed(uint8_t level) {
    const uint8_t block[4] = {level,0,0,0};
    auto result = era_eeprom_driver_replace(era_test_link_address(), block, sizeof(block), ERA_NVM_ORIGIN_LOCAL_QMK);
    ASSERT_TRUE(result == ERA_NVM_RESULT_OK || result == ERA_NVM_RESULT_NO_CHANGE);
    EXPECT_EQ(era_split_link_pending_level(), level);
    nor().programs = nor().erases = nor().reads = nor().first_program_ms = 0;
}
std::array<uint8_t,4> replay() {
    std::array<uint8_t,4> data{};
    EXPECT_EQ(era_eeprom_driver_replay_read(era_test_link_address(), data.data(), data.size()), ERA_NVM_RESULT_OK);
    return data;
}
void begin_local(uint8_t target) {
    ASSERT_TRUE(era_split_link_set_pending_level(target));
    ASSERT_TRUE(era_split_link_request_apply());
    era_split_restart_agreement_task();
}
void commit_local(uint8_t target) {
    begin_local(target); advance_time(ERA_SPLIT_RESTART_COMMIT_DELAY_MS); era_split_restart_agreement_task();
}
struct Arm { uint8_t act, param; uint32_t deadline; };
Arm arm() { Arm a{}; era_split_restart_agreement_arm_section(&a.act,&a.param,&a.deadline); return a; }
era_split_wire_authority_section_t authority() {
    era_split_wire_authority_section_t a{}; era_split_restart_agreement_fill_authority(&a); return a;
}
void agree_pair(uint8_t target, uint32_t left_ms=1000, uint32_t right_ms=5000) {
    choose(0,left_ms); era_test_link_clock_offset(static_cast<int32_t>(right_ms-left_ms));
    era_test_link_relation(true,true,true,true);
    begin_local(target); const auto offer = arm(); ASSERT_EQ(offer.act,ERA_SPLIT_RESTART_ACT_LINK_SPEED);
    choose(1,right_ms); era_test_link_clock_offset(0); era_test_link_relation(true,false,false,false);
    era_split_restart_agreement_note_peer_arm(offer.act,offer.param,offer.deadline);
    auto answer=authority(); ASSERT_TRUE(answer.restart_armed);
    choose(0,left_ms); era_split_restart_agreement_note_peer_authority(&answer);
}
// Exercise the listener's production search, rather than inventing a local
// search flag. The caller selects Right; its settled role may later reverse.
void search_listener_to(uint8_t level, uint32_t end_ms) {
    era_test_link_meet(false,false,false,false,false,false);
    const uint8_t previous=level==ERA_SPLIT_LINK_LEVEL_HIGH ? ERA_SPLIT_LINK_LEVEL_LOW : level-1;
    era_test_link_set_physical(previous,true);
    set_time(end_ms-ERA_SPLIT_LINK_SCAN_DWELL_MS);
    era_test_link_noise(0,0);
    ASSERT_FALSE(era_test_link_recovery_step());
    era_test_link_noise(0,ERA_SPLIT_LINK_SCAN_NOISE_MIN);
    set_time(end_ms);
    ASSERT_TRUE(era_test_link_recovery_step());
    ASSERT_TRUE(era_split_link_rate_searched());
    ASSERT_EQ(era_test_link_physical(),level);
}

// Isolate presentation on an already settled rate. Both discovery facts are
// real: Right owns its search, Left receives that fact in the answer. Only the
// settled initiator requests the report; it may be either physical half.
void agree_recovery_report(bool left_initiator=true, bool left_winner=true,
                           uint32_t left_ms=1000, uint32_t right_ms=5000, bool confirm=true,
                           uint32_t listener_lead_ms=0) {
    const uint32_t local_ms[2]={left_ms,right_ms};
    for (unsigned side=0;side<2;++side) {
        choose(side,local_ms[side]-(side==1 ? listener_lead_ms : 0));
        const bool initiator=side==0 ? left_initiator : !left_initiator;
        const bool winner=side==0 ? left_winner : !left_winner;
        era_test_link_clock_offset(initiator ? static_cast<int32_t>(local_ms[1-side]-local_ms[side]) : 0);
        if (side==1) {
            search_listener_to(ERA_SPLIT_LINK_LEVEL_LOW,local_ms[side]-listener_lead_ms);
        }
        era_test_link_meet(false,side==0,side==0,winner,false,false);
        ASSERT_TRUE(era_split_link_apply(ERA_SPLIT_LINK_LEVEL_LOW));
        era_test_link_meet(true,initiator,side==0,winner,false,side==0);
        bool on=true;
        EXPECT_FALSE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_FALSE(on);
        nor().programs=nor().erases=0;
    }
    const unsigned initiator=left_initiator ? 0 : 1;
    const unsigned responder=1-initiator;
    choose(initiator,local_ms[initiator]); era_split_link_task(); era_split_restart_agreement_task();
    const auto offer=arm(); ASSERT_EQ(offer.act,ERA_SPLIT_RESTART_ACT_LINK_RECOVERED);
    choose(responder,local_ms[responder]);
    era_split_restart_agreement_note_peer_arm(offer.act,offer.param,offer.deadline);
    const auto echo=authority(); ASSERT_TRUE(echo.restart_armed);
    choose(initiator,local_ms[initiator]);
    if (confirm) era_split_restart_agreement_note_peer_authority(&echo);
}

void expect_report(unsigned side, uint32_t ms, bool active, bool lit) {
    choose(side,ms); bool on=!lit;
    EXPECT_EQ(era_split_link_reconcile_success_report_advance(&on),active);
    EXPECT_EQ(on,lit);
}

class EraSplitLinkLifecycle : public ::testing::Test {
 protected:
    void SetUp() override {
        flash = {}; era_test_link_reset();
        for(unsigned i=0;i<2;++i) { choose(i,1000); era_test_link_boot(); ASSERT_TRUE(era_eeprom_driver_ready()); seed(0); }
        choose(0,1000); era_test_link_relation(false,true,true,true);
    }
};
}
extern "C" bool era_nvm_rp2040_flash_bind(era_nvm_flash_t *f) {
    if (!f) return false;
    *f = {&nor(),FakeNor::Init,FakeNor::Read,FakeNor::Program,FakeNor::Erase}; return true;
}
extern "C" bool process_record_via(uint16_t, keyrecord_t *) { return true; }
extern "C" bool via_eeprom_is_valid(void) { return true; }
extern "C" void via_init(void) {}
extern "C" void eeconfig_init_via(void) {}

TEST_F(EraSplitLinkLifecycle, SelectionIsRamOnlyAndReportsAreLengthChecked) {
    uint8_t command[4]={id_custom_set_value,ERA_VIA_SYSTEM_CHANNEL,ERA_SPLIT_VIA_LINK_LEVEL_VALUE_ID,1};
    for(uint8_t n=0;n<4;++n) EXPECT_FALSE(era_split_via_link_handle_via_command(command,n));
    EXPECT_FALSE(era_split_via_link_handle_via_command(nullptr,32));
    ASSERT_TRUE(era_split_via_link_handle_via_command(command,4));
    EXPECT_EQ(era_split_link_pending_level(),1); EXPECT_EQ(nor().programs,0U);
    EXPECT_EQ(era_split_link_active_level(),2); EXPECT_FALSE(era_split_restart_agreement_in_flight());
    command[3]=3; EXPECT_FALSE(era_split_via_link_handle_via_command(command,4));
    EXPECT_EQ(era_split_link_pending_level(),1);
}
TEST_F(EraSplitLinkLifecycle, StandaloneCommitIsCheckedRuntimeThenDurableWithoutReset) {
    nor().check_wire=true; commit_local(1);
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_SUCCEEDED);
    EXPECT_EQ(era_test_link_physical(),1); EXPECT_EQ(era_test_link_stored(),1);
    EXPECT_EQ(replay(),(std::array<uint8_t,4>{1,0,0,0}));
    EXPECT_FALSE(nor().observed_unready); EXPECT_EQ(era_test_link_resets(),0U);
    EXPECT_FALSE(era_split_restart_agreement_in_flight());
    uint8_t get[4]={id_custom_get_value,ERA_VIA_SYSTEM_CHANNEL,ERA_SPLIT_VIA_LINK_APPLY_VALUE_ID,99};
    ASSERT_TRUE(era_split_via_link_handle_via_command(get,4)); EXPECT_EQ(get[3],0);
    era_test_link_boot(); EXPECT_EQ(era_split_link_active_level(),2); EXPECT_EQ(era_split_link_pending_level(),1);
}
TEST_F(EraSplitLinkLifecycle, QuiesceFailureIsTerminalAndWritesNothing) {
    era_test_link_fault(1); commit_local(1);
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_FAILED);
    EXPECT_EQ(era_test_link_physical(),2); EXPECT_EQ(era_test_link_stored(),0);
    EXPECT_EQ(nor().programs,0U); EXPECT_NE(era_test_link_dirty(),0U); EXPECT_EQ(era_test_link_resets(),0U);
}
TEST_F(EraSplitLinkLifecycle, SerialReadyFailureIsNotSuccessAndOldDurabilitySurvives) {
    era_test_link_fault(2); commit_local(1);
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_FAILED);
    EXPECT_EQ(era_split_link_active_level(),1); EXPECT_FALSE(era_test_link_wire_ready());
    EXPECT_EQ(nor().programs,0U); EXPECT_EQ(era_test_link_stored(),0);
    era_test_link_fault(0); EXPECT_TRUE(era_test_link_recovery_step());
    EXPECT_EQ(era_test_link_physical(),2); EXPECT_TRUE(era_test_link_wire_ready());
    EXPECT_EQ(replay()[0],0);
}
TEST_F(EraSplitLinkLifecycle, SnapshotPublicationFailureDoesNotCommitStorage) {
    for(unsigned failure: {3U,4U}) {
        era_test_link_fault(failure);
        EXPECT_FALSE(era_split_link_apply(1)); EXPECT_EQ(nor().programs,0U);
        EXPECT_NE(era_test_link_dirty(),0U);
        era_test_link_fault(0); ASSERT_TRUE(era_test_link_repair());
        EXPECT_TRUE(era_test_link_published()); EXPECT_EQ(era_test_link_dirty(),0U);
    }
}
TEST_F(EraSplitLinkLifecycle, EqualDividerStillRequiresReadyLeaseAndPublications) {
    era_test_link_set_physical(1,false); era_test_link_fault(2);
    EXPECT_FALSE(era_split_transport_scheduler_apply_link_level(1));
    EXPECT_EQ(nor().programs,0U); EXPECT_NE(era_test_link_dirty(),0U);
    era_test_link_fault(0); EXPECT_TRUE(era_test_link_repair());
    EXPECT_EQ(era_test_link_physical(),1); EXPECT_TRUE(era_test_link_wire_ready());
    EXPECT_TRUE(era_test_link_published());
}
TEST_F(EraSplitLinkLifecycle, DurableProgramFailureKeepsOldCacheAndAllowsSameRuntimeRetry) {
    nor().fail_at=1; commit_local(1);
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_FAILED);
    EXPECT_EQ(era_test_link_physical(),1); EXPECT_EQ(era_test_link_stored(),0); EXPECT_EQ(replay()[0],0);
    const auto writes=nor().programs;
    for(unsigned i=0;i<10;++i) { advance_time(1000); era_split_link_task(); }
    EXPECT_EQ(nor().programs,writes); EXPECT_EQ(era_test_link_stored(),0);
    nor().fail_at=0; commit_local(1);
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_SUCCEEDED);
    EXPECT_EQ(era_test_link_stored(),1); EXPECT_EQ(replay()[0],1);
}
TEST_F(EraSplitLinkLifecycle, NotReadyAtCommitDoesNotForgeDurableSuccess) {
    begin_local(1); era_test_link_driver_ready(false);
    advance_time(120); era_split_restart_agreement_task();
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_FAILED);
    EXPECT_EQ(era_test_link_physical(),1); EXPECT_EQ(era_test_link_stored(),0); EXPECT_EQ(nor().programs,0U);
    era_test_link_driver_ready(true); EXPECT_EQ(replay()[0],0);
}
TEST_F(EraSplitLinkLifecycle, FailedReadDoesNotCacheInventedHighOrBlockRuntimeTransition) {
    era_test_link_boot(); nor().read_fail=true;
    EXPECT_EQ(era_split_link_pending_level(),0); EXPECT_FALSE(era_test_link_stored_cached());
    nor().reads=0; EXPECT_EQ(era_split_link_active_level(),2); EXPECT_EQ(nor().reads,0U);
    nor().read_fail=false; EXPECT_EQ(era_split_link_pending_level(),0); EXPECT_TRUE(era_test_link_stored_cached());
}
TEST_F(EraSplitLinkLifecycle, TimeoutThenPeerCommitHasNoLocalOwnerResidue) {
    era_test_link_relation(true,true,true,true); begin_local(1);
    advance_time(60); era_split_restart_agreement_task(); era_split_link_task();
    EXPECT_FALSE(era_split_restart_agreement_in_flight()); EXPECT_EQ(nor().programs,0U);
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_ABORTED);
    era_split_restart_agreement_note_relation_rotation(); era_test_link_relation(true,false,true,false);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_SPEED,2,timer_read32()+120);
    advance_time(120); era_split_restart_agreement_task();
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_SUCCEEDED);
    EXPECT_EQ(era_test_link_stored(),2); EXPECT_EQ(replay()[0],2); EXPECT_EQ(era_test_link_resets(),0U);
}
TEST_F(EraSplitLinkLifecycle, DropdownEditsDoNotReplaceAcceptedIntent) {
    begin_local(1); ASSERT_TRUE(era_split_link_set_pending_level(0));
    EXPECT_FALSE(era_split_link_request_apply());
    advance_time(120); era_split_restart_agreement_task();
    EXPECT_EQ(era_test_link_physical(),1); EXPECT_EQ(era_test_link_stored(),1);
}
TEST_F(EraSplitLinkLifecycle, AnUnservicedPendingIntentExpiresWithoutStaleOwnership) {
    ASSERT_TRUE(era_split_link_set_pending_level(1)); ASSERT_TRUE(era_split_link_request_apply());
    advance_time(5000); era_split_restart_agreement_task();
    EXPECT_FALSE(era_split_restart_agreement_in_flight());
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_ABORTED);
    era_test_link_quiet(true); era_split_restart_agreement_task(); EXPECT_EQ(nor().programs,0U);
}
TEST_F(EraSplitLinkLifecycle, NewPeerPromotesStandaloneDeadlineInsteadOfApplyingAlone) {
    begin_local(1); advance_time(40); era_test_link_relation(true,false,true,true);
    advance_time(80); era_split_restart_agreement_task();
    EXPECT_EQ(era_test_link_physical(),2); EXPECT_EQ(nor().programs,0U);
    EXPECT_TRUE(era_split_restart_agreement_in_flight()); EXPECT_FALSE(authority().restart_armed);
}
TEST_F(EraSplitLinkLifecycle, DifferentUptimesMeetBeforeUnequalStorageLatency) {
    agree_pair(1);
    nor().delay_ms=250; nor().check_wire=true;
    choose(0,1120); era_split_restart_agreement_task();
    EXPECT_EQ(era_test_link_transition_ms(),1120U); EXPECT_FALSE(nor().observed_unready);
    EXPECT_GT(timer_read32(),1120U); EXPECT_EQ(era_test_link_stored(),1);
    choose(1,5120); nor().delay_ms=1; nor().check_wire=true; era_split_restart_agreement_task();
    EXPECT_EQ(era_test_link_transition_ms(),5120U); EXPECT_FALSE(nor().observed_unready);
    EXPECT_EQ(era_test_link_stored(),1); EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_SUCCEEDED);
}
TEST_F(EraSplitLinkLifecycle, ClockSourceFlipAndDuplicateArmCannotMoveCommit) {
    agree_pair(1);
    choose(0,1050); era_test_link_clock_offset(0); era_split_restart_agreement_note_relation_rotation();
    era_test_link_relation(true,false,true,true);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_SPEED,0,1160);
    choose(1,5050); era_test_link_clock_offset(-4000); era_split_restart_agreement_note_relation_rotation();
    era_test_link_relation(true,true,false,false); auto moved=arm(); EXPECT_EQ(moved.deadline,1120U);
    choose(0,1119); era_split_restart_agreement_task(); EXPECT_EQ(era_test_link_physical(),2);
    choose(0,1120); era_split_restart_agreement_task(); EXPECT_EQ(era_test_link_physical(),1);
    choose(1,5120); era_split_restart_agreement_task(); EXPECT_EQ(era_test_link_physical(),1);
}
TEST_F(EraSplitLinkLifecycle, WinnerLowUsesAgreementRatherThanUnilateralAdoption) {
    const uint8_t low[4]={2,0,0,0};
    ASSERT_EQ(era_eeprom_driver_replace(era_test_link_address(),low,4,ERA_NVM_ORIGIN_LOCAL_QMK),ERA_NVM_RESULT_OK);
    era_test_link_boot(); era_test_link_relation(true,true,true,true); era_split_link_task(); era_split_restart_agreement_task();
    auto offer=arm(); ASSERT_EQ(offer.act,ERA_SPLIT_RESTART_ACT_LINK_SPEED); ASSERT_EQ(offer.param,2);
    choose(1,1000); era_test_link_relation(true,false,false,false);
    era_split_restart_agreement_note_peer_arm(offer.act,offer.param,offer.deadline); auto answer=authority();
    choose(0,1000); era_split_restart_agreement_note_peer_authority(&answer);
    choose(0,1120); era_split_restart_agreement_task(); EXPECT_EQ(era_test_link_stored(),2);
    choose(1,1120); era_split_restart_agreement_task(); EXPECT_EQ(era_test_link_stored(),2);
}
// Both halves commit the agreed transition and let the confirm window settle;
// the NVM programs of that real level change are then discounted.
static void agree_and_settle(uint8_t target) {
    agree_pair(target);
    for (unsigned side : {0U, 1U}) {
        choose(side, side ? 5120 : 1120);
        era_split_restart_agreement_task();
        ASSERT_EQ(era_split_restart_agreement_last_result(), ERA_SPLIT_RESTART_RESULT_SUCCEEDED);
        advance_time(ERA_SPLIT_LINK_UPGRADE_CONFIRM_MS);
        era_split_link_task();
        ASSERT_TRUE(era_split_link_runtime_settled());
        nor().programs = 0;
    }
}
TEST_F(EraSplitLinkLifecycle, SamePairReopenAfterAgreementReopensNoReconciliation) {
    agree_and_settle(1);
    // The storage-close revalidation churn: unserviced, then serviced again as the same pair.
    for (unsigned side : {0U, 1U}) {
        choose(side, side ? 5400 : 1400);
        const bool initiator = side == 0;
        era_test_link_meet(false, initiator, initiator, initiator, false, false);
        era_test_link_meet(true, initiator, initiator, initiator, false, false);
        era_split_link_task();
        era_split_restart_agreement_task();
        EXPECT_FALSE(era_split_restart_agreement_in_flight());
        EXPECT_TRUE(era_split_link_runtime_settled());
        EXPECT_EQ(nor().programs, 0U);
    }
}
TEST_F(EraSplitLinkLifecycle, FreshMeetingAfterAgreementReopensReconciliation) {
    agree_and_settle(1);
    choose(0, 3000);
    era_test_link_relation(false, true, true, true);
    era_test_link_relation(true, true, true, true);
    era_split_link_task();
    EXPECT_TRUE(era_split_restart_agreement_in_flight());
    EXPECT_FALSE(era_split_link_runtime_settled());
    choose(1, 7000);
    era_test_link_relation(false, false, false, false);
    era_test_link_relation(true, false, false, false);
    EXPECT_FALSE(era_split_link_runtime_settled());
    advance_time(ERA_SPLIT_LINK_UPGRADE_WAIT_MS);
    era_split_link_task();
    EXPECT_TRUE(era_split_link_runtime_settled());
}
TEST_F(EraSplitLinkLifecycle, UnagreedWinnerStillReconcilesOnSamePairReopen) {
    era_test_link_relation(true, true, true, true);
    era_split_link_task();
    ASSERT_TRUE(era_split_restart_agreement_in_flight());
    // The churn retires the unconfirmed proposal before any peer answered.
    era_split_restart_agreement_task();
    era_split_restart_agreement_note_relation_rotation();
    advance_time(ERA_SPLIT_RESTART_REQUEST_LIFETIME_MS);
    era_split_restart_agreement_task();
    ASSERT_FALSE(era_split_restart_agreement_in_flight());
    era_test_link_meet(false, true, true, true, false, false);
    era_test_link_meet(true, true, true, true, false, false);
    era_split_link_task();
    EXPECT_TRUE(era_split_restart_agreement_in_flight());
}
TEST_F(EraSplitLinkLifecycle, RecoveryStepOffTheAgreedLevelReopensReconciliation) {
    agree_and_settle(1);
    choose(0, 1400);
    ASSERT_EQ(era_test_link_physical(), 1);
    era_test_link_set_physical(2, true);
    era_test_link_meet(false, true, true, true, false, false);
    era_test_link_meet(true, true, true, true, false, false);
    era_split_link_task();
    EXPECT_TRUE(era_split_restart_agreement_in_flight());
}
TEST_F(EraSplitLinkLifecycle, WinnerChangeReopensReconciliationEvenOnSamePairReopen) {
    agree_and_settle(1);
    choose(0, 1400);
    era_test_link_meet(true, true, true, false, false, false);
    EXPECT_FALSE(era_split_link_runtime_settled());
}
TEST_F(EraSplitLinkLifecycle, NoArmDoesNotGuessWinnerPreferenceOrWriteAfterWait) {
    era_test_link_relation(true,false,false,false); advance_time(500); era_split_link_task();
    EXPECT_EQ(era_test_link_stored(),0); EXPECT_EQ(nor().programs,0U); EXPECT_TRUE(era_split_link_runtime_settled());
}
TEST_F(EraSplitLinkLifecycle, ConfirmLossFallsBackOnceWithoutPersistingLow) {
    agree_pair(1); choose(0,1120); era_split_restart_agreement_task();
    era_test_link_relation(false,true,true,true); advance_time(200); era_split_link_task();
    ASSERT_TRUE(era_test_link_recovery_step()); EXPECT_EQ(era_test_link_physical(),2); EXPECT_EQ(replay()[0],1);
    era_test_link_relation(false,false,false,false); era_test_link_noise(0,0); EXPECT_FALSE(era_test_link_recovery_step());
    advance_time(1500); era_test_link_noise(0,2); ASSERT_TRUE(era_test_link_recovery_step());
    EXPECT_EQ(era_test_link_physical(),0); EXPECT_FALSE(era_test_link_recovery_step());
    EXPECT_EQ(era_test_link_physical(),0); EXPECT_EQ(replay()[0],1);
}
TEST_F(EraSplitLinkLifecycle, ListenerIgnoresSilenceAndSingleBootBreak) {
    era_test_link_relation(false,false,false,false);
    EXPECT_FALSE(era_test_link_recovery_step()); advance_time(1500); EXPECT_FALSE(era_test_link_recovery_step());
    era_test_link_noise(0,1); advance_time(1500); EXPECT_FALSE(era_test_link_recovery_step());
    era_test_link_noise(0,3); advance_time(1500); EXPECT_TRUE(era_test_link_recovery_step());
    EXPECT_EQ(era_test_link_physical(),0); EXPECT_EQ(nor().programs,0U);
}
TEST_F(EraSplitLinkLifecycle, ListenerRateSearchReportsSuccessOnlyAfterRelationOpens) {
    era_test_link_set_physical(ERA_SPLIT_LINK_LEVEL_MEDIUM,true);
    era_test_link_relation(false,false,false,false);
    bool on=true;
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_FALSE(on);
    era_test_link_noise(0,0); EXPECT_FALSE(era_test_link_recovery_step());
    advance_time(1500); era_test_link_noise(0,2); ASSERT_TRUE(era_test_link_recovery_step());
    EXPECT_EQ(era_test_link_physical(),ERA_SPLIT_LINK_LEVEL_LOW);
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_FALSE(on);

    era_test_link_relation(true,false,false,false);
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_FALSE(on);
    // Service alone no longer starts a local pulse. The peer's agreed instant does.
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_RECOVERED,0,timer_read32()+120);
    advance_time(120); era_split_restart_agreement_task();
    ASSERT_TRUE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_TRUE(on);
    advance_time(ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_ON_MS);
    ASSERT_TRUE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_FALSE(on);
    advance_time(ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_OFF_MS);
    ASSERT_TRUE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_TRUE(on);
    advance_time(ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_ON_MS);
    ASSERT_TRUE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_FALSE(on);
    advance_time(ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_TAIL_MS);
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_FALSE(on);
}
TEST_F(EraSplitLinkLifecycle, SameRateRelationDoesNotReportReconciliationSuccess) {
    era_test_link_relation(true,false,false,false);
    bool on=true;
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(&on));
    EXPECT_FALSE(on);
}
TEST_F(EraSplitLinkLifecycle, AbandonedListenerSearchCannotMarkLaterUnrelatedServiceSuccessful) {
    era_test_link_set_physical(ERA_SPLIT_LINK_LEVEL_MEDIUM,true);
    era_test_link_relation(false,false,false,false);
    era_test_link_noise(0,0); EXPECT_FALSE(era_test_link_recovery_step());
    advance_time(1500); era_test_link_noise(0,2); ASSERT_TRUE(era_test_link_recovery_step());
    era_test_link_relation(false,true,true,true); // listener episode ended without service
    era_test_link_relation(true,true,true,true);
    bool on=true;
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(&on));
    EXPECT_FALSE(on);
}
TEST_F(EraSplitLinkLifecycle, FallbackFailureSupersedesPendingReconciliationSuccess) {
    era_test_link_set_physical(ERA_SPLIT_LINK_LEVEL_MEDIUM,true);
    era_test_link_relation(false,false,false,false);
    era_test_link_noise(0,0); EXPECT_FALSE(era_test_link_recovery_step());
    advance_time(1500); era_test_link_noise(0,2); ASSERT_TRUE(era_test_link_recovery_step());
    era_test_link_relation(true,false,false,false);
    era_test_link_fault(1);
    EXPECT_FALSE(era_split_link_apply(ERA_SPLIT_LINK_LEVEL_HIGH));
    EXPECT_TRUE(era_test_link_fallback());
    bool on=true;
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(&on));
    EXPECT_FALSE(on);
}
TEST_F(EraSplitLinkLifecycle, TalkerRaisesTheListenersSearchReportFromItsAnswer) {
    // Left never steps; the answer that opened this relation says the listener did.
    EXPECT_FALSE(era_split_link_rate_searched());
    era_test_link_meet(true, true, true, true, true, true);
    EXPECT_FALSE(era_split_link_rate_searched());
    bool on=false;
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(&on));
    // A checked equal-rate reconciliation reaches idle before the report request.
    ASSERT_TRUE(era_split_link_apply(ERA_SPLIT_LINK_LEVEL_LOW));
    era_split_link_task(); era_split_restart_agreement_task();
    const auto offer=arm(); ASSERT_EQ(offer.act,ERA_SPLIT_RESTART_ACT_LINK_RECOVERED);
    auto reply=authority(); reply.restart_act=offer.act; reply.restart_param=0; reply.restart_armed=true;
    era_split_restart_agreement_note_peer_authority(&reply);
    set_time(offer.deadline); era_split_restart_agreement_task();
    ASSERT_TRUE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_TRUE(on);
    advance_time(ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_ON_MS);
    ASSERT_TRUE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_FALSE(on);
    advance_time(ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_OFF_MS);
    ASSERT_TRUE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_TRUE(on);
    advance_time(ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_ON_MS);
    ASSERT_TRUE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_FALSE(on);
    advance_time(ERA_SPLIT_LINK_RECONCILE_SUCCESS_REPORT_TAIL_MS);
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_FALSE(on);
    // The storage-close revalidation reopens the same pair; that answer no longer says searched.
    era_test_link_meet(false, true, true, true, false, false);
    era_test_link_meet(true, true, true, true, false, false);
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_FALSE(on);
}
TEST_F(EraSplitLinkLifecycle, AnAnswerWithoutASearchRaisesNothingOnTheTalker) {
    era_test_link_meet(true, true, true, true, true, false);
    bool on=true;
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_FALSE(on);
}
TEST_F(EraSplitLinkLifecycle, ListenerAdvertisesItsSearchOnlyUntilTheRelationItFoundOpens) {
    era_test_link_set_physical(ERA_SPLIT_LINK_LEVEL_MEDIUM,true);
    era_test_link_relation(false,false,false,false);
    EXPECT_FALSE(era_split_link_rate_searched());
    era_test_link_noise(0,0); EXPECT_FALSE(era_test_link_recovery_step());
    EXPECT_FALSE(era_split_link_rate_searched());
    advance_time(1500); era_test_link_noise(0,2); ASSERT_TRUE(era_test_link_recovery_step());
    EXPECT_TRUE(era_split_link_rate_searched());
    // Unserviced passes while still listening keep advertising it to the next probe.
    era_test_link_relation(false,false,false,false);
    EXPECT_TRUE(era_split_link_rate_searched());
    era_test_link_relation(true,false,false,false);
    EXPECT_FALSE(era_split_link_rate_searched());
    bool on=false;
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(&on)); EXPECT_FALSE(on);
}
TEST_F(EraSplitLinkLifecycle, AnAbandonedSearchStopsAdvertisingBeforeAnyLaterProbe) {
    era_test_link_set_physical(ERA_SPLIT_LINK_LEVEL_MEDIUM,true);
    era_test_link_relation(false,false,false,false);
    era_test_link_noise(0,0); EXPECT_FALSE(era_test_link_recovery_step());
    advance_time(1500); era_test_link_noise(0,2); ASSERT_TRUE(era_test_link_recovery_step());
    EXPECT_TRUE(era_split_link_rate_searched());
    era_test_link_relation(false,true,true,true); // listener episode ended without service
    EXPECT_FALSE(era_split_link_rate_searched());
}
TEST_F(EraSplitLinkLifecycle, EachProgramCutRebootsToOneCompleteRecord) {
    for(unsigned cut=1;cut<=5;++cut) {
        SCOPED_TRACE(cut); flash[0]=FakeNor{}; choose(0,1000); era_test_link_boot(); seed(0);
        nor().fail_at=cut; commit_local(1);
        auto outcome=era_split_restart_agreement_last_result();
        if(outcome==ERA_SPLIT_RESTART_RESULT_FAILED) EXPECT_EQ(era_test_link_stored(),0);
        else EXPECT_EQ(outcome,ERA_SPLIT_RESTART_RESULT_SUCCEEDED);
        nor().fail_at=0; era_test_link_boot(); auto recovered=replay();
        EXPECT_TRUE(recovered==(std::array<uint8_t,4>{0,0,0,0}) || recovered==(std::array<uint8_t,4>{1,0,0,0}));
        if(outcome==ERA_SPLIT_RESTART_RESULT_SUCCEEDED) EXPECT_EQ(recovered[0],1);
        EXPECT_EQ(era_split_link_pending_level(),recovered[0]); EXPECT_EQ(era_split_link_active_level(),2);
    }
}
TEST_F(EraSplitLinkLifecycle, NewRequestClearsPriorTerminalResultAndSameStoredRuntimeIsInert) {
    commit_local(1); EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_SUCCEEDED);
    EXPECT_FALSE(era_split_link_request_apply()); EXPECT_FALSE(era_split_restart_agreement_in_flight());
    ASSERT_TRUE(era_split_link_set_pending_level(0)); ASSERT_TRUE(era_split_link_request_apply());
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_NONE);
}
TEST_F(EraSplitLinkLifecycle, TimedReservationStartsAtFirstTaskWithoutHidQuietAndEndsAtCommit) {
    era_test_link_quiet(false);
    ASSERT_TRUE(era_split_link_set_pending_level(1)); ASSERT_TRUE(era_split_link_request_apply());
    EXPECT_FALSE(era_split_restart_agreement_timed_window());
    era_split_restart_agreement_task(); EXPECT_TRUE(era_split_restart_agreement_timed_window());
    advance_time(120); era_split_restart_agreement_task(); EXPECT_FALSE(era_split_restart_agreement_timed_window());
}

TEST_F(EraSplitLinkLifecycle, AFailedSealReadbackCannotTurnRamNoChangeIntoDurableSuccess) {
    const auto original = nor();
    commit_local(1); const auto commit_programs = nor().programs;
    ASSERT_GT(commit_programs,0U);
    nor()=original; era_test_link_boot(); EXPECT_EQ(era_split_link_pending_level(),0);
    nor().programs=0; nor().fail_read_after_program=commit_programs;
    commit_local(1);
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_FAILED);
    EXPECT_EQ(era_test_link_stored(),0);
    nor().fail_read_after_program=0;
    ASSERT_EQ(replay()[0],1); // Physical commit landed, but its readback failed.
    commit_local(0);
    // The adapter's RAM equality is not a reboot-equivalent receipt after an
    // ambiguous I/O failure. This attempt must either repair or report failure.
    if (era_split_restart_agreement_last_result()==ERA_SPLIT_RESTART_RESULT_SUCCEEDED) {
        EXPECT_EQ(replay()[0],0);
    } else {
        EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_FAILED);
    }
}

TEST_F(EraSplitLinkLifecycle, AFailedActivationReadbackAlsoRepairsRamIdenticalReplacement) {
    const auto original = nor(); era_test_link_full_journal();
    commit_local(1); const auto programs = nor().programs; ASSERT_GT(programs,5U);
    nor()=original; era_test_link_boot(); EXPECT_EQ(era_split_link_pending_level(),0);
    era_test_link_full_journal(); nor().programs=0; nor().fail_read_after_program=programs;
    commit_local(1); EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_FAILED);
    nor().fail_read_after_program=0; ASSERT_EQ(replay()[0],1);
    commit_local(0); EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_SUCCEEDED);
    EXPECT_EQ(replay()[0],0); EXPECT_EQ(era_test_link_stored(),0);
}
TEST_F(EraSplitLinkLifecycle, UncachedPeerArmDoesNotReplayStorageBeforeItsDeadline) {
    era_test_link_boot(); era_test_link_relation(true,false,false,false);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_SPEED,1,timer_read32()+120);
    nor().reads=0; era_split_link_task(); uint8_t next=0;
    EXPECT_FALSE(era_split_link_step_due(&next)); EXPECT_EQ(nor().reads,0U);
    EXPECT_FALSE(era_test_link_stored_cached());
    advance_time(120); era_split_restart_agreement_task();
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_SUCCEEDED);
    EXPECT_EQ(era_test_link_stored(),1);
}
TEST_F(EraSplitLinkLifecycle, LateIdleCannotRetroactivelyCancelReachedCommit) {
    era_test_link_relation(true,false,false,false);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_SPEED,2,timer_read32()+120);
    advance_time(120); era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_NONE,0,0);
    era_split_restart_agreement_task();
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_SUCCEEDED);
    EXPECT_EQ(replay()[0],2);
}
TEST_F(EraSplitLinkLifecycle, LateEchoCannotReviveExpiredProposalBeforeTimeoutTask) {
    era_test_link_relation(true,true,true,true); begin_local(1); advance_time(60);
    auto reply=authority(); reply.restart_act=ERA_SPLIT_RESTART_ACT_LINK_SPEED; reply.restart_param=1; reply.restart_armed=true;
    era_split_restart_agreement_note_peer_authority(&reply); era_split_restart_agreement_task();
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_ABORTED);
    advance_time(60); era_split_restart_agreement_task(); EXPECT_EQ(nor().programs,0U);
}
TEST_F(EraSplitLinkLifecycle, LinkRecordDoesNotOverlapBusyMacroTransaction) {
    const uint8_t invalid_marker=1;
    eeprom_write_block(&invalid_marker,reinterpret_cast<void *>(617U+16384U-1U),1);
    ASSERT_TRUE(era_eeprom_driver_macro_transaction_open());
    commit_local(1); EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_SUCCEEDED);
    EXPECT_EQ(replay()[0],1); EXPECT_TRUE(era_eeprom_driver_macro_transaction_open());
}

TEST_F(EraSplitLinkLifecycle, RecoveryDoesNotWaitForUnreadyUncachedStorage) {
    era_test_link_boot(); era_test_link_driver_ready(false); era_test_link_relation(true,false,false,false);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_SPEED,1,timer_read32()+120);
    advance_time(120); era_split_restart_agreement_task();
    ASSERT_EQ(era_test_link_physical(),1); ASSERT_FALSE(era_test_link_stored_cached());
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_FAILED);
    era_test_link_relation(false,true,true,true); advance_time(200); era_split_link_task();
    ASSERT_TRUE(era_test_link_recovery_step()); EXPECT_EQ(era_test_link_physical(),2);
    EXPECT_EQ(nor().programs,0U);
}
TEST_F(EraSplitLinkLifecycle, SuccessfulPeerActReplacesEarlierFallbackLatch) {
    era_test_link_fault(1); commit_local(1); ASSERT_TRUE(era_test_link_fallback());
    era_test_link_fault(0); era_split_link_task();
    era_test_link_relation(true,false,false,false);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_SPEED,1,timer_read32()+120);
    advance_time(120); era_split_restart_agreement_task();
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_SUCCEEDED);
    EXPECT_FALSE(era_test_link_fallback()); EXPECT_EQ(era_test_link_stored(),1);
}

TEST_F(EraSplitLinkLifecycle, RecoveryReportHasOneEpochInAllThreeRelations) {
    // DUAL-HOST, Right HOST, Left HOST. The last makes the searcher initiator.
    for (const auto roles : {std::array<bool,2>{true,true}, {true,false}, {false,true}}) {
        SetUp(); agree_recovery_report(roles[0],roles[1]);
        for (unsigned side=0;side<2;++side) {
            choose(side,side==0 ? 1119 : 5119); era_split_restart_agreement_task();
            EXPECT_FALSE(era_split_link_reconcile_success_report_advance(nullptr));
            advance_time(1); era_split_restart_agreement_task();
            EXPECT_FALSE(era_split_restart_agreement_in_flight());
            EXPECT_EQ(nor().programs,0U); EXPECT_EQ(nor().erases,0U);
            EXPECT_EQ(era_test_link_physical(),ERA_SPLIT_LINK_LEVEL_LOW);
            EXPECT_EQ(era_test_link_resets(),0U);
        }
        for (uint32_t elapsed : {0U,1U,200U,319U,320U,639U,640U,959U,960U,1439U,1440U,2000U}) {
            const bool active=elapsed<1440;
            const bool lit=elapsed<320 || (elapsed>=640 && elapsed<960);
            expect_report(0,1120+elapsed,active,lit);
            expect_report(1,5120+elapsed,active,lit);
        }
        for (unsigned side=0;side<2;++side) {
            choose(side,side==0 ? 3200 : 7200);
            era_split_link_task(); era_split_restart_agreement_task();
            EXPECT_FALSE(era_split_restart_agreement_in_flight()); // no second local request
        }
    }
}

TEST_F(EraSplitLinkLifecycle, RecoveryReportAgreementOverlapsConfirmationInEveryRelation) {
    for (const auto roles : {std::array<bool,2>{true,true}, {true,false}, {false,true}}) {
        SetUp();
        const uint32_t clocks[2]={1000,5000};
        const unsigned initiator=roles[0] ? 0 : 1;
        const unsigned responder=1-initiator;
        const unsigned winner=roles[1] ? 0 : 1;
        for (unsigned side=0;side<2;++side) {
            choose(side,clocks[side]);
            era_test_link_clock_offset(side==initiator ? static_cast<int32_t>(clocks[responder]-clocks[initiator]) : 0);
            if (side==1) search_listener_to(ERA_SPLIT_LINK_LEVEL_LOW,clocks[side]);
            era_test_link_meet(true,side==initiator,side==0,side==winner,true,side==0);
            era_split_link_task(); era_split_restart_agreement_task();
        }
        if (winner!=initiator) {
            choose(winner,clocks[winner]); const auto request=authority();
            choose(initiator,clocks[initiator]);
            era_split_restart_agreement_note_peer_authority(&request); era_split_restart_agreement_task();
        }
        choose(initiator,clocks[initiator]); const auto rate=arm();
        ASSERT_EQ(rate.act,ERA_SPLIT_RESTART_ACT_LINK_SPEED);
        choose(responder,clocks[responder]);
        era_split_restart_agreement_note_peer_arm(rate.act,rate.param,rate.deadline);
        auto echo=authority();
        choose(initiator,clocks[initiator]); era_split_restart_agreement_note_peer_authority(&echo);
        for (unsigned side=0;side<2;++side) {
            choose(side,clocks[side]+120); era_split_restart_agreement_task();
            EXPECT_EQ(era_test_link_physical(),ERA_SPLIT_LINK_LEVEL_HIGH);
            EXPECT_FALSE(era_split_link_reconcile_success_report_advance(nullptr));
            nor().programs=nor().erases=0;
        }
        for (unsigned side=0;side<2;++side) {
            choose(side,clocks[side]+199); era_split_link_task();
            EXPECT_FALSE(era_split_restart_agreement_in_flight());
            choose(side,clocks[side]+200); era_test_link_quiet(false);
            era_split_link_task(); era_split_restart_agreement_task();
        }
        choose(initiator,clocks[initiator]+200); const auto report=arm();
        ASSERT_EQ(report.act,ERA_SPLIT_RESTART_ACT_LINK_RECOVERED);
        EXPECT_EQ(report.deadline,clocks[responder]+320);
        choose(responder,clocks[responder]+200);
        era_split_restart_agreement_note_peer_arm(report.act,report.param,report.deadline);
        echo=authority();
        choose(initiator,clocks[initiator]+200); era_split_restart_agreement_note_peer_authority(&echo);
        for (unsigned side=0;side<2;++side) {
            choose(side,clocks[side]+319); era_split_restart_agreement_task();
            EXPECT_FALSE(era_split_link_reconcile_success_report_advance(nullptr));
            choose(side,clocks[side]+320); era_split_restart_agreement_task();
            era_split_link_task();
            expect_report(side,clocks[side]+320,true,true);
            EXPECT_EQ(nor().programs,0U); EXPECT_EQ(nor().erases,0U);
        }
    }
}

TEST_F(EraSplitLinkLifecycle, FirstVisibilityTwoHundredMsLateDoesNotRebaseEitherPulse) {
    agree_recovery_report();
    choose(0,1120); era_split_restart_agreement_task();
    choose(1,5120); era_split_restart_agreement_task();
    expect_report(0,1120,true,true); // Right has not advanced its presentation.
    expect_report(1,5320,true,true);
    expect_report(0,1440,true,false);
    expect_report(1,5440,true,false); // old first-advance origin would still be ON
    expect_report(0,1760,true,true);
    expect_report(1,5760,true,true);
}

TEST_F(EraSplitLinkLifecycle, DifferentServicedEdgesWaitForOneFuturePresentationStart) {
    agree_recovery_report(true,true,1000,5000,true,200);
    choose(1,5119); era_split_restart_agreement_task();
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(nullptr));
    choose(0,1120); era_split_restart_agreement_task();
    choose(1,5120); era_split_restart_agreement_task();
    expect_report(0,1120,true,true); expect_report(1,5120,true,true);
    expect_report(0,1440,true,false); expect_report(1,5440,true,false);
}

TEST_F(EraSplitLinkLifecycle, DuplicateRecoveryArmsCannotRestartOrReplaceTheEpoch) {
    agree_recovery_report();
    choose(1,5030);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_RECOVERED,0,5120);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_RECOVERED,0,5150);
    choose(0,1120); era_split_restart_agreement_task();
    choose(1,5120); era_split_restart_agreement_task();
    choose(1,5130);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_RECOVERED,0,5120);
    EXPECT_FALSE(era_split_restart_agreement_in_flight());
    expect_report(0,1440,true,false); expect_report(1,5440,true,false);
}

TEST_F(EraSplitLinkLifecycle, ConfirmedReportKeepsItsDeadlineAndOutcomeThroughServiceGap) {
    agree_recovery_report();
    for (unsigned side=0;side<2;++side) {
        choose(side,side==0 ? 1060 : 5060);
        era_split_restart_agreement_note_relation(false,false,side==0);
        EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_NONE);
        EXPECT_TRUE(era_split_restart_agreement_in_flight());
        era_split_restart_agreement_note_relation(true,side==0,side==0);
    }
    choose(0,1120); era_split_restart_agreement_task();
    choose(1,5120); era_split_restart_agreement_task();
    expect_report(0,1440,true,false); expect_report(1,5440,true,false);
}

TEST_F(EraSplitLinkLifecycle, SearchedFallbackCanReportWithoutEnablingARateRaise) {
    era_test_link_fault(1); EXPECT_FALSE(era_split_link_apply(ERA_SPLIT_LINK_LEVEL_HIGH));
    ASSERT_TRUE(era_test_link_fallback()); era_test_link_fault(0);
    era_test_link_meet(true,true,true,true,true,true);
    era_split_link_task(); era_split_restart_agreement_task();
    const auto offer=arm(); ASSERT_EQ(offer.act,ERA_SPLIT_RESTART_ACT_LINK_RECOVERED);
    auto reply=authority(); reply.restart_act=offer.act; reply.restart_param=0; reply.restart_armed=true;
    era_split_restart_agreement_note_peer_authority(&reply);
    set_time(offer.deadline); era_split_restart_agreement_task();
    EXPECT_TRUE(era_split_link_reconcile_success_report_advance(nullptr));
    EXPECT_TRUE(era_test_link_fallback()); EXPECT_EQ(nor().programs,0U);
    EXPECT_EQ(era_test_link_physical(),ERA_SPLIT_LINK_LEVEL_LOW);
}

TEST_F(EraSplitLinkLifecycle, LateColdDispatchAlsoUsesTheAcceptedInstant) {
    agree_recovery_report();
    choose(0,1120); era_split_restart_agreement_task();
    choose(1,5320); era_split_restart_agreement_task(); // 200 ms late, not a new epoch
    expect_report(0,1440,true,false);
    expect_report(1,5440,true,false);
    expect_report(0,1760,true,true);
    expect_report(1,5760,true,true);
}

TEST_F(EraSplitLinkLifecycle, HiddenOrSleepingReportExpiresRatherThanReplaying) {
    agree_recovery_report();
    choose(0,1120); era_split_restart_agreement_task();
    choose(1,5120); era_split_restart_agreement_task();
    expect_report(0,2560,false,false);
    expect_report(1,16560,false,false); // no visible callbacks in either lifetime
    expect_report(1,16561,false,false);
}

TEST_F(EraSplitLinkLifecycle, ClockAndRoleFlipCannotMoveRecoveryPresentation) {
    agree_recovery_report();
    choose(0,1060); era_split_restart_agreement_note_relation_rotation();
    era_test_link_clock_offset(0); era_split_restart_agreement_note_relation(true,false,true);
    choose(1,5060); era_split_restart_agreement_note_relation_rotation();
    era_test_link_clock_offset(-4000); era_split_restart_agreement_note_relation(true,true,false);
    choose(0,1120); era_split_restart_agreement_task();
    choose(1,5120); era_split_restart_agreement_task();
    expect_report(0,1440,true,false); expect_report(1,5440,true,false);
    expect_report(0,1760,true,true); expect_report(1,5760,true,true);
}

TEST_F(EraSplitLinkLifecycle, RecoveryEpochCanBeZeroAcrossLocalTimerWrap) {
    agree_recovery_report(true,true,UINT32_MAX-119U,5000);
    uint32_t deadline=99;
    choose(0,UINT32_MAX-1U);
    ASSERT_TRUE(era_split_restart_agreement_agreed_deadline(&deadline)); EXPECT_EQ(deadline,0U);
    choose(0,0); era_split_restart_agreement_task();
    choose(1,5120); era_split_restart_agreement_task();
    expect_report(0,0,true,true); expect_report(1,5120,true,true);
    expect_report(0,320,true,false); expect_report(1,5440,true,false);
    expect_report(0,1440,false,false); expect_report(1,6560,false,false);
}

TEST_F(EraSplitLinkLifecycle, LostRecoveryEchoRetiresAndDisarmsWithoutLocalReplay) {
    agree_recovery_report(true,true,1000,5000,false);
    choose(0,1060); era_split_restart_agreement_task();
    const auto idle=arm(); ASSERT_EQ(idle.act,ERA_SPLIT_RESTART_ACT_NONE);
    choose(1,5070); era_split_restart_agreement_note_peer_arm(idle.act,idle.param,idle.deadline);
    choose(0,1120); era_split_restart_agreement_task();
    choose(1,5120); era_split_restart_agreement_task();
    expect_report(0,1120,false,false); expect_report(1,5120,false,false);
    choose(0,2000); era_split_link_task(); era_split_restart_agreement_task();
    EXPECT_FALSE(era_split_restart_agreement_in_flight());
}

TEST_F(EraSplitLinkLifecycle, LaterRuntimeFailureCancelsAnActiveRecoveryReport) {
    agree_recovery_report();
    choose(0,1120); era_split_restart_agreement_task();
    expect_report(0,1120,true,true);
    era_test_link_fault(1); EXPECT_FALSE(era_split_link_apply(ERA_SPLIT_LINK_LEVEL_HIGH));
    expect_report(0,1200,false,false);
}

TEST_F(EraSplitLinkLifecycle, NotificationWaitsForTimeAnchorAndStorageWithoutLocalBlink) {
    era_test_link_meet(true,true,true,true,true,true);
    ASSERT_TRUE(era_split_link_apply(ERA_SPLIT_LINK_LEVEL_LOW));
    era_test_link_anchor(false); era_split_link_task(); era_split_restart_agreement_task();
    EXPECT_EQ(arm().act,ERA_SPLIT_RESTART_ACT_NONE);
    era_test_link_anchor(true); era_test_link_storage_busy(true); era_split_restart_agreement_task();
    EXPECT_EQ(arm().act,ERA_SPLIT_RESTART_ACT_NONE);
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(nullptr));
    era_test_link_storage_busy(false); era_split_restart_agreement_task();
    EXPECT_EQ(arm().act,ERA_SPLIT_RESTART_ACT_LINK_RECOVERED);
}

TEST_F(EraSplitLinkLifecycle, PeerOnlyNotificationNeverFallsBackToStandalone) {
    EXPECT_FALSE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_RECOVERED,0));
    EXPECT_FALSE(era_split_link_reconcile_success_report_commit());
    era_test_link_meet(true,true,true,true,true,false); era_test_link_quiet(false);
    ASSERT_TRUE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_RECOVERED,0));
    era_test_link_meet(false,false,true,true,false,false);
    era_test_link_quiet(true); advance_time(1000); era_split_restart_agreement_task();
    EXPECT_FALSE(era_split_restart_agreement_in_flight());
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_ABORTED);
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(nullptr));
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_RECOVERED,0,timer_read32()+120);
    EXPECT_FALSE(era_split_restart_agreement_in_flight());
}

TEST_F(EraSplitLinkLifecycle, PureRecoveryReportNeverWaitsForRawHidQuiet) {
    era_test_link_meet(true,true,true,true,true,true);
    ASSERT_TRUE(era_split_link_apply(ERA_SPLIT_LINK_LEVEL_LOW));
    era_test_link_quiet(false);
    era_split_link_task(); era_split_restart_agreement_task();
    EXPECT_EQ(arm().act,ERA_SPLIT_RESTART_ACT_LINK_RECOVERED);
    EXPECT_EQ(arm().deadline,timer_read32()+ERA_SPLIT_RESTART_COMMIT_DELAY_MS);
}

TEST_F(EraSplitLinkLifecycle, FailedConfirmationCancelsAnAlreadyAgreedFutureReport) {
    era_test_link_meet(true,true,true,true,true,true);
    ASSERT_TRUE(era_split_link_apply(ERA_SPLIT_LINK_LEVEL_HIGH));
    advance_time(100); era_split_link_task(); era_split_restart_agreement_task();
    const auto offer=arm(); ASSERT_EQ(offer.act,ERA_SPLIT_RESTART_ACT_LINK_RECOVERED);
    auto echo=authority(); echo.restart_act=offer.act; echo.restart_param=0; echo.restart_armed=true;
    era_split_restart_agreement_note_peer_authority(&echo);
    advance_time(100); era_test_link_meet(false,true,true,true,false,false);
    era_split_link_task(); ASSERT_TRUE(era_test_link_fallback());
    set_time(offer.deadline); era_split_restart_agreement_task();
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(nullptr));
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_FAILED);
}

static std::string link_label(uint8_t id) {
    uint8_t data[32]={id_custom_get_value,ERA_VIA_SYSTEM_CHANNEL,id};
    EXPECT_TRUE(era_split_via_link_handle_via_command(data,sizeof(data)));
    return reinterpret_cast<const char *>(&data[3]);
}

TEST_F(EraSplitLinkLifecycle, ApplyReadbackSeparatesSelectionRuntimeStorageAndReceipt) {
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_RUNTIME_VALUE_ID),"Low");
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_STORED_VALUE_ID),"High");
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_RESULT_VALUE_ID),"No Apply this boot");
    ASSERT_TRUE(era_split_link_set_pending_level(1));
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_RUNTIME_VALUE_ID),"Low");
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_STORED_VALUE_ID),"High");
    EXPECT_FALSE(era_split_link_apply_report_advance(nullptr,nullptr));
}

TEST_F(EraSplitLinkLifecycle, EqualApplyIsExplicitWithoutAnyWriteOrRestart) {
    ASSERT_TRUE(era_split_link_apply(1));
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_NONE);
    nor().programs=nor().erases=0;
    const auto transitions=era_test_link_transition_count();
    EXPECT_FALSE(era_split_link_request_apply());
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_UNCHANGED);
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_RESULT_VALUE_ID),"Already set");
    bool on=false; EXPECT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_TRUE(on);
    advance_time(640); EXPECT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_FALSE(on);
    advance_time(160); EXPECT_FALSE(era_split_link_apply_report_advance(nullptr,&on));
    EXPECT_EQ(nor().programs,0U); EXPECT_EQ(nor().erases,0U);
    EXPECT_EQ(era_test_link_transition_count(),transitions); EXPECT_EQ(era_test_link_resets(),0U);
}

TEST_F(EraSplitLinkLifecycle, LocalApplyReportsPendingThenCheckedSuccessAndExpiresOnlyPresentation) {
    begin_local(1);
    bool pending_on=true;
    EXPECT_FALSE(era_split_link_apply_report_advance(nullptr,&pending_on)); EXPECT_FALSE(pending_on);
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_RESULT_VALUE_ID),"Pending Medium");
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_STORED_VALUE_ID),"High");
    advance_time(120); era_split_restart_agreement_task();
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_APPLIED);
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_RESULT_VALUE_ID),"Applied Medium");
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_RUNTIME_VALUE_ID),"Medium");
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_STORED_VALUE_ID),"Medium");
    bool on=false; EXPECT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_TRUE(on);
    advance_time(640); EXPECT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_FALSE(on);
    advance_time(160); EXPECT_FALSE(era_split_link_apply_report_advance(nullptr,&on));
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_RESULT_VALUE_ID),"Applied Medium");
    EXPECT_EQ(era_test_link_resets(),0U);
}

TEST_F(EraSplitLinkLifecycle, DuplicateApplyAndDropdownEditsKeepTheOriginalReceiptTarget) {
    begin_local(1); ASSERT_TRUE(era_split_link_set_pending_level(0));
    EXPECT_FALSE(era_split_link_request_apply());
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_RESULT_VALUE_ID),"Pending Medium");
    EXPECT_TRUE(era_split_restart_agreement_holds_intent(ERA_SPLIT_RESTART_ACT_LINK_SPEED,1));
    EXPECT_FALSE(era_split_restart_agreement_holds_intent(ERA_SPLIT_RESTART_ACT_LINK_SPEED,0));
    advance_time(120); era_split_restart_agreement_task();
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_RESULT_VALUE_ID),"Applied Medium");
}

TEST_F(EraSplitLinkLifecycle, PairedApplyReceiptIsPromptAndHealthCheckCannotRearmIt) {
    agree_pair(1);
    choose(0,1120); era_split_restart_agreement_task();
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_APPLIED);
    bool on=false; ASSERT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_TRUE(on);
    choose(1,5120); era_split_restart_agreement_task();
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_NONE);
    choose(0,1319); era_split_link_task(); EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_APPLIED);
    choose(0,1320); era_split_link_task(); EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_APPLIED);
    choose(1,5320); era_split_link_task(); EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_NONE);
    choose(0,1760); ASSERT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_FALSE(on);
    choose(0,1920); EXPECT_FALSE(era_split_link_apply_report_advance(nullptr,&on));
}

TEST_F(EraSplitLinkLifecycle, FailedHealthCheckCorrectsThePromptLocalReceipt) {
    agree_pair(1); choose(0,1120); era_split_restart_agreement_task();
    ASSERT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_APPLIED);
    era_test_link_relation(false,true,true,true); advance_time(200); era_split_link_task();
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_FAILED);
    EXPECT_EQ(era_test_link_resets(),0U);
}

TEST_F(EraSplitLinkLifecycle, RuntimeAndStorageFaultsProduceFailedReceipts) {
    for (unsigned fault : {1U,2U,3U,4U,5U}) {
        SetUp();
        if (fault==5) nor().fail_at=1; else era_test_link_fault(fault);
        commit_local(1);
        EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_FAILED);
        EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_RESULT_VALUE_ID),"Failed - check levels");
        EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_STORED_VALUE_ID),"High");
        EXPECT_EQ(era_test_link_resets(),0U);
    }
}

TEST_F(EraSplitLinkLifecycle, ExpiredApplyReceiptCannotBorrowALaterSuccessfulReport) {
    era_test_link_relation(true,true,true,true); begin_local(1);
    advance_time(60); era_split_restart_agreement_task(); era_split_link_task();
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_CANCELLED);
    ASSERT_TRUE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_LINK_RECOVERED,0));
    era_split_restart_agreement_task(); const auto offer=arm();
    auto echo=authority(); echo.restart_act=offer.act; echo.restart_param=0; echo.restart_armed=true;
    era_split_restart_agreement_note_peer_authority(&echo);
    set_time(offer.deadline); era_split_restart_agreement_task(); era_split_link_task();
    EXPECT_EQ(era_split_restart_agreement_last_result(),ERA_SPLIT_RESTART_RESULT_SUCCEEDED);
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_CANCELLED);
}

TEST_F(EraSplitLinkLifecycle, BusyReceiptDoesNotChangeAnUnrelatedAgreement) {
    era_test_link_quiet(false);
    ASSERT_TRUE(era_split_restart_agreement_request(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,0));
    ASSERT_TRUE(era_split_link_set_pending_level(1));
    EXPECT_FALSE(era_split_link_request_apply());
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_BUSY);
    EXPECT_TRUE(era_split_restart_agreement_holds_intent(ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN,0));
    EXPECT_EQ(nor().programs,0U);
}

TEST_F(EraSplitLinkLifecycle, ACompetingPeerLevelCancelsRatherThanForgingLocalSuccess) {
    era_test_link_relation(true,false,true,true); era_test_link_quiet(false); begin_local(1);
    era_split_restart_agreement_note_peer_arm(ERA_SPLIT_RESTART_ACT_LINK_SPEED,0,timer_read32()+120);
    era_split_link_task();
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_CANCELLED);
    advance_time(120); era_split_restart_agreement_task();
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_CANCELLED);
    EXPECT_EQ(era_test_link_physical(),0);
}

TEST_F(EraSplitLinkLifecycle, PendingIsSilentAndTerminalPulseExpiresAcrossWrap) {
    set_time(UINT32_MAX-50U); era_test_link_quiet(false); begin_local(1);
    bool on=true; EXPECT_FALSE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_FALSE(on);
    advance_time(120); era_split_restart_agreement_task();
    ASSERT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_TRUE(on);
    advance_time(5000); EXPECT_FALSE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_FALSE(on);
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_APPLIED);
}

TEST_F(EraSplitLinkLifecycle, ReadOnlyLabelsAreLengthCheckedAndCannotWriteOnSetOrSave) {
    for (uint8_t id : {uint8_t(64),uint8_t(65),uint8_t(66)}) {
        for (uint8_t command : {uint8_t(id_custom_set_value),uint8_t(id_custom_save)}) {
            std::array<uint8_t,32> data{}; data[0]=command; data[1]=ERA_VIA_SYSTEM_CHANNEL; data[2]=id;
            const auto before=data;
            EXPECT_FALSE(era_split_via_link_handle_via_command(data.data(),data.size()));
            EXPECT_EQ(data,before);
        }
        std::array<uint8_t,32> data{}; data[0]=id_custom_get_value; data[1]=ERA_VIA_SYSTEM_CHANNEL; data[2]=id;
        const auto before=data;
        for(uint8_t n=0;n<7;++n) {
            EXPECT_FALSE(era_split_via_link_handle_via_command(data.data(),n)); EXPECT_EQ(data,before);
        }
    }
    EXPECT_EQ(nor().programs,0U); EXPECT_EQ(nor().erases,0U);
    uint8_t old_apply[4]={id_custom_get_value,ERA_VIA_SYSTEM_CHANNEL,ERA_SPLIT_VIA_LINK_APPLY_VALUE_ID,99};
    ASSERT_TRUE(era_split_via_link_handle_via_command(old_apply,4)); EXPECT_EQ(old_apply[3],0);
}

TEST_F(EraSplitLinkLifecycle, UnreadableStorageIsUnknownNotAnInventedSavedLevel) {
    era_test_link_boot(); era_test_link_driver_ready(false);
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_STORED_VALUE_ID),"Unknown");
    EXPECT_FALSE(era_test_link_stored_cached());
}

TEST_F(EraSplitLinkLifecycle, RebootRetainsSavedLevelButStartsLowWithoutAnOldApplyReceipt) {
    commit_local(1); ASSERT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_APPLIED);
    era_test_link_boot();
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_RUNTIME_VALUE_ID),"Low");
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_STORED_VALUE_ID),"Medium");
    EXPECT_EQ(link_label(ERA_SPLIT_VIA_LINK_RESULT_VALUE_ID),"No Apply this boot");
    EXPECT_FALSE(era_split_link_apply_report_advance(nullptr,nullptr));
}


TEST_F(EraSplitLinkLifecycle, PromptApplyWorksInBothDualHostDirectionsAndBothHostOrientations) {
    // left initiator, rate winner, requester: both DUAL-HOST directions and both HOSTs.
    for (const auto shape : {std::array<unsigned,3>{1,1,0}, {1,1,1}, {1,0,1}, {0,1,0}}) {
        for (uint8_t target=0;target<3;++target) {
            SetUp();
            const uint32_t clocks[2]={1000,5000};
            const unsigned initiator=shape[0] ? 0 : 1, responder=1-initiator, requester=shape[2];
            for (unsigned side=0;side<2;++side) {
                choose(side,clocks[side]); era_test_link_quiet(false);
                era_test_link_clock_offset(side==initiator ? static_cast<int32_t>(clocks[responder]-clocks[initiator]) : 0);
                era_test_link_meet(true,side==initiator,side==0,side==(shape[1] ? 0U : 1U),true,false);
            }
            choose(requester,clocks[requester]); begin_local(target);
            const auto request=authority();
            if (requester!=initiator) {
                choose(initiator,clocks[initiator]);
                era_split_restart_agreement_note_peer_authority(&request); era_split_restart_agreement_task();
            }
            const auto offer=arm(); ASSERT_EQ(offer.act,ERA_SPLIT_RESTART_ACT_LINK_SPEED);
            ASSERT_EQ(offer.param,target); ASSERT_EQ(offer.deadline,clocks[responder]+120);
            choose(responder,clocks[responder]);
            era_split_restart_agreement_note_peer_arm(offer.act,offer.param,offer.deadline);
            const auto answer=authority(); ASSERT_TRUE(answer.restart_armed);
            choose(initiator,clocks[initiator]); era_split_restart_agreement_note_peer_authority(&answer);
            for (unsigned side=0;side<2;++side) {
                choose(side,clocks[side]+119); era_split_restart_agreement_task();
                bool on=true; EXPECT_FALSE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_FALSE(on);
                EXPECT_EQ(era_test_link_physical(),ERA_SPLIT_LINK_LEVEL_LOW); EXPECT_EQ(nor().programs,0U);
                advance_time(1); era_split_restart_agreement_task();
                EXPECT_EQ(era_test_link_physical(),target); EXPECT_EQ(era_test_link_stored(),target);
                EXPECT_EQ(era_split_link_apply_status(),side==requester ? ERA_SPLIT_LINK_APPLY_APPLIED : ERA_SPLIT_LINK_APPLY_NONE);
                EXPECT_EQ(era_split_link_apply_report_advance(nullptr,&on),side==requester);
                EXPECT_EQ(on,side==requester); EXPECT_EQ(era_test_link_resets(),0U);
            }
        }
    }
}

TEST_F(EraSplitLinkLifecycle, StorageAndAnchorWaitAreSilentAndStillGateTheDeadline) {
    era_test_link_relation(true,true,true,true); era_test_link_quiet(false); era_test_link_anchor(false);
    begin_local(1);
    for (uint32_t ms : {1000U,1160U,1320U,1960U}) {
        set_time(ms); era_split_restart_agreement_task();
        ASSERT_EQ(arm().act,ERA_SPLIT_RESTART_ACT_NONE);
        bool on=true; EXPECT_FALSE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_FALSE(on);
        EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_PENDING);
    }
    era_test_link_anchor(true); era_test_link_storage_busy(true); era_split_restart_agreement_task();
    ASSERT_EQ(arm().act,ERA_SPLIT_RESTART_ACT_NONE); EXPECT_EQ(nor().programs,0U);
    era_test_link_storage_busy(false); era_split_restart_agreement_task();
    const auto offer=arm(); ASSERT_EQ(offer.act,ERA_SPLIT_RESTART_ACT_LINK_SPEED);
    auto answer=authority(); answer.restart_act=offer.act; answer.restart_param=1; answer.restart_armed=true;
    era_split_restart_agreement_note_peer_authority(&answer);
    set_time(offer.deadline); era_split_restart_agreement_task();
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_APPLIED);
}

TEST_F(EraSplitLinkLifecycle, SlowFlashHasNoProgressAndNoSuccessUntilDurableReturn) {
    agree_pair(1); choose(0,1120);
    nor().delay_ms=80; nor().check_wire=true; nor().check_receipt=true;
    era_split_restart_agreement_task();
    nor().check_receipt=false;
    EXPECT_FALSE(nor().observed_unready); EXPECT_EQ(era_test_link_transition_ms(),1120U);
    ASSERT_GT(nor().programs,0U); ASSERT_GE(timer_read32(),1320U); // injected, not measured device latency
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_APPLIED);
    EXPECT_EQ(replay()[0],1);
    bool on=false; EXPECT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_TRUE(on);
    advance_time(639); EXPECT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_TRUE(on);
    advance_time(1); EXPECT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_FALSE(on);
}

TEST_F(EraSplitLinkLifecycle, EveryFlashFailureStaysSilentUntilTheFailedReceipt) {
    commit_local(1); const unsigned programs=nor().programs;
    for (unsigned cut=1;cut<=programs;++cut) {
        SetUp(); begin_local(1); nor().fail_at=cut; nor().check_receipt=true;
        advance_time(120); era_split_restart_agreement_task(); nor().check_receipt=false;
        EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_FAILED);
        EXPECT_EQ(era_test_link_stored(),ERA_SPLIT_LINK_LEVEL_HIGH);
    }
    SetUp(); begin_local(1); nor().fail_read_after_program=programs; nor().check_receipt=true;
    advance_time(120); era_split_restart_agreement_task(); nor().check_receipt=false;
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_FAILED); // even a landed seal is not an acknowledged write
}

TEST_F(EraSplitLinkLifecycle, JournalRotationAlsoFinishesBeforeTheOnlySuccessPulse) {
    begin_local(1); era_test_link_full_journal(); nor().check_receipt=true;
    advance_time(120); era_split_restart_agreement_task(); nor().check_receipt=false;
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_APPLIED);
    EXPECT_EQ(replay()[0],1);
    bool on=false; EXPECT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_TRUE(on);
}

TEST_F(EraSplitLinkLifecycle, SameSettingGetsTheSameTerminalDurationWithoutWireOrFlashWork) {
    commit_local(1); advance_time(800);
    const auto programs=nor().programs, transitions=era_test_link_transition_count();
    EXPECT_FALSE(era_split_link_request_apply());
    ASSERT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_UNCHANGED);
    bool on=false; EXPECT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_TRUE(on);
    advance_time(639); EXPECT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_TRUE(on);
    advance_time(1); EXPECT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_FALSE(on);
    advance_time(160); EXPECT_FALSE(era_split_link_apply_report_advance(nullptr,&on));
    EXPECT_FALSE(era_split_restart_agreement_in_flight());
    EXPECT_EQ(nor().programs,programs); EXPECT_EQ(era_test_link_transition_count(),transitions);
}

TEST_F(EraSplitLinkLifecycle, RepeatedClickDuringHealthObservationCannotRestartThePulse) {
    agree_pair(1); choose(0,1120); era_split_restart_agreement_task();
    advance_time(100); EXPECT_FALSE(era_split_link_request_apply());
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_APPLIED);
    set_time(1320); era_split_link_task();
    set_time(1760); bool on=true; EXPECT_TRUE(era_split_link_apply_report_advance(nullptr,&on)); EXPECT_FALSE(on);
}

TEST_F(EraSplitLinkLifecycle, LaterPeerActCannotReassignItsFailureToThePreviousLocalReceipt) {
    agree_pair(1); choose(0,1120); era_split_restart_agreement_task();
    advance_time(10); ASSERT_TRUE(era_split_link_apply(0)); // different, peer-originated transition
    era_test_link_relation(false,true,true,true); advance_time(200); era_split_link_task();
    EXPECT_TRUE(era_test_link_fallback());
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_APPLIED);
    EXPECT_EQ(era_split_link_apply_target(),1); // historical receipt, not that peer act
}

TEST_F(EraSplitLinkLifecycle, StorageFailureCanBeRetriedAtTheAlreadyRunningLevel) {
    nor().fail_at=1; commit_local(1);
    ASSERT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_FAILED);
    ASSERT_EQ(era_test_link_physical(),1); ASSERT_EQ(era_test_link_stored(),0);
    nor().fail_at=0; commit_local(1);
    EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_APPLIED);
    EXPECT_EQ(replay()[0],1); EXPECT_EQ(era_test_link_resets(),0U);
}

TEST_F(EraSplitLinkLifecycle, LeftHostReportsPeerSearchWithoutAnIdleBetweenRateAndReport) {
    // Right starts at boot Low and really follows two undecodable probes to
    // High. Left was already running High. Both stores also contain High.
    choose(0,1000); era_test_link_set_physical(ERA_SPLIT_LINK_LEVEL_HIGH,true);
    choose(1,5000); era_test_link_relation(false,false,false,false);
    ASSERT_FALSE(era_test_link_recovery_step());
    era_test_link_noise(0,ERA_SPLIT_LINK_SCAN_NOISE_MIN);
    advance_time(ERA_SPLIT_LINK_SCAN_DWELL_MS);
    ASSERT_TRUE(era_test_link_recovery_step());
    ASSERT_EQ(era_test_link_physical(),ERA_SPLIT_LINK_LEVEL_HIGH);
    ASSERT_TRUE(era_split_link_rate_searched());
    // Discovery changes Right from listener to HOST-PEER initiator.
    const uint32_t clocks[2]={2500,6500};
    choose(0,clocks[0]); era_test_link_clock_offset(0);
    era_test_link_meet(true,false,true,true,true,true);
    era_split_link_task(); era_split_restart_agreement_task();
    const auto request=authority();
    ASSERT_EQ(request.restart_act,ERA_SPLIT_RESTART_ACT_LINK_SPEED);
    choose(1,clocks[1]); era_test_link_clock_offset(-4000);
    era_test_link_meet(true,true,false,false,true,false);
    era_split_restart_agreement_note_peer_authority(&request);
    era_split_restart_agreement_task();
    const auto rate=arm(); ASSERT_EQ(rate.act,ERA_SPLIT_RESTART_ACT_LINK_SPEED);
    choose(0,clocks[0]);
    era_split_restart_agreement_note_peer_arm(rate.act,rate.param,rate.deadline);
    const auto echo=authority();
    choose(1,clocks[1]); era_split_restart_agreement_note_peer_authority(&echo);
    for (unsigned side=0;side<2;++side) {
        choose(side,clocks[side]+120);
        era_split_restart_agreement_task(); era_split_link_task();
        ASSERT_EQ(era_test_link_physical(),ERA_SPLIT_LINK_LEVEL_HIGH);
        ASSERT_EQ(nor().programs,0U);
        ASSERT_FALSE(era_split_link_reconcile_success_report_advance(nullptr));
    }
    // A zero-length intermediate state need not be sampled by the standing
    // exchange. Send only what production publishes, never an invented idle.
    choose(0,clocks[0]+130);
    era_split_restart_agreement_task(); era_split_link_task();
    const auto next=authority();
    choose(1,clocks[1]+130);
    era_split_restart_agreement_note_peer_authority(&next);
    era_split_restart_agreement_task(); era_split_link_task();
    const auto report=arm();
    ASSERT_EQ(report.act,ERA_SPLIT_RESTART_ACT_LINK_RECOVERED);
    choose(0,clocks[0]+130);
    era_split_restart_agreement_note_peer_arm(report.act,report.param,report.deadline);
    const auto report_echo=authority();
    choose(1,clocks[1]+130); era_split_restart_agreement_note_peer_authority(&report_echo);
    for (unsigned side=0;side<2;++side) {
        choose(side,clocks[side]+250);
        era_split_restart_agreement_task(); era_split_link_task();
        expect_report(side,clocks[side]+250,true,true);
        EXPECT_EQ(nor().programs,0U); EXPECT_EQ(nor().erases,0U);
        EXPECT_EQ(era_test_link_resets(),0U);
    }
}

TEST_F(EraSplitLinkLifecycle, SampledLatestStateRecoversOnceAcrossAllRolesAndRates) {
    // The endpoint tasks and wire sampling have independent cadences. Carry
    // actual current publications only; a fleeting idle need not be sampled.
    for (const auto roles : {std::array<bool,2>{true,true}, {true,false}, {false,true}}) {
        for (uint8_t found=0;found<ERA_SPLIT_LINK_LEVEL_COUNT;++found) {
            for (uint8_t target=0;target<ERA_SPLIT_LINK_LEVEL_COUNT;++target) {
                for (bool searched : {false,true}) {
                    SCOPED_TRACE(::testing::Message() << "left_initiator=" << roles[0]
                        << " left_winner=" << roles[1] << " found=" << unsigned(found)
                        << " target=" << unsigned(target) << " searched=" << searched);
                    SetUp();
                    const uint32_t clocks[2]={10000,14000};
                    const unsigned initiator=roles[0] ? 0 : 1, responder=1-initiator;
                    for (unsigned side=0;side<2;++side) {
                        choose(side,clocks[side]-2000);
                        ASSERT_TRUE(era_split_link_apply(target));
                        if (side==1 && searched) {
                            search_listener_to(found,clocks[side]);
                        } else {
                            era_test_link_set_physical(found,true);
                        }
                        choose(side,clocks[side]);
                        era_test_link_clock_offset(side==initiator ?
                            static_cast<int32_t>(clocks[responder]-clocks[initiator]) : 0);
                        era_test_link_meet(true,side==initiator,side==0,
                            side==(roles[1] ? 0U : 1U),true,searched && side==0);
                        nor().programs=nor().erases=0;
                    }
                    unsigned pulses[2]={}, report_arms=0;
                    bool previous_on[2]={}, previous_active[2]={};
                    uint32_t first_on[2]={}, first_off[2]={}, last_off[2]={}, ended[2]={};
                    Arm previous_arm{};
                    for (uint32_t t=0;t<=2000;++t) {
                        for (unsigned side=0;side<2;++side) {
                            choose(side,clocks[side]+t);
                            era_split_restart_agreement_task(); era_split_link_task();
                            bool on=false;
                            const bool active=era_split_link_reconcile_success_report_advance(&on);
                            if (on && !previous_on[side]) {
                                if (!pulses[side]) first_on[side]=t;
                                ++pulses[side];
                            }
                            if (!on && previous_on[side]) {
                                if (pulses[side]==1) first_off[side]=t;
                                last_off[side]=t;
                            }
                            if (!active && previous_active[side]) ended[side]=t;
                            previous_on[side]=on; previous_active[side]=active;
                        }
                        if (t%10==0) {
                            choose(initiator,clocks[initiator]+t);
                            const auto offer=arm(); const auto initiator_authority=authority();
                            if (offer.act==ERA_SPLIT_RESTART_ACT_LINK_RECOVERED &&
                                (previous_arm.act!=offer.act || previous_arm.deadline!=offer.deadline)) ++report_arms;
                            previous_arm=offer;
                            choose(responder,clocks[responder]+t);
                            const auto responder_authority=authority();
                            era_split_restart_agreement_note_peer_authority(&initiator_authority);
                            era_split_restart_agreement_note_peer_arm(offer.act,offer.param,offer.deadline);
                            choose(initiator,clocks[initiator]+t);
                            era_split_restart_agreement_note_peer_authority(&responder_authority);
                        }
                    }
                    EXPECT_EQ(report_arms,searched ? 1U : 0U);
                    EXPECT_EQ(first_on[0],first_on[1]);
                    for (unsigned side=0;side<2;++side) {
                        choose(side,clocks[side]+2000);
                        EXPECT_EQ(pulses[side],searched ? 2U : 0U);
                        if (searched) {
                            EXPECT_EQ(first_off[side]-first_on[side],320U);
                            EXPECT_EQ(last_off[side]-first_on[side],960U);
                            EXPECT_EQ(ended[side]-first_on[side],1440U);
                        }
                        EXPECT_EQ(era_test_link_physical(),target);
                        EXPECT_EQ(era_test_link_stored(),target);
                        EXPECT_EQ(nor().programs,0U); EXPECT_EQ(nor().erases,0U);
                        EXPECT_EQ(era_split_link_apply_status(),ERA_SPLIT_LINK_APPLY_NONE);
                        EXPECT_FALSE(era_split_restart_agreement_in_flight());
                    }
                }
            }
        }
    }
}



// Sender and listener are retuned together. These are logical time tests,
// not measurements of wire latency, Core0 load or LED output.
TEST_F(EraSplitLinkLifecycle, DiscoveryWindowAdvancesAt400msWithoutAnEarlyPhase) {
    era_test_link_relation(false,false,false,false);
    ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(10); era_test_link_noise(0,1); ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(110); era_test_link_noise(0,2); ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(279); ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(1); ASSERT_TRUE(era_test_link_recovery_step());
    EXPECT_EQ(era_test_link_physical(),ERA_SPLIT_LINK_LEVEL_HIGH);
    EXPECT_EQ(nor().programs,0U); EXPECT_EQ(nor().erases,0U);
    EXPECT_EQ(era_test_link_resets(),0U);
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(nullptr));
}

TEST_F(EraSplitLinkLifecycle, BurstCannotShortenTheCompleteDiscoveryWindow) {
    era_test_link_relation(false,false,false,false);
    ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(10); era_test_link_noise(0,20);
    for (unsigned elapsed=10;elapsed<400;elapsed+=10) {
        set_time(1000+elapsed); EXPECT_FALSE(era_test_link_recovery_step());
    }
    set_time(1400); EXPECT_TRUE(era_test_link_recovery_step());
}

TEST_F(EraSplitLinkLifecycle, AcceptedFrameDominatesNoiseThroughoutTheWindow) {
    era_test_link_relation(false,false,false,false);
    ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(10); era_test_link_noise(0,20); ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(389); era_test_link_noise(1,100); EXPECT_FALSE(era_test_link_recovery_step());
    advance_time(1); EXPECT_FALSE(era_test_link_recovery_step());
    advance_time(400); EXPECT_FALSE(era_test_link_recovery_step());
    EXPECT_EQ(era_test_link_transition_count(),0U);
}

TEST_F(EraSplitLinkLifecycle, ListenerExitRetiresEvidenceWithoutAnInterveningStepTask) {
    era_test_link_relation(false,false,false,false);
    ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(110); era_test_link_noise(0,2); ASSERT_FALSE(era_test_link_recovery_step());
    era_test_link_meet(false,true,false,false,false,false);
    advance_time(2000);
    era_test_link_meet(false,false,false,false,false,false);
    EXPECT_FALSE(era_test_link_recovery_step());
    advance_time(400); EXPECT_FALSE(era_test_link_recovery_step());
}

TEST_F(EraSplitLinkLifecycle, DiscoveryWindowAndErrorCountsAreWrapSafe) {
    set_time(UINT32_MAX-200U); era_test_link_noise(0,UINT32_MAX-1U);
    era_test_link_relation(false,false,false,false);
    ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(10); era_test_link_noise(0,UINT32_MAX); ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(110); era_test_link_noise(0,0); ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(279); ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(1); ASSERT_TRUE(era_test_link_recovery_step());
    EXPECT_EQ(era_test_link_physical(),ERA_SPLIT_LINK_LEVEL_HIGH);
}

TEST_F(EraSplitLinkLifecycle, LateCore0SampleCannotReplayMissedWindows) {
    era_test_link_relation(false,false,false,false);
    ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(10); era_test_link_noise(0,20);
    advance_time(1990); ASSERT_TRUE(era_test_link_recovery_step());
    ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(400); EXPECT_FALSE(era_test_link_recovery_step());
    EXPECT_EQ(era_test_link_transition_count(),1U);
    EXPECT_EQ(nor().programs,0U);
}

TEST_F(EraSplitLinkLifecycle, SingleBootBreakCannotAccumulateAcrossWindows) {
    era_test_link_relation(false,false,false,false);
    ASSERT_FALSE(era_test_link_recovery_step());
    for (unsigned count=1;count<=4;++count) {
        advance_time(10); era_test_link_noise(0,count);
        advance_time(390); EXPECT_FALSE(era_test_link_recovery_step());
    }
    EXPECT_EQ(era_test_link_transition_count(),0U);
    EXPECT_FALSE(era_split_link_rate_searched());
}

TEST_F(EraSplitLinkLifecycle, SilentDisconnectedHalvesDoNotReconfigureOrWrite) {
    for (bool initiator : {false,true}) {
        SetUp(); era_test_link_relation(false,initiator,initiator,initiator);
        for (unsigned t=0;t<=60000;t+=10) {
            set_time(1000+t); EXPECT_FALSE(era_test_link_recovery_step());
        }
        EXPECT_EQ(era_test_link_quiesce_count(),0U);
        EXPECT_EQ(era_test_link_transition_count(),0U);
        EXPECT_EQ(nor().programs,0U); EXPECT_EQ(nor().erases,0U);
        EXPECT_FALSE(era_split_link_rate_searched());
        EXPECT_FALSE(era_split_link_reconcile_success_report_advance(nullptr));
    }
}

TEST_F(EraSplitLinkLifecycle, NewDividerCannotReusePreviousDiscoveryEvidence) {
    era_test_link_relation(false,false,false,false);
    ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(110); era_test_link_noise(0,2);
    advance_time(290); ASSERT_TRUE(era_test_link_recovery_step());
    ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(400); EXPECT_FALSE(era_test_link_recovery_step());
    advance_time(400); EXPECT_FALSE(era_test_link_recovery_step());
    EXPECT_EQ(era_test_link_transition_count(),1U);
    EXPECT_EQ(nor().programs,0U);
}

TEST_F(EraSplitLinkLifecycle, FailedDiscoveryTransitionNeverPersistsAnUnagreedRate) {
    era_test_link_relation(false,false,false,false);
    ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(110); era_test_link_noise(0,2);
    advance_time(290); era_test_link_fault(2); EXPECT_FALSE(era_test_link_recovery_step());
    EXPECT_NE(era_test_link_dirty(),0U);
    EXPECT_EQ(nor().programs,0U); EXPECT_EQ(nor().erases,0U);
    EXPECT_FALSE(era_split_link_reconcile_success_report_advance(nullptr));
    era_test_link_fault(0); EXPECT_TRUE(era_test_link_repair());
    EXPECT_EQ(era_test_link_physical(),ERA_SPLIT_LINK_LEVEL_HIGH);
    EXPECT_EQ(era_test_link_stored(),ERA_SPLIT_LINK_LEVEL_HIGH);
}

TEST_F(EraSplitLinkLifecycle, PeriodicProbePhaseModelFindsEveryRateWithoutExtraTransitions) {
    // Supplied arrivals cover a fast probe, backed-off absent-peer response,
    // and a conservative Low known-peer window plus maintenance. 1ms phases
    // deliberately sweep across the independent 10ms listener task boundary.
    unsigned cases=0;
    for (uint32_t period : {30U,110U,190U}) {
        for (uint32_t phase=1;phase<=period;++phase) {
            for (uint8_t initial=0;initial<ERA_SPLIT_LINK_LEVEL_COUNT;++initial) {
                for (uint8_t target=0;target<ERA_SPLIT_LINK_LEVEL_COUNT;++target) {
                    SetUp(); choose(1,1000);
                    era_test_link_set_physical(initial,true);
                    era_test_link_relation(false,false,false,false);
                    ASSERT_FALSE(era_test_link_recovery_step());
                    unsigned errors=0, probes=0;
                    uint32_t found_at=0;
                    const unsigned hops=(target+ERA_SPLIT_LINK_LEVEL_COUNT-initial)%ERA_SPLIT_LINK_LEVEL_COUNT;
                    for (uint32_t t=1;t<=2500;++t) {
                        set_time(1000+t);
                        if (t>=phase && (t-phase)%period==0) {
                            ++probes;
                            if (era_test_link_physical()==target) {
                                era_test_link_noise(1,errors);
                                EXPECT_FALSE(era_test_link_recovery_step());
                                found_at=t;
                                break;
                            }
                            era_test_link_noise(0,++errors);
                        }
                        if (t%10==0) (void)era_test_link_recovery_step();
                        EXPECT_FALSE(era_split_link_reconcile_success_report_advance(nullptr));
                    }
                    ASSERT_NE(found_at,0U);
                    EXPECT_LE(found_at,hops*(400U+10U)+period);
                    EXPECT_EQ(era_test_link_transition_count(),hops);
                    EXPECT_EQ(probes,1U+(found_at-phase)/period);
                    EXPECT_EQ(era_test_link_physical(),target);
                    EXPECT_EQ(nor().programs,0U); EXPECT_EQ(nor().erases,0U);
                    ++cases;
                }
            }
        }
    }
    EXPECT_EQ(cases,2970U);
}

TEST_F(EraSplitLinkLifecycle, AcceptedFrameArrivingDuringTheDecisionVetoesTheStep) {
    era_test_link_relation(false,false,false,false);
    ASSERT_FALSE(era_test_link_recovery_step());
    advance_time(110); era_test_link_noise(0,2);
    advance_time(290); era_test_link_accept_after_next_read();
    EXPECT_FALSE(era_test_link_recovery_step());
    EXPECT_EQ(era_test_link_quiesce_count(),0U);
    EXPECT_EQ(era_test_link_physical(),ERA_SPLIT_LINK_LEVEL_LOW);
    EXPECT_FALSE(era_split_link_rate_searched());
}

TEST_F(EraSplitLinkLifecycle, OneCorruptProbeCannotHideTheNextCorrectRateProbe) {
    for (unsigned period : {110U,190U}) {
        for (unsigned phase=1;phase<=period;++phase) {
            SetUp(); era_test_link_relation(false,false,false,false);
            ASSERT_FALSE(era_test_link_recovery_step());
            for (unsigned t=1;t<=400;++t) {
                set_time(1000+t);
                // One malformed frame can produce multiple decoder errors.
                if (t==phase) era_test_link_noise(0,5);
                if (t==phase+period) era_test_link_noise(1,5);
                if (t%10==0) EXPECT_FALSE(era_test_link_recovery_step());
            }
            EXPECT_EQ(era_test_link_transition_count(),0U);
        }
    }
}

TEST_F(EraSplitLinkLifecycle, ContinuousNoiseCannotSpinTheRateSelector) {
    era_test_link_relation(false,false,false,false);
    ASSERT_FALSE(era_test_link_recovery_step());
    unsigned previous=0; uint32_t last=1000;
    for (unsigned t=10;t<=2000;t+=10) {
        set_time(1000+t); era_test_link_noise(0,t);
        (void)era_test_link_recovery_step();
        unsigned count=era_test_link_transition_count();
        if (count!=previous) {
            EXPECT_EQ(count,previous+1);
            EXPECT_GE(1000+t-last,400U);
            previous=count; last=1000+t;
        }
    }
    EXPECT_LE(previous,5U);
    EXPECT_EQ(nor().programs,0U); EXPECT_EQ(nor().erases,0U);
}
