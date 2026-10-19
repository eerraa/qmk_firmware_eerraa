// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <algorithm>
#include <cstring>
#include <vector>
#include "gtest/gtest.h"
#include "test_common.hpp"
#include "test_fixture.hpp"
#include "test_keymap_key.hpp"
extern "C" {
#include "era_macro.h"
#include "dynamic_keymap.h"
#include "send_string.h"
#include "timer.h"
#include "keyboards/era/common/system/era_state_sync.h"
void set_time(uint32_t time);
void suspend_wakeup_init(void);
}
using testing::_;
static std::array<uint8_t, 256> storage;
static uint32_t session = 1;
static bool connected = true, sleeping, pressure;
static unsigned reads, raw_replies;
static std::array<uint8_t,32> raw_reply;
static constexpr uint8_t mouse_directions[] = {MS_UP, MS_DOWN, MS_LEFT, MS_RGHT, MS_WHLU, MS_WHLD, MS_WHLL, MS_WHLR};
static uint8_t mouse_opposite(uint8_t code) {
    for (unsigned i = 0; i < sizeof(mouse_directions); ++i) {
        if (mouse_directions[i] == code) return mouse_directions[i ^ 1];
    }
    return KC_NO;
}
static int16_t mouse_axis(uint8_t code) {
    const auto report = mousekey_get_report();
    if (code == MS_UP || code == MS_DOWN) return report.y;
    if (code == MS_LEFT || code == MS_RGHT) return report.x;
    if (code == MS_WHLU || code == MS_WHLD) return report.v;
    return report.h;
}
extern "C" {
void raw_hid_send(uint8_t *data,uint8_t length) { ++raw_replies; std::copy_n(data,length,raw_reply.begin()); }
uint16_t nvm_dynamic_keymap_read_keycode(uint8_t,uint8_t,uint8_t) { return KC_NO; }
void nvm_dynamic_keymap_update_keycode(uint8_t,uint8_t,uint8_t,uint16_t) {}
void nvm_dynamic_keymap_erase(void) {}
void nvm_dynamic_keymap_read_buffer(uint32_t,uint32_t size,uint8_t *data) { std::memset(data,0,size); }
void nvm_dynamic_keymap_update_buffer(uint32_t,uint32_t,uint8_t *) {}
void nvm_dynamic_keymap_macro_update_buffer(uint32_t offset,uint32_t size,uint8_t *data) {
    for (uint32_t i=0;i<size && offset+i<storage.size();++i) storage[offset+i]=data[i];
}
void nvm_dynamic_keymap_macro_erase(void) {}
void nvm_dynamic_keymap_macro_reset(void) { storage.fill(0); }
uint32_t nvm_dynamic_keymap_macro_size(void) { return storage.size(); }
void nvm_dynamic_keymap_macro_read_buffer(uint32_t offset, uint32_t size, uint8_t *data) {
    ++reads;
    for (unsigned i = 0; i < size; ++i) data[i] = offset + i < storage.size() ? storage[offset + i] : 0;
}
void era_macro_platform_sync(void) { era_macro_session(session, connected, sleeping); }
bool era_macro_platform_pressured(void) { return pressure; }
void host_keyboard_delay(uint16_t) {} // Production transport has its separate regression suite.
}
class EraMacro : public TestFixture {
protected:
    testing::NiceMock<TestDriver> driver;
    std::vector<report_keyboard_t> reports;
    uint16_t consumer=0,system=0;
    uint8_t buttons=0;
    bool reset_on_consumer_restore=false;
    void SetUp() override {
        ON_CALL(driver, send_keyboard_mock(_)).WillByDefault([this](report_keyboard_t &r) { reports.push_back(r); });
        connected = true; sleeping = pressure = false; ++session;
        era_macro_platform_sync(); clear_keyboard();
        storage.fill(0); reads = 0; reports.clear(); set_time(1);
    }
    void TearDown() override { era_macro_cancel(); clear_keyboard(); }
    void load(std::initializer_list<uint8_t> data) { storage.fill(0); std::copy(data.begin(), data.end(), storage.begin()); }
    void step(unsigned n = 1) { while (n--) { era_macro_platform_sync(); era_macro_task(); } }
    bool held(uint8_t key) { return is_key_pressed(key); }
};
TEST_F(EraMacro, OutputScopesNestAndCleanupPreservesOtherContributors) {
    EXPECT_EQ(action_output_get_owner(), ACTION_OUTPUT_OWNER_REGULAR);
    register_code(KC_A); add_mods(MOD_BIT(KC_LSFT));
    auto regular = action_output_set_owner(ACTION_OUTPUT_OWNER_MACRO_HOLD);
    register_code(KC_A); register_code(KC_B); add_mods(MOD_BIT(KC_LSFT));
    auto outer = action_output_set_owner(ACTION_OUTPUT_OWNER_MACRO_TAP);
    EXPECT_EQ(outer, ACTION_OUTPUT_OWNER_MACRO_HOLD);
    register_code(KC_B); add_mods(MOD_BIT(KC_LCTL));

    action_output_clear_owner_keys(outer);
    action_output_clear_owner_mods(outer);
    action_output_clear_owner_hid(outer);
    EXPECT_EQ(action_output_get_owner(), ACTION_OUTPUT_OWNER_MACRO_TAP);
    EXPECT_TRUE(held(KC_A)); EXPECT_TRUE(held(KC_B));
    EXPECT_EQ(get_mods(), MOD_BIT(KC_LSFT) | MOD_BIT(KC_LCTL));

    EXPECT_EQ(action_output_set_owner(outer), ACTION_OUTPUT_OWNER_MACRO_TAP);
    action_output_clear_owner_keys(ACTION_OUTPUT_OWNER_MACRO_TAP);
    action_output_clear_owner_mods(ACTION_OUTPUT_OWNER_MACRO_TAP);
    EXPECT_EQ(action_output_get_owner(), outer);
    EXPECT_TRUE(held(KC_A)); EXPECT_FALSE(held(KC_B));
    EXPECT_EQ(get_mods(), MOD_BIT(KC_LSFT));
    EXPECT_EQ(action_output_set_owner(regular), outer);
    unregister_code(KC_A); del_mods(MOD_BIT(KC_LSFT));
    EXPECT_FALSE(held(KC_A)); EXPECT_EQ(get_mods(), 0);
}

