// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cstring>
#include <vector>
#include "test_common.hpp"
extern "C" {
#include "keyboards/era/common/features/era_tapdance.h"
#include "keyboards/era/common/features/era_tapping.h"
#include "suspend.h"
}
using testing::_;
namespace {
std::array<uint8_t,280> config;
bool reject_td_down;
}
extern "C" {
uint32_t era_eeprom_read_config(void *data,uint32_t offset,uint32_t length) {
    if(offset>config.size() || length>config.size()-offset)return 0;
    std::memcpy(data,config.data()+offset,length);return length;
}
uint32_t era_eeprom_update_config(const void *data,uint32_t offset,uint32_t length) {
    if(offset>config.size() || length>config.size()-offset)return 0;
    std::memcpy(config.data()+offset,data,length);return length;
}
bool pre_process_record_user(uint16_t keycode,keyrecord_t *record) {
    return !(reject_td_down && record->event.pressed && IS_QK_TAP_DANCE(keycode));
}
void host_keyboard_delay(uint16_t) {}
}
class EraWakeInput : public TestFixture {
protected:
    testing::NiceMock<TestDriver> driver;
    std::vector<report_keyboard_t> reports;
    bool previous_oneshot_enable;
    void SetUp() override {
        ON_CALL(driver,send_keyboard_mock(_)).WillByDefault([this](report_keyboard_t &report){reports.push_back(report);});
        reject_td_down=false;config.fill(0);era_tapdance_init();era_tapping_init();
        previous_oneshot_enable=keymap_config.oneshot_enable;
        era_tapping_set_hold_on_other_key_press(false);era_tapping_set_permissive_hold(false);
        era_tapping_set_retro_tapping(false);
#ifdef SWAP_HANDS_ENABLE
        swap_hands_off();
#endif
        for(uint8_t row=0;row<MATRIX_ROWS;++row)for(uint8_t col=0;col<MATRIX_COLS;++col)
            update_source_layers_cache((keypos_t){.col=col,.row=row},0);
    }
    void TearDown() override {
        keymap_config.oneshot_enable=previous_oneshot_enable;
        reject_td_down=false;clear_all_keys();idle_for(TAPPING_TERM*2+1);
        tap_dance_cancel_all();clear_keyboard();layer_clear();default_layer_set(1);
#ifdef SWAP_HANDS_ENABLE
        swap_hands_off();
#endif
    }
    void mapping(std::initializer_list<KeymapKey> keys) {
        set_keymap(keys);
        for(uint8_t layer=0;layer<4;++layer)for(uint8_t row=0;row<MATRIX_ROWS;++row)
            for(uint8_t col=0;col<MATRIX_COLS;++col)
                if(!find_key(layer,(keypos_t){.col=col,.row=row}))
                    add_key(KeymapKey(layer,col,row,layer?KC_TRNS:KC_NO));
    }
    void down(uint8_t col) {press_key(col,0);run_one_scan_loop();}
    void up(uint8_t col) {release_key(col,0);run_one_scan_loop();}
    void dance(uint16_t hold,uint8_t slot=0) {
        ASSERT_TRUE(era_tapdance_set_action(slot,0,KC_Z));
        ASSERT_TRUE(era_tapdance_set_action(slot,1,hold));
        ASSERT_TRUE(era_tapdance_set_action(slot,2,KC_NO));
        ASSERT_TRUE(era_tapdance_set_action(slot,3,KC_NO));
    }
    void wake(bool reconcile=false) {
        ASSERT_TRUE(suspend_wakeup_condition());
        if(reconcile)update_matrix_state_after_wakeup();
        suspend_wakeup_init();
    }
    bool reported(uint8_t code) {
        for(const auto &report:reports)for(uint8_t key:report.keys)if(key==code)return true;
        return false;
    }
#ifdef SWAP_HANDS_ENABLE
    void synthetic_release(bool expired) {
        mapping({KeymapKey(0,1,0,TD(0)),KeymapKey(0,2,0,LT(1,KC_X)),KeymapKey(1,1,0,KC_A),
                 KeymapKey(0,3,0,MT(MOD_LCTL,KC_B)),KeymapKey(0,4,0,MT(MOD_LALT,KC_C))});
        swap_hands_on();down(2);down(0);wake();idle_for(TAPPING_TERM+1);
        ASSERT_TRUE(is_key_pressed(KC_A));down(3);up(3);down(3);
        ASSERT_TRUE(is_key_pressed(KC_B));if(expired)idle_for(TAPPING_TERM+1);down(4);
        ASSERT_FALSE(is_key_pressed(KC_B));up(4);up(3);up(0);EXPECT_FALSE(is_key_pressed(KC_A));
        up(2);layer_clear();
    }
#endif
};
TEST_F(EraWakeInput,HeldDanceKeyReleasesAfterRealWake) {
    mapping({KeymapKey(0,0,0,TD(0))});dance(KC_A);down(0);idle_for(TAPPING_TERM+1);
    ASSERT_TRUE(is_key_pressed(KC_A));wake();ASSERT_TRUE(is_key_pressed(KC_A));up(0);
    EXPECT_FALSE(is_key_pressed(KC_A));EXPECT_EQ(tap_dance_get_state(0),nullptr);
}
TEST_F(EraWakeInput,HeldDanceModifierReleasesAfterRealWake) {
    mapping({KeymapKey(0,0,0,TD(0))});dance(KC_LCTL);down(0);idle_for(TAPPING_TERM+1);
    ASSERT_EQ(get_mods(),MOD_BIT(KC_LCTL));wake(true);ASSERT_EQ(get_mods(),MOD_BIT(KC_LCTL));up(0);
    EXPECT_EQ(get_mods(),0);EXPECT_EQ(tap_dance_get_state(0),nullptr);
}
TEST_F(EraWakeInput,HeldDanceLayerReleasesAfterRealWake) {
    mapping({KeymapKey(0,0,0,TD(0))});dance(MO(1));down(0);idle_for(TAPPING_TERM+1);
    ASSERT_TRUE(layer_state_is(1));wake();up(0);
    EXPECT_FALSE(layer_state_is(1));EXPECT_EQ(tap_dance_get_state(0),nullptr);
}