TEST_F(EraMacro, OutputCancellationAndRegularModifierClearsKeepTheirDistinctMeaning) {
    add_mods(MOD_BIT(KC_LSFT)); add_weak_mods(MOD_BIT(KC_LALT));
    auto regular = action_output_set_owner(ACTION_OUTPUT_OWNER_MACRO_HOLD);
    add_mods(MOD_BIT(KC_LCTL)); add_weak_mods(MOD_BIT(KC_RALT)); ::add_key(KC_A);
    clear_mods(); // QMK clears regular real mods, even inside an owned scope.
    EXPECT_EQ(get_mods(), MOD_BIT(KC_LCTL));
    clear_weak_mods(); // Weak mods retain QMK's global clear-on-next-press rule.
    EXPECT_EQ(get_weak_mods(), 0);
    clear_keys(); // Report-only edits must not retire the owner's contribution.
    auto hold = action_output_set_owner(ACTION_OUTPUT_OWNER_MACRO_TAP);
    ::add_key(KC_A); del_key(KC_A);
    EXPECT_TRUE(held(KC_A)); // The first owner's contribution survived clear_keys.
    action_output_set_owner(hold);
    ::add_key(KC_A); // Same owner: idempotent; a single UP must still release it.
    del_key(KC_A); EXPECT_FALSE(held(KC_A));
    ::add_key(KC_A);
    action_output_reset_keys(); action_output_reset_hid(); clear_keys();
    EXPECT_EQ(action_output_get_owner(), ACTION_OUTPUT_OWNER_MACRO_HOLD);
    EXPECT_EQ(get_mods(), MOD_BIT(KC_LCTL));
    action_output_clear_owner_mods(ACTION_OUTPUT_OWNER_MACRO_HOLD);
    action_output_set_owner(regular);
    ::add_key(KC_B); ::add_key(KC_B); del_key(KC_B);
    EXPECT_FALSE(held(KC_B)); // Regular QMK output remains bit state.
    EXPECT_EQ(get_mods(), 0);
}

TEST_F(EraMacro, DelayKeepsMatrixAndReadsRunning) {
    KeymapKey a(0,0,0,KC_A); set_keymap({a});
    load({SS_QMK_PREFIX,SS_DELAY_CODE,'9','9','9','9','|','b',0});
    dynamic_keymap_macro_send(0); EXPECT_EQ(reads,0U); step();
    a.press(); run_one_scan_loop(); EXPECT_TRUE(held(KC_A));
    uint8_t public_byte; dynamic_keymap_macro_get_buffer(0,1,&public_byte); EXPECT_EQ(public_byte,SS_QMK_PREFIX);
    uint8_t query[32]={0x02,ERA_STATE_SYNC_KEYBOARD_VALUE,ERA_STATE_SYNC_ENVELOPE_VERSION,0,0x12,0x34};
    auto replies=raw_replies;EXPECT_TRUE(era_state_sync_via_command(query,sizeof(query)));EXPECT_EQ(raw_replies,replies+1);
    EXPECT_EQ(raw_reply[3],ERA_STATE_SYNC_STATUS_OK);EXPECT_EQ(raw_reply[5],0x34);
    a.release(); run_one_scan_loop(); EXPECT_FALSE(held(KC_A));
    EXPECT_FALSE(held(KC_B)); set_time(10001); step(8); EXPECT_FALSE(reports.empty());
}
#ifdef ERA_TAP_DANCE_ENABLE
TEST_F(EraMacro, MacroUpAndCancelPreservePhysicalAndTapDanceOwners) {
    register_code(KC_A); register_code(KC_LSFT);
    auto previous=action_output_set_owner(0); register_code(KC_A); register_code(KC_LSFT); action_output_set_owner(previous);
    reports.clear();
    load({SS_QMK_PREFIX,SS_DOWN_CODE,KC_A,SS_QMK_PREFIX,SS_DOWN_CODE,KC_LSFT,SS_QMK_PREFIX,SS_UP_CODE,KC_A,SS_QMK_PREFIX,SS_UP_CODE,KC_LSFT,0});
    dynamic_keymap_macro_send(0); step(3);
    for(auto &r: reports) { EXPECT_TRUE(r.mods & MOD_BIT(KC_LSFT)); EXPECT_NE(std::find(std::begin(r.keys),std::end(r.keys),KC_A),std::end(r.keys)); }
    era_macro_cancel(); EXPECT_TRUE(held(KC_A)); EXPECT_TRUE(get_mods() & MOD_BIT(KC_LSFT));
    unregister_code(KC_A); unregister_code(KC_LSFT); EXPECT_TRUE(held(KC_A));
    action_output_clear_owner_keys(0); action_output_clear_owner_mods(0); send_keyboard_report(); EXPECT_FALSE(held(KC_A)); EXPECT_EQ(get_mods(),0);
}
#endif
TEST_F(EraMacro, LongDataAndQueueAreBoundedAndSnapshotsDoNotMix) {
    storage.fill('a'); storage.back()=0;
    for(unsigned i=0;i<ERA_MACRO_QUEUE_CAPACITY;++i) EXPECT_TRUE(era_macro_request(0));
    EXPECT_FALSE(era_macro_request(0)); EXPECT_EQ(reads,0U);
    step(); EXPECT_EQ(reads,1U); EXPECT_LT(reports.size(),20U);
    storage.fill('b'); storage.back()=0;
    reports.clear(); step();
    for(auto &r:reports) EXPECT_EQ(std::find(std::begin(r.keys),std::end(r.keys),KC_B),std::end(r.keys));
    pressure=true; auto size=reports.size(); step(10); EXPECT_EQ(reports.size(),size);
    pressure=false; era_macro_cancel(); step(); EXPECT_EQ(reads,1U);
}
TEST_F(EraMacro, MalformedMarkerIdAndBoundsAbortSafely) {
    EXPECT_FALSE(era_macro_request(255));
    storage.back()=255; dynamic_keymap_macro_send(0); step(); EXPECT_TRUE(reports.empty());
    load({SS_QMK_PREFIX,SS_DOWN_CODE,KC_A,SS_QMK_PREFIX,SS_DELAY_CODE,'9','9','9','9','9','|',0});
    dynamic_keymap_macro_send(0); step(); EXPECT_FALSE(held(KC_A));
    load({SS_QMK_PREFIX,SS_TAP_CODE,0}); dynamic_keymap_macro_send(0); step(); EXPECT_FALSE(held(KC_A));
    storage.fill('a');storage[254]=SS_QMK_PREFIX;storage.back()=0; dynamic_keymap_macro_send(0);step(200);EXPECT_FALSE(held(KC_A));
    storage.fill(0);for(unsigned i=0;i<4;++i)dynamic_keymap_macro_send(0);unsigned before=reads;step();EXPECT_EQ(reads,before+1);
}
TEST_F(EraMacro, SuspendFreezesDelayAndSessionAndClearCancelOutputs) {
    load({SS_QMK_PREFIX,SS_DOWN_CODE,KC_A,SS_QMK_PREFIX,SS_DELAY_CODE,'1','0','0','|',SS_QMK_PREFIX,SS_UP_CODE,KC_A,0});
    dynamic_keymap_macro_send(0);step();EXPECT_TRUE(held(KC_A));
    set_time(21);sleeping=true;step();set_time(10021);step();EXPECT_TRUE(held(KC_A));
    sleeping=false;step();set_time(10100);step();EXPECT_TRUE(held(KC_A));set_time(10101);step();EXPECT_FALSE(held(KC_A));
    dynamic_keymap_macro_send(0);step();EXPECT_TRUE(held(KC_A));++session;step();EXPECT_FALSE(held(KC_A));
    dynamic_keymap_macro_send(0);step();clear_keyboard_but_mods_and_keys();set_time(20000);step(20);EXPECT_FALSE(held(KC_A));
}
TEST_F(EraMacro, AuxiliaryReleaseRestoresOtherOwners) {
    register_code(KC_VOLU);register_code(KC_SYSTEM_SLEEP);register_code(MS_BTN1);
    load({SS_QMK_PREFIX,SS_DOWN_CODE,KC_VOLD,SS_QMK_PREFIX,SS_UP_CODE,KC_VOLD,SS_QMK_PREFIX,SS_DOWN_CODE,KC_SYSTEM_WAKE,SS_QMK_PREFIX,SS_UP_CODE,KC_SYSTEM_WAKE,SS_QMK_PREFIX,SS_DOWN_CODE,MS_BTN1,SS_QMK_PREFIX,SS_UP_CODE,MS_BTN1,0});
    dynamic_keymap_macro_send(0);step(3);
    EXPECT_EQ(host_last_consumer_usage(),KEYCODE2CONSUMER(KC_VOLU));
    EXPECT_EQ(host_last_system_usage(),KEYCODE2SYSTEM(KC_SYSTEM_SLEEP));
    EXPECT_TRUE(mousekey_get_report().buttons & 1);
    era_macro_cancel();EXPECT_TRUE(mousekey_get_report().buttons & 1);
    unregister_code(KC_VOLU);unregister_code(KC_SYSTEM_SLEEP);unregister_code(MS_BTN1);
}