TEST_F(EraWakeInput,HeldMomentaryLayerReleasesAfterWake) {
    mapping({KeymapKey(0,0,0,MO(1))});down(0);ASSERT_TRUE(layer_state_is(1));
    wake();ASSERT_TRUE(layer_state_is(1));up(0);EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraWakeInput,HeldLayerTapReleasesAfterWake) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X))});down(0);idle_for(TAPPING_TERM+1);
    ASSERT_TRUE(layer_state_is(1));wake();up(0);EXPECT_FALSE(layer_state_is(1));
    EXPECT_FALSE(is_key_pressed(KC_X));
}
TEST_F(EraWakeInput,HeldLayerModsWakeReleasePreservesFreshModifier) {
    mapping({KeymapKey(0,0,0,LM(1,MOD_LCTL)),KeymapKey(0,1,0,KC_LCTL)});
    down(0);ASSERT_TRUE(layer_state_is(1));ASSERT_EQ(get_mods(),MOD_BIT(KC_LCTL));
    wake();ASSERT_EQ(get_mods(),0);down(1);up(0);
    EXPECT_FALSE(layer_state_is(1));EXPECT_EQ(get_mods(),MOD_BIT(KC_LCTL));up(1);
}
TEST_F(EraWakeInput,RetainedLayerWakeReleaseUsesOriginalActionAfterRemap) {
    mapping({KeymapKey(0,0,0,MO(1)),KeymapKey(0,1,0,KC_A)});down(0);wake();
    mapping({KeymapKey(0,0,0,KC_A),KeymapKey(0,1,0,KC_A)});down(1);up(0);
    EXPECT_FALSE(layer_state_is(1));EXPECT_TRUE(is_key_pressed(KC_A));up(1);
}
TEST_F(EraWakeInput,RetainedLayerWakeReleasePreservesScopedDanceLayer) {
    mapping({KeymapKey(0,0,0,MO(1)),KeymapKey(0,1,0,TD(0))});dance(MO(1));
    down(0);down(1);idle_for(TAPPING_TERM+1);wake();up(0);
    EXPECT_TRUE(layer_state_is(1));up(1);EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraWakeInput,ExplicitLayerReplacementRetiresOldWakeRelease) {
    mapping({KeymapKey(0,0,0,MO(1)),KeymapKey(0,1,0,MO(1))});down(0);wake();
    layer_clear();down(1);up(0);EXPECT_TRUE(layer_state_is(1));up(1);
}
TEST_F(EraWakeInput,ExplicitLayerReplacementRetiresPostWakeLayerDispatch) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X)),KeymapKey(0,1,0,TD(0)),KeymapKey(1,1,0,MO(2)),
             KeymapKey(0,2,0,KC_A)});dance(KC_LCTL);down(0);down(1);wake();idle_for(TAPPING_TERM+1);
    ASSERT_TRUE(layer_state_is(2));layer_clear();
    mapping({KeymapKey(0,0,0,KC_NO),KeymapKey(0,1,0,KC_A),KeymapKey(0,2,0,KC_A)});
    down(2);up(1);EXPECT_TRUE(is_key_pressed(KC_A));up(2);up(0);
}
TEST_F(EraWakeInput,OrdinaryLayerOverlapKeepsQmkBitStateAcrossWake) {
    mapping({KeymapKey(0,0,0,MO(1)),KeymapKey(0,1,0,MO(1))});down(0);wake();down(1);
    up(0);EXPECT_FALSE(layer_state_is(1));up(1);
}
TEST_F(EraWakeInput,FreshSuppressedMomentaryLayerNeverOwnsRelease) {
    mapping({KeymapKey(0,0,0,MO(1)),KeymapKey(0,1,0,MO(1))});press_key(0,0);wake(true);
    down(1);up(0);EXPECT_TRUE(layer_state_is(1));up(1);
}
TEST_F(EraWakeInput,CompletedLayerToggleIsNotClearedByWake) {
    mapping({KeymapKey(0,0,0,TG(1)),KeymapKey(0,1,0,KC_NO)});down(0);up(0);
    ASSERT_TRUE(layer_state_is(1));down(1);wake();up(1);EXPECT_TRUE(layer_state_is(1));
}
TEST_F(EraWakeInput,RuntimeDisabledOneShotLayerReleasesAsMomentaryLayer) {
    const bool previous=keymap_config.oneshot_enable;keymap_config.oneshot_enable=false;
    mapping({KeymapKey(0,0,0,OSL(1))});down(0);idle_for(TAPPING_TERM+1);
    ASSERT_TRUE(layer_state_is(1));wake();up(0);EXPECT_FALSE(layer_state_is(1));
    keymap_config.oneshot_enable=previous;
}
TEST_F(EraWakeInput,HeldTapToggleReleasesItsOriginalInverseAfterWake) {
    mapping({KeymapKey(0,0,0,TT(1))});down(0);idle_for(TAPPING_TERM+1);
    ASSERT_TRUE(layer_state_is(1));wake();up(0);EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraWakeInput,HeldTapToggleRestoresPreviouslyEnabledLayerAfterWake) {
    mapping({KeymapKey(0,0,0,TT(1))});layer_on(1);down(0);idle_for(TAPPING_TERM+1);
    ASSERT_FALSE(layer_state_is(1));wake();up(0);EXPECT_TRUE(layer_state_is(1));
}
TEST_F(EraWakeInput,QueuedLayerModsReleaseNewModifiersAfterWake) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X)),KeymapKey(0,1,0,TD(0)),KeymapKey(1,1,0,LM(2,MOD_LCTL))});
    down(0);down(1);wake();idle_for(TAPPING_TERM+1);
    ASSERT_TRUE(layer_state_is(2));ASSERT_EQ(get_mods(),MOD_BIT(KC_LCTL));
    up(1);EXPECT_FALSE(layer_state_is(2));EXPECT_EQ(get_mods(),0);up(0);
}
TEST_F(EraWakeInput,LayerReplacementKeepsQueuedLayerModsModifierRelease) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X)),KeymapKey(0,1,0,TD(0)),KeymapKey(1,1,0,LM(2,MOD_LCTL)),
             KeymapKey(0,2,0,MO(2)),KeymapKey(0,3,0,KC_RCTL)});
    down(0);down(1);wake();idle_for(TAPPING_TERM+1);ASSERT_EQ(get_mods(),MOD_BIT(KC_LCTL));
    layer_move(3);down(2);down(3);up(1);
    EXPECT_TRUE(layer_state_is(2));EXPECT_TRUE(layer_state_is(3));EXPECT_EQ(get_mods(),MOD_BIT(KC_RCTL));
    up(3);up(2);up(0);
}
TEST_F(EraWakeInput,LayerReplacementBeforeWakeKeepsFreshModifierAfterOldUp) {
    mapping({KeymapKey(0,0,0,LM(1,MOD_LCTL)),KeymapKey(0,1,0,KC_LCTL)});
    down(0);layer_clear();wake();down(1);up(0);EXPECT_EQ(get_mods(),MOD_BIT(KC_LCTL));up(1);
}
TEST_F(EraWakeInput,OtherDanceAndFreshRegularOwnerSurviveWakeRelease) {
    mapping({KeymapKey(0,0,0,TD(0)),KeymapKey(0,1,0,TD(1)),KeymapKey(0,2,0,KC_A)});
    dance(KC_A);dance(KC_A,1);down(0);down(1);idle_for(TAPPING_TERM+1);wake();down(2);
    up(0);EXPECT_TRUE(is_key_pressed(KC_A));up(1);EXPECT_TRUE(is_key_pressed(KC_A));up(2);
    EXPECT_FALSE(is_key_pressed(KC_A));
}
TEST_F(EraWakeInput,WakeReleaseUsesOwnerAfterLiveRemap) {
    mapping({KeymapKey(0,0,0,TD(0)),KeymapKey(0,1,0,KC_A)});dance(KC_LCTL);
    down(0);idle_for(TAPPING_TERM+1);wake();mapping({KeymapKey(0,0,0,KC_A),KeymapKey(0,1,0,KC_A)});
    down(1);up(0);EXPECT_EQ(get_mods(),0);EXPECT_TRUE(is_key_pressed(KC_A));up(1);
}
TEST_F(EraWakeInput,CancelledDanceWakeReleaseDoesNotBecomeRemappedOrdinaryUp) {
    mapping({KeymapKey(0,0,0,TD(0)),KeymapKey(0,1,0,KC_A)});dance(KC_LCTL);
    down(0);idle_for(TAPPING_TERM+1);wake();clear_keyboard();
    mapping({KeymapKey(0,0,0,KC_A),KeymapKey(0,1,0,KC_A)});down(1);up(0);
    EXPECT_TRUE(is_key_pressed(KC_A));EXPECT_EQ(tap_dance_get_state(0),nullptr);up(1);
}
TEST_F(EraWakeInput,AlreadyClearedOrdinaryKeyUpPreservesFreshSameUsage) {
    mapping({KeymapKey(0,0,0,KC_A),KeymapKey(0,1,0,KC_A)});down(0);wake();
    ASSERT_FALSE(is_key_pressed(KC_A));down(1);up(0);EXPECT_TRUE(is_key_pressed(KC_A));up(1);
}
TEST_F(EraWakeInput,AlreadyClearedOrdinaryModifierUpPreservesFreshSameModifier) {
    mapping({KeymapKey(0,0,0,KC_LCTL),KeymapKey(0,1,0,KC_LCTL)});down(0);wake();
    ASSERT_EQ(get_mods(),0);down(1);up(0);EXPECT_EQ(get_mods(),MOD_BIT(KC_LCTL));up(1);
}
TEST_F(EraWakeInput,FreshWakeDownAndUpNeverCreateDance) {
    mapping({KeymapKey(0,0,0,TD(0))});dance(KC_A);press_key(0,0);wake(true);
    run_one_scan_loop();up(0);idle_for(TAPPING_TERM+1);
    EXPECT_FALSE(is_key_pressed(KC_A));EXPECT_EQ(tap_dance_get_state(0),nullptr);
    down(0);idle_for(TAPPING_TERM+1);EXPECT_TRUE(is_key_pressed(KC_A));up(0);
}
TEST_F(EraWakeInput,RejectedDanceDownDoesNotOwnWakeRelease) {
    mapping({KeymapKey(0,0,0,TD(0)),KeymapKey(0,1,0,KC_A)});reject_td_down=true;
    down(0);wake();reject_td_down=false;mapping({KeymapKey(0,0,0,KC_A),KeymapKey(0,1,0,KC_A)});
    down(1);up(0);EXPECT_TRUE(is_key_pressed(KC_A));up(1);
}
TEST_F(EraWakeInput,QueuedDanceDownAndWakeUpKeepOriginalShortTapTime) {
    mapping({KeymapKey(0,0,0,MT(MOD_LCTL,KC_X)),KeymapKey(0,1,0,TD(0))});dance(KC_A);
    down(0);down(1);ASSERT_EQ(tap_dance_get_state(0),nullptr);wake();up(1);
    idle_for(TAPPING_TERM*2+1);EXPECT_EQ(tap_dance_get_state(0),nullptr);
    EXPECT_TRUE(reported(KC_Z));EXPECT_FALSE(reported(KC_A));
    EXPECT_FALSE(is_key_pressed(KC_A));EXPECT_FALSE(is_key_pressed(KC_Z));
    release_key(0,0);run_one_scan_loop();
}
TEST_F(EraWakeInput,QueuedDanceCanResolveToOrdinaryOnSettledLayerAndRelease) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X)),KeymapKey(0,1,0,TD(0)),KeymapKey(1,1,0,KC_A)});
    dance(KC_LCTL);down(0);down(1);ASSERT_EQ(tap_dance_get_state(0),nullptr);wake();up(1);
    idle_for(TAPPING_TERM+1);EXPECT_FALSE(is_key_pressed(KC_A));EXPECT_EQ(tap_dance_get_state(0),nullptr);
    EXPECT_TRUE(reported(KC_A));
    release_key(0,0);run_one_scan_loop();layer_clear();
}
TEST_F(EraWakeInput,QueuedDanceSettlesOrdinaryBeforeCapturedPhysicalUp) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X)),KeymapKey(0,1,0,TD(0)),KeymapKey(1,1,0,KC_A)});
    dance(KC_LCTL);down(0);down(1);wake();idle_for(TAPPING_TERM+1);
    ASSERT_TRUE(is_key_pressed(KC_A));ASSERT_TRUE(reported(KC_A));ASSERT_EQ(tap_dance_get_state(0),nullptr);
    up(1);EXPECT_FALSE(is_key_pressed(KC_A));release_key(0,0);run_one_scan_loop();layer_clear();
}
TEST_F(EraWakeInput,QueuedSpaceCadetStillOwnsUpWhenItsModuleReturnsFalse) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X)),KeymapKey(0,1,0,TD(0)),KeymapKey(1,1,0,SC_LSPO)});
    down(0);down(1);wake();idle_for(TAPPING_TERM+1);
    ASSERT_EQ(get_mods(),MOD_BIT(KC_LSFT));up(1);EXPECT_EQ(get_mods(),0);
    release_key(0,0);run_one_scan_loop();layer_clear();
}
TEST_F(EraWakeInput,QueuedGraveEscapeStillOwnsUpWhenItsModuleReturnsFalse) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X)),KeymapKey(0,1,0,TD(0)),KeymapKey(1,1,0,QK_GRAVE_ESCAPE)});
    down(0);down(1);wake();idle_for(TAPPING_TERM+1);
    ASSERT_TRUE(is_key_pressed(KC_ESC));up(1);EXPECT_FALSE(is_key_pressed(KC_ESC));
    release_key(0,0);run_one_scan_loop();layer_clear();
}
TEST_F(EraWakeInput,SecondWakeClearsPriorOrdinaryDispatchQualification) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X)),KeymapKey(0,1,0,TD(0)),KeymapKey(1,1,0,KC_A),KeymapKey(1,2,0,KC_A)});
    down(0);down(1);wake();idle_for(TAPPING_TERM+1);ASSERT_TRUE(is_key_pressed(KC_A));
    wake();ASSERT_FALSE(is_key_pressed(KC_A));down(2);up(1);EXPECT_TRUE(is_key_pressed(KC_A));up(2);
    release_key(0,0);run_one_scan_loop();layer_clear();
}
TEST_F(EraWakeInput,PendingTapHoldDownDispatchedAfterWakeOwnsItsUp) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X))});down(0);wake();idle_for(TAPPING_TERM+1);
    ASSERT_TRUE(layer_state_is(1));up(0);EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraWakeInput,OverflowDropsPendingWakePairWithoutInventingOrdinaryUp) {
    mapping({KeymapKey(0,0,0,MT(MOD_LCTL,KC_X)),KeymapKey(0,1,0,TD(0)),KeymapKey(0,9,0,KC_A)});
    down(0);down(1);wake();for(uint8_t col=2;col<9;++col)down(col);
    mapping({KeymapKey(0,0,0,MT(MOD_LCTL,KC_X)),KeymapKey(0,1,0,KC_A),KeymapKey(0,9,0,KC_A)});
    down(9);up(1);EXPECT_TRUE(is_key_pressed(KC_A));up(9);
}
TEST_F(EraWakeInput,QueuedInvalidDanceCanResolveOrdinaryAndRelease) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X)),KeymapKey(0,1,0,TD(200)),KeymapKey(1,1,0,KC_A)});
    down(0);down(1);wake();up(1);idle_for(TAPPING_TERM+1);
    EXPECT_TRUE(reported(KC_A));EXPECT_FALSE(is_key_pressed(KC_A));
    release_key(0,0);run_one_scan_loop();layer_clear();
}
TEST_F(EraWakeInput,RetiredUnchangedInvalidDanceCanResolveOrdinaryAndRelease) {
    mapping({KeymapKey(0,0,0,LT(1,KC_X)),KeymapKey(0,1,0,TD(200)),KeymapKey(1,1,0,KC_A)});
    down(0);down(1);wake();clear_keyboard();up(1);idle_for(TAPPING_TERM+1);
    EXPECT_TRUE(reported(KC_A));EXPECT_FALSE(is_key_pressed(KC_A));
    release_key(0,0);run_one_scan_loop();layer_clear();
}
TEST_F(EraWakeInput,RetiredQueuedDanceDoesNotLeakIntoRemappedOrdinaryOutput) {
    mapping({KeymapKey(0,0,0,MT(MOD_LCTL,KC_X)),KeymapKey(0,1,0,TD(0)),KeymapKey(0,2,0,KC_A)});
    dance(KC_A);down(0);down(1);wake();clear_keyboard();
    mapping({KeymapKey(0,0,0,MT(MOD_LCTL,KC_X)),KeymapKey(0,1,0,KC_A),KeymapKey(0,2,0,KC_A)});
    down(2);up(1);idle_for(TAPPING_TERM+1);EXPECT_TRUE(is_key_pressed(KC_A));
    EXPECT_EQ(tap_dance_get_state(0),nullptr);up(2);release_key(0,0);run_one_scan_loop();
}
TEST_F(EraWakeInput,RetiredInvalidQueuedDanceDoesNotLeakIntoRemappedOrdinaryOutput) {
    mapping({KeymapKey(0,0,0,MT(MOD_LCTL,KC_X)),KeymapKey(0,1,0,TD(200)),KeymapKey(0,2,0,KC_A)});
    down(0);down(1);wake();clear_keyboard();
    mapping({KeymapKey(0,0,0,MT(MOD_LCTL,KC_X)),KeymapKey(0,1,0,KC_A),KeymapKey(0,2,0,KC_A)});
    down(2);up(1);idle_for(TAPPING_TERM+1);EXPECT_TRUE(is_key_pressed(KC_A));up(2);
    release_key(0,0);run_one_scan_loop();
}
#ifdef SWAP_HANDS_ENABLE
TEST_F(EraWakeInput,SwappedRetainedLayerUpIgnoresLiveRemap) {
    mapping({KeymapKey(0,1,0,MO(1)),KeymapKey(0,2,0,KC_A)});swap_hands_on();down(0);wake();
    swap_hands_off();mapping({KeymapKey(0,0,0,KC_A),KeymapKey(0,1,0,KC_A),KeymapKey(0,2,0,KC_A)});
    down(2);up(0);EXPECT_FALSE(layer_state_is(1));EXPECT_TRUE(is_key_pressed(KC_A));up(2);
}
TEST_F(EraWakeInput,OtherLogicalLayerReleaseCannotRetireRawWakeIdentity) {
    mapping({KeymapKey(0,1,0,MO(1)),KeymapKey(0,2,0,MO(1))});swap_hands_on();down(0);wake();
    swap_hands_off();down(1);up(1);ASSERT_FALSE(layer_state_is(1));down(2);ASSERT_TRUE(layer_state_is(1));
    up(0);EXPECT_FALSE(layer_state_is(1));up(2);
}
TEST_F(EraWakeInput,WakeReleaseUsesOriginalHandSwapMapping) {
    mapping({KeymapKey(0,0,0,KC_NO),KeymapKey(0,1,0,TD(0))});dance(KC_A);
    swap_hands_on();down(0);idle_for(TAPPING_TERM+1);wake();swap_hands_off();up(0);
    EXPECT_FALSE(is_key_pressed(KC_A));EXPECT_EQ(tap_dance_get_state(0),nullptr);
}
TEST_F(EraWakeInput,FreshUnswappedWakePositionCannotReleaseOtherSwappedOwner) {
    mapping({KeymapKey(0,0,0,KC_NO),KeymapKey(0,1,0,TD(0))});dance(KC_A);
    swap_hands_on();down(0);idle_for(TAPPING_TERM+1);swap_hands_off();press_key(1,0);
    wake(true);up(1);EXPECT_TRUE(is_key_pressed(KC_A));up(0);
    EXPECT_FALSE(is_key_pressed(KC_A));
}
TEST_F(EraWakeInput,QueuedSwappedOrdinaryDispatchUsesRawInputLifetime) {
    mapping({KeymapKey(0,1,0,TD(0)),KeymapKey(0,2,0,LT(1,KC_X)),KeymapKey(1,1,0,KC_A)});
    swap_hands_on();down(2);down(0);wake();idle_for(TAPPING_TERM+1);ASSERT_TRUE(is_key_pressed(KC_A));
    swap_hands_off();up(0);EXPECT_FALSE(is_key_pressed(KC_A));up(2);layer_clear();
}
TEST_F(EraWakeInput,RawWakeQualificationSurvivesAnotherLogicalPositionRelease) {
    mapping({KeymapKey(0,1,0,TD(0)),KeymapKey(0,2,0,LT(1,KC_X)),KeymapKey(1,1,0,KC_A),KeymapKey(0,3,0,KC_A)});
    swap_hands_on();down(2);down(0);wake();idle_for(TAPPING_TERM+1);ASSERT_TRUE(is_key_pressed(KC_A));
    swap_hands_off();down(1);up(1);ASSERT_FALSE(is_key_pressed(KC_A));
    down(3);ASSERT_TRUE(is_key_pressed(KC_A));up(0);EXPECT_FALSE(is_key_pressed(KC_A));
    up(3);up(2);layer_clear();
}
TEST_F(EraWakeInput,SyntheticTapReleaseKeepsOtherRawWakePositionBeforeTerm) {synthetic_release(false);}
TEST_F(EraWakeInput,SyntheticTapReleaseKeepsOtherRawWakePositionAfterTerm) {synthetic_release(true);}
#endif