TEST_F(EraMacro, MouseOppositePhysicalDirectionSurvivesMacroCancel) {
    KeymapKey left(0, 0, 0, MS_LEFT);
    set_keymap({left});
    left.press();
    run_one_scan_loop();
    ASSERT_LT(mouse_axis(MS_LEFT), 0);
    load({SS_QMK_PREFIX, SS_DOWN_CODE, MS_RGHT, SS_QMK_PREFIX, SS_DELAY_CODE, '9', '9', '9', '9', '|', 0});
    dynamic_keymap_macro_send(0);
    step();
    ASSERT_GT(mouse_axis(MS_LEFT), 0);
    era_macro_cancel();
    EXPECT_LT(mouse_axis(MS_LEFT), 0);
    run_one_scan_loop();
    EXPECT_LT(mouse_axis(MS_LEFT), 0);
    left.release();
    run_one_scan_loop();
    EXPECT_EQ(mouse_axis(MS_LEFT), 0);
}

TEST_F(EraMacro, MouseOpposedDirectionsRestoreOnMacroUpAndCancel) {
    for (uint8_t held_code : mouse_directions) {
        SCOPED_TRACE(held_code);
        register_code(held_code);
        const auto held_value = mouse_axis(held_code);
        const auto displaced = mouse_opposite(held_code);
        ASSERT_NE(held_value, 0);
        load({SS_QMK_PREFIX, SS_DOWN_CODE, displaced, SS_QMK_PREFIX, SS_UP_CODE, displaced, 0});
        dynamic_keymap_macro_send(0);
        step(3);
        EXPECT_EQ(mouse_axis(held_code), held_value);
        era_macro_cancel();
        EXPECT_EQ(mouse_axis(held_code), held_value);
        load({SS_QMK_PREFIX, SS_DOWN_CODE, displaced, SS_QMK_PREFIX, SS_DELAY_CODE, '9', '9', '9', '9', '|', 0});
        dynamic_keymap_macro_send(0);
        step();
        EXPECT_EQ(mouse_axis(held_code), -held_value);
        era_macro_cancel();
        EXPECT_EQ(mouse_axis(held_code), held_value);
        unregister_code(held_code);
        EXPECT_EQ(mouse_axis(held_code), 0);
    }
}

TEST_F(EraMacro, MouseTemporaryOppositeTapPreservesPersistentDirection) {
    for (uint8_t held_code : mouse_directions) {
        SCOPED_TRACE(held_code);
        load({SS_QMK_PREFIX, SS_DOWN_CODE, held_code, SS_QMK_PREFIX, SS_TAP_CODE, mouse_opposite(held_code), 0});
        dynamic_keymap_macro_send(0);
        step(3);
        const bool positive = held_code == MS_DOWN || held_code == MS_RGHT || held_code == MS_WHLU || held_code == MS_WHLR;
        EXPECT_EQ(mouse_axis(held_code) > 0, positive);
        EXPECT_NE(mouse_axis(held_code), 0);
        era_macro_cancel();
        EXPECT_EQ(mouse_axis(held_code), 0);
    }
}

TEST_F(EraMacro, MouseRegularReleaseRestoresScopedOpposite) {
    for (uint8_t held_code : mouse_directions) {
        SCOPED_TRACE(held_code);
        const auto previous = action_output_set_owner(ACTION_OUTPUT_OWNER_MACRO_HOLD);
        register_code(held_code);
        const auto held_value = mouse_axis(held_code);
        action_output_set_owner(previous);
        register_code(mouse_opposite(held_code));
        ASSERT_EQ(mouse_axis(held_code), -held_value);
        unregister_code(mouse_opposite(held_code));
        EXPECT_EQ(mouse_axis(held_code), held_value);
        action_output_clear_owner_hid(ACTION_OUTPUT_OWNER_MACRO_HOLD);
        EXPECT_EQ(mouse_axis(held_code), 0);
    }
}

TEST_F(EraMacro, MouseHiddenAndSameCodeOwnerReleasesPreserveWinner) {
    for (uint8_t held_code : mouse_directions) {
        SCOPED_TRACE(held_code);
        const auto previous = action_output_set_owner(ACTION_OUTPUT_OWNER_MACRO_HOLD);
        register_code(held_code);
        action_output_set_owner(previous);
        register_code(mouse_opposite(held_code));
        const auto winner = mouse_axis(held_code);
        action_output_clear_owner_hid(ACTION_OUTPUT_OWNER_MACRO_HOLD);
        EXPECT_EQ(mouse_axis(held_code), winner); // Releasing a hidden direction leaves the winner.
        unregister_code(mouse_opposite(held_code));
        EXPECT_EQ(mouse_axis(held_code), 0);

        register_code(held_code);
        const auto held_value = mouse_axis(held_code);
        action_output_set_owner(ACTION_OUTPUT_OWNER_MACRO_HOLD);
        register_code(mouse_opposite(held_code));
        action_output_set_owner(ACTION_OUTPUT_OWNER_MACRO_TAP);
        register_code(mouse_opposite(held_code));
        action_output_set_owner(previous);
        action_output_clear_owner_hid(ACTION_OUTPUT_OWNER_MACRO_HOLD);
        EXPECT_EQ(mouse_axis(held_code), -held_value); // The second owner still holds the winning code.
        action_output_clear_owner_hid(ACTION_OUTPUT_OWNER_MACRO_TAP);
        EXPECT_EQ(mouse_axis(held_code), held_value);
        unregister_code(held_code);
        EXPECT_EQ(mouse_axis(held_code), 0);
    }
}

TEST_F(EraMacro, MouseOrdinaryOpposedDirectionsKeepQmkReleaseSemantics) {
    for (uint8_t held_code : mouse_directions) {
        SCOPED_TRACE(held_code);
        register_code(held_code);
        const auto held_value = mouse_axis(held_code);
        register_code(mouse_opposite(held_code));
        ASSERT_EQ(mouse_axis(held_code), -held_value);
        unregister_code(mouse_opposite(held_code));
        EXPECT_EQ(mouse_axis(held_code), 0);
        unregister_code(held_code);
    }
}

#ifdef ERA_TAP_DANCE_ENABLE
TEST_F(EraMacro, MouseOppositeScopedProducerSurvivesMacroCancel) {
    for (uint8_t held_code : mouse_directions) {
        SCOPED_TRACE(held_code);
        const auto previous = action_output_set_owner(0);
        register_code(held_code);
        const auto held_value = mouse_axis(held_code);
        action_output_set_owner(previous);
        load({SS_QMK_PREFIX, SS_DOWN_CODE, mouse_opposite(held_code), SS_QMK_PREFIX, SS_DELAY_CODE, '9', '9', '9', '9', '|', 0});
        dynamic_keymap_macro_send(0);
        step();
        ASSERT_EQ(mouse_axis(held_code), -held_value);
        era_macro_cancel();
        EXPECT_EQ(mouse_axis(held_code), held_value);
        action_output_clear_owner_hid(0);
        EXPECT_EQ(mouse_axis(held_code), 0);
    }
}
#endif

TEST_F(EraMacro, MouseOppositeCancelPreservesOtherAxesButtonsAndAcceleration) {
    report_mouse_t emitted{};
    ON_CALL(driver, send_mouse_mock(_)).WillByDefault([&emitted](report_mouse_t &report) { emitted = report; });
    register_code(MS_ACL1);
    for (uint8_t code : {MS_LEFT, MS_UP, MS_WHLU, MS_WHLL, MS_BTN1}) register_code(code);
    set_time(1001);
    mousekey_task();
    const auto baseline = emitted;
    const auto projection = mousekey_get_report();
    ASSERT_LT(baseline.x, 0);
    load({SS_QMK_PREFIX, SS_DOWN_CODE, MS_RGHT, SS_QMK_PREFIX, SS_DOWN_CODE, MS_ACL2,
          SS_QMK_PREFIX, SS_DELAY_CODE, '9', '9', '9', '9', '|', 0});
    dynamic_keymap_macro_send(0);
    step();
    era_macro_cancel();
    const auto restored = mousekey_get_report();
    EXPECT_LT(restored.x, 0);
    EXPECT_EQ(restored.y, projection.y);
    EXPECT_EQ(restored.v, projection.v);
    EXPECT_EQ(restored.h, projection.h);
    EXPECT_EQ(restored.buttons, projection.buttons);
    EXPECT_EQ(emitted.x, 0); // The final auxiliary replay is buttons-only.
    EXPECT_EQ(emitted.y, 0);
    EXPECT_EQ(emitted.v, 0);
    EXPECT_EQ(emitted.h, 0);
    EXPECT_EQ(emitted.buttons, 1);
    set_time(2001);
    mousekey_task();
    EXPECT_EQ(emitted.x, baseline.x);
    EXPECT_EQ(emitted.y, baseline.y);
    EXPECT_EQ(emitted.v, baseline.v);
    EXPECT_EQ(emitted.h, baseline.h);
    for (uint8_t code : {MS_LEFT, MS_UP, MS_WHLU, MS_WHLL, MS_BTN1, MS_ACL1}) unregister_code(code);
}

TEST_F(EraMacro, PendingIdsSnapshotLatestValidImageOnlyWhenStarted) {
    load({SS_QMK_PREFIX,SS_DELAY_CODE,'1','0','0','|','a',0});
    dynamic_keymap_macro_send(0);step();dynamic_keymap_macro_send(0);
    load({'b',0}); reports.clear();set_time(101);step();
    EXPECT_EQ(reads,1U);
    ASSERT_EQ(reports.size(),2U);EXPECT_EQ(reports[0].keys[0],KC_A);EXPECT_EQ(reports[1].keys[0],0);
    reports.clear();step();EXPECT_EQ(reads,2U);
    ASSERT_EQ(reports.size(),2U);EXPECT_EQ(reports[0].keys[0],KC_B);EXPECT_EQ(reports[1].keys[0],0);
    load({SS_QMK_PREFIX,SS_DELAY_CODE,'1','0','0','|',0});dynamic_keymap_macro_send(0);step();dynamic_keymap_macro_send(0);
    storage.back()=255;set_time(201);step(4);reports.clear();step(4);EXPECT_TRUE(reports.empty());
}
TEST_F(EraMacro, PersistentDownSurvivesTemporaryTapAndPhysicalRelease) {
    load({SS_QMK_PREFIX,SS_DOWN_CODE,KC_A,SS_QMK_PREFIX,SS_TAP_CODE,KC_A,0});
    dynamic_keymap_macro_send(0);step(3);EXPECT_TRUE(held(KC_A));
    register_code(KC_A);unregister_code(KC_A);EXPECT_TRUE(held(KC_A));
    load({SS_QMK_PREFIX,SS_UP_CODE,KC_A,0});dynamic_keymap_macro_send(0);step(3);EXPECT_FALSE(held(KC_A));
}

TEST_F(EraMacro, ReconnectResendsSurvivingAuxiliaryAfterRejectingOldMacroLifetime) {
    ON_CALL(driver,send_extra_mock(_)).WillByDefault([this](report_extra_t &r) {
        if(!era_macro_session_current(session)) return; // Actual platform source boundary.
        if(r.report_id==REPORT_ID_CONSUMER)consumer=r.usage;
        if(r.report_id==REPORT_ID_SYSTEM)system=r.usage;
    });
    ON_CALL(driver,send_mouse_mock(_)).WillByDefault([this](report_mouse_t &r) {
        if(era_macro_session_current(session))buttons=r.buttons;
    });
    register_code(KC_VOLU);register_code(KC_SYSTEM_SLEEP);register_code(MS_BTN1);
    load({SS_QMK_PREFIX,SS_DOWN_CODE,KC_VOLD,SS_QMK_PREFIX,SS_DOWN_CODE,KC_SYSTEM_WAKE,SS_QMK_PREFIX,SS_DOWN_CODE,MS_BTN2,0});
    dynamic_keymap_macro_send(0);step(3);
    consumer=system=buttons=0; ++session;era_macro_platform_sync();
    EXPECT_EQ(consumer,KEYCODE2CONSUMER(KC_VOLU));EXPECT_EQ(system,KEYCODE2SYSTEM(KC_SYSTEM_SLEEP));EXPECT_EQ(buttons,1);
    unregister_code(KC_VOLU);unregister_code(KC_SYSTEM_SLEEP);unregister_code(MS_BTN1);
}

TEST_F(EraMacro, ResetDuringFinalUpRetainsReplayObligationAfterCompletion) {
    ON_CALL(driver,send_extra_mock(_)).WillByDefault([this](report_extra_t &r) {
        if(reset_on_consumer_restore && r.report_id==REPORT_ID_CONSUMER && r.usage==KEYCODE2CONSUMER(KC_VOLU)) {
            reset_on_consumer_restore=false;
            ++session;consumer=system=buttons=0; // ISR immediately before source admission.
        }
        if(!era_macro_session_current(session)) return;
        if(r.report_id==REPORT_ID_CONSUMER)consumer=r.usage;
        if(r.report_id==REPORT_ID_SYSTEM)system=r.usage;
    });
    ON_CALL(driver,send_mouse_mock(_)).WillByDefault([this](report_mouse_t &r) {
        if(era_macro_session_current(session))buttons=r.buttons;
    });
    register_code(KC_VOLU);register_code(KC_SYSTEM_SLEEP);register_code(MS_BTN1);
    load({SS_QMK_PREFIX,SS_DOWN_CODE,KC_VOLD,SS_QMK_PREFIX,SS_DOWN_CODE,KC_SYSTEM_WAKE,SS_QMK_PREFIX,SS_DOWN_CODE,MS_BTN2,
          SS_QMK_PREFIX,SS_UP_CODE,KC_SYSTEM_WAKE,SS_QMK_PREFIX,SS_UP_CODE,MS_BTN2,SS_QMK_PREFIX,SS_UP_CODE,KC_VOLD,0});
    reset_on_consumer_restore=true;dynamic_keymap_macro_send(0);step();
    EXPECT_FALSE(reset_on_consumer_restore);EXPECT_EQ(consumer,0);EXPECT_EQ(system,0);EXPECT_EQ(buttons,0);
    EXPECT_EQ(host_last_consumer_usage(),KEYCODE2CONSUMER(KC_VOLU));
    era_macro_platform_sync();
    EXPECT_EQ(consumer,KEYCODE2CONSUMER(KC_VOLU));EXPECT_EQ(system,KEYCODE2SYSTEM(KC_SYSTEM_SLEEP));EXPECT_EQ(buttons,1);
    unregister_code(KC_VOLU);unregister_code(KC_SYSTEM_SLEEP);unregister_code(MS_BTN1);
}

TEST_F(EraMacro, RealWakeRetiresRegularInputAndPreservesScopedOutputAndRemainingDelay) {
    ON_CALL(driver,send_extra_mock(_)).WillByDefault([this](report_extra_t &r) {
        if(r.report_id==REPORT_ID_CONSUMER)consumer=r.usage;
        if(r.report_id==REPORT_ID_SYSTEM)system=r.usage;
    });
    ON_CALL(driver,send_mouse_mock(_)).WillByDefault([this](report_mouse_t &r) {buttons=r.buttons;});
    register_code(KC_B);register_code(KC_LALT);register_code(KC_VOLU);register_code(MS_BTN1);
#ifdef ERA_TAP_DANCE_ENABLE
    auto previous=action_output_set_owner(0);
    register_code(KC_A);register_code(KC_LCTL);register_code(MS_BTN2);
    add_weak_mods(MOD_BIT(KC_RALT));
    action_output_set_owner(previous);
#endif
    load({SS_QMK_PREFIX,SS_DOWN_CODE,KC_A,SS_QMK_PREFIX,SS_DOWN_CODE,KC_LSFT,
          SS_QMK_PREFIX,SS_DOWN_CODE,KC_VOLD,SS_QMK_PREFIX,SS_DOWN_CODE,KC_SYSTEM_WAKE,
          SS_QMK_PREFIX,SS_DOWN_CODE,MS_BTN2,SS_QMK_PREFIX,SS_DELAY_CODE,'1','0','0','|',
          SS_QMK_PREFIX,SS_UP_CODE,KC_A,0});
    dynamic_keymap_macro_send(0);step(4);
    sleeping=true;era_macro_platform_sync();set_time(10001);step();
    suspend_wakeup_init();sleeping=false;era_macro_platform_sync();send_keyboard_report();
    EXPECT_TRUE(held(KC_A));EXPECT_FALSE(held(KC_B));
    EXPECT_TRUE(get_mods()&MOD_BIT(KC_LSFT));EXPECT_FALSE(get_mods()&MOD_BIT(KC_LALT));
    EXPECT_EQ(consumer,KEYCODE2CONSUMER(KC_VOLD));EXPECT_EQ(system,KEYCODE2SYSTEM(KC_SYSTEM_WAKE));
    EXPECT_EQ(buttons,2);EXPECT_EQ(mousekey_get_report().buttons,2);
#ifdef ERA_TAP_DANCE_ENABLE
    EXPECT_TRUE(get_mods()&MOD_BIT(KC_LCTL));EXPECT_TRUE(get_weak_mods()&MOD_BIT(KC_RALT));
#endif
    set_time(10100);step();EXPECT_TRUE(held(KC_A));
    set_time(10101);step(3);
#ifdef ERA_TAP_DANCE_ENABLE
    EXPECT_TRUE(held(KC_A));
#else
    EXPECT_FALSE(held(KC_A));
#endif
    era_macro_cancel();
    EXPECT_EQ(consumer,0);EXPECT_EQ(system,0);
#ifdef ERA_TAP_DANCE_ENABLE
    EXPECT_TRUE(held(KC_A));EXPECT_EQ(buttons,2);
    action_output_clear_owner_keys(0);action_output_clear_owner_mods(0);action_output_clear_owner_hid(0);
#endif
    EXPECT_FALSE(held(KC_A));EXPECT_EQ(buttons,0);
}
