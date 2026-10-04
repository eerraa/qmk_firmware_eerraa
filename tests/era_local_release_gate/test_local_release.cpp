// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cstring>
#include <vector>
#include "test_common.hpp"
extern "C" {
#include "action_tapping.h"
#include "usb_device_state.h"
#include "keyboards/era/common/features/era_tapdance.h"
#include "keyboards/era/common/features/era_tapping.h"
#include "keyboards/era/common/system/era_hid_report_interval.h"
void gate_reset(bool left);
void gate_start_suspend(void);
void gate_stale_frames(void);
void gate_reopen(bool wake);
void gate_peer(bool enabled,bool closure);
unsigned gate_peer_syncs(void);
bool gate_bus_active(void);
bool era_hid_report_interval_select_format(uint8_t format) { (void)format;return false; }
}
namespace {
std::array<uint8_t,280> config;
struct Output {uint8_t lane;std::array<uint8_t,32> bytes;};
std::vector<Output> outputs;
uint8_t leds(void) {return 0;}
void keyboard_sink(report_keyboard_t *r) {ASSERT_TRUE(era_hid_report_interval_hold(0,r,sizeof(*r)));}
void nkro_sink(report_nkro_t *r) {ASSERT_TRUE(era_hid_report_interval_hold(1,r,sizeof(*r)));}
host_driver_t recording_driver={.keyboard_leds=leds,.send_keyboard=keyboard_sink,.send_nkro=nkro_sink};
}
extern "C" {
uint32_t era_eeprom_read_config(void *buf,uint32_t offset,uint32_t length) {
 if(offset>config.size() || length>config.size()-offset)return 0;
 memcpy(buf,config.data()+offset,length);return length;
}
uint32_t era_eeprom_update_config(const void *buf,uint32_t offset,uint32_t length) {
 if(offset>config.size() || length>config.size()-offset)return 0;
 memcpy(config.data()+offset,buf,length);return length;
}
void host_keyboard_delay(uint16_t ms) {era_hid_report_interval_request(ms,timer_read32());}
bool era_hid_report_interval_platform_post(uint8_t lane,const uint8_t *data,uint8_t size) {
 if(!gate_bus_active())return false;
 Output out{};out.lane=lane;memcpy(out.bytes.data(),data,size);outputs.push_back(out);
 era_hid_report_interval_note_posted(lane);
 era_hid_report_interval_note_completed(lane,timer_read32());
 return true;
}
}
class EraLocalReleaseGate : public TestFixture {
protected:
 void SetUp() override {
  config.fill(0);era_tapdance_init();era_tapping_init();
  era_tapping_set_hold_on_other_key_press(false);era_tapping_set_permissive_hold(false);
  era_tapping_set_retro_tapping(false);era_hid_report_interval_init(1);
  usb_device_state_set_protocol(USB_PROTOCOL_REPORT);keymap_config.nkro=true;
  host_set_driver(&recording_driver);gate_reset(true);outputs.clear();
  for(uint8_t row=0;row<MATRIX_ROWS;++row)for(uint8_t col=0;col<MATRIX_COLS;++col)
   update_source_layers_cache((keypos_t){.col=col,.row=row},0);
 }
 void TearDown() override {
  gate_reopen(false);clear_all_keys();idle_for(TAPPING_TERM+1);
  tap_dance_cancel_all();layer_clear();default_layer_set(1);clear_keyboard();
  keymap_config.nkro=false;host_set_driver(nullptr);
 }
 void mapping(std::initializer_list<KeymapKey> keys) {
  set_keymap(keys);
  for(uint8_t layer=0;layer<4;++layer)for(uint8_t row=0;row<MATRIX_ROWS;++row)
   for(uint8_t col=0;col<MATRIX_COLS;++col)
    if(!find_key(layer,(keypos_t){.col=col,.row=row}))add_key(KeymapKey(layer,col,row,layer?KC_TRNS:KC_NO));
 }
 void press(uint8_t row,uint8_t col) {press_key(col,row);run_one_scan_loop();}
 void release(uint8_t row,uint8_t col) {release_key(col,row);run_one_scan_loop();}
 void close_suspend(void) {
  gate_start_suspend();ASSERT_TRUE(is_keyboard_master());idle_for(501);ASSERT_FALSE(is_keyboard_master());
 }
 void hold(uint16_t code,uint8_t row=0) {
  mapping({KeymapKey(0,0,row,code)});press(row,0);idle_for(TAPPING_TERM+1);
 }
 void hold_dance(uint16_t code) {
  ASSERT_TRUE(era_tapdance_set_mode(0,0));
  ASSERT_TRUE(era_tapdance_set_action(0,0,KC_Z));ASSERT_TRUE(era_tapdance_set_action(0,1,code));
  ASSERT_TRUE(era_tapdance_set_action(0,2,KC_NO));ASSERT_TRUE(era_tapdance_set_action(0,3,KC_NO));
  hold(TD(0));ASSERT_NE(tap_dance_get_state(0),nullptr);
 }
 bool last_has_key(uint8_t key) {
  if(outputs.empty())return false;
  const Output &out=outputs.back();
  if(out.lane==1)return out.bytes[2+(key>>3)]&(1U<<(key&7));
  for(unsigned i=2;i<8;++i)if(out.bytes[i]==key)return true;
  return false;
 }
};
TEST_F(EraLocalReleaseGate, MomentaryLayerReleaseAfterSuspendGrace) {
 hold(MO(1));ASSERT_TRUE(layer_state_is(1));close_suspend();release(0,0);
 EXPECT_FALSE(layer_state_is(1));gate_reopen(true);idle_for(1);EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraLocalReleaseGate, LayerModifierReleaseAfterSuspendGrace) {
 hold(LM(1,MOD_LCTL));ASSERT_TRUE(layer_state_is(1));ASSERT_NE(get_mods()&MOD_BIT(KC_LCTL),0);
 close_suspend();release(0,0);EXPECT_FALSE(layer_state_is(1));EXPECT_EQ(get_mods(),0);
 gate_reopen(true);idle_for(1);EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraLocalReleaseGate, LayerTapHoldReleaseAfterSuspendGrace) {
 hold(LT(1,KC_X));ASSERT_TRUE(layer_state_is(1));close_suspend();release(0,0);
 EXPECT_FALSE(layer_state_is(1));gate_reopen(true);idle_for(1);EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraLocalReleaseGate, ModifierTapHoldReleaseAfterSuspendGrace) {
 hold(MT(MOD_LCTL,KC_X));ASSERT_NE(get_mods()&MOD_BIT(KC_LCTL),0);
 close_suspend();release(0,0);EXPECT_EQ(get_mods(),0);gate_reopen(true);idle_for(1);EXPECT_EQ(get_mods(),0);
}
TEST_F(EraLocalReleaseGate, TapDanceLayerOwnerReleaseSurvivesWakeClear) {
 hold_dance(MO(1));ASSERT_TRUE(layer_state_is(1));close_suspend();release(0,0);
 EXPECT_FALSE(layer_state_is(1));EXPECT_EQ(tap_dance_get_state(0),nullptr);
 gate_reopen(true);idle_for(1);EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraLocalReleaseGate, TapDanceModifierOwnerReleaseSurvivesWakeClear) {
 hold_dance(KC_LCTL);ASSERT_NE(get_mods()&MOD_BIT(KC_LCTL),0);close_suspend();release(0,0);
 EXPECT_EQ(get_mods(),0);EXPECT_EQ(tap_dance_get_state(0),nullptr);
 gate_reopen(true);idle_for(1);EXPECT_EQ(get_mods(),0);
}
TEST_F(EraLocalReleaseGate, BasicUsageReleaseAfterFrameLossReopen) {
 hold(KC_A);ASSERT_TRUE(last_has_key(KC_A));gate_stale_frames();ASSERT_FALSE(is_keyboard_master());
 release(0,0);gate_reopen(false);idle_for(1);EXPECT_FALSE(last_has_key(KC_A));
}
TEST_F(EraLocalReleaseGate, RightPhysicalHalfOwnsClosedRelease) {
 gate_reset(false);hold(MO(1),6);ASSERT_TRUE(layer_state_is(1));gate_stale_frames();
 release(6,0);EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraLocalReleaseGate, HeldInputKeepsOwnerAcrossClosedAndOpen) {
 hold(MO(1));gate_stale_frames();idle_for(3);EXPECT_TRUE(layer_state_is(1));
 gate_reopen(false);idle_for(1);EXPECT_TRUE(layer_state_is(1));release(0,0);EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraLocalReleaseGate, FreshClosedPressAndReleaseNeverCreateAction) {
 mapping({KeymapKey(0,0,0,MO(1)),KeymapKey(0,1,0,TD(0))});gate_stale_frames();
 press(0,0);press(0,1);release(0,0);release(0,1);idle_for(TAPPING_TERM+1);
 EXPECT_FALSE(layer_state_is(1));EXPECT_EQ(tap_dance_get_state(0),nullptr);
 gate_reopen(false);idle_for(1);EXPECT_FALSE(layer_state_is(1));EXPECT_EQ(tap_dance_get_state(0),nullptr);
}
TEST_F(EraLocalReleaseGate, FreshClosedDownIsConsumedAcrossReopen) {
 mapping({KeymapKey(0,0,0,MO(1))});layer_on(1);gate_stale_frames();press(0,0);gate_reopen(false);idle_for(1);
 EXPECT_TRUE(layer_state_is(1));release(0,0);EXPECT_TRUE(layer_state_is(1));
}
TEST_F(EraLocalReleaseGate, FreshClosedTdReleaseCannotCreateSeatAfterReopen) {
 mapping({KeymapKey(0,0,0,TD(0))});gate_stale_frames();press(0,0);gate_reopen(false);idle_for(1);
 uint32_t epoch=tap_dance_input_epoch();release(0,0);idle_for(TAPPING_TERM+1);
 EXPECT_EQ(tap_dance_input_epoch(),epoch);EXPECT_EQ(tap_dance_get_state(0),nullptr);
 EXPECT_EQ(get_mods(),0);EXPECT_FALSE(layer_state_is(1));
}
TEST_F(EraLocalReleaseGate, ReleaseThenRepressClosedRetiresOnlyOriginalDown) {
 hold(MO(1));gate_stale_frames();release(0,0);EXPECT_FALSE(layer_state_is(1));
 press(0,0);gate_reopen(false);idle_for(1);EXPECT_FALSE(layer_state_is(1));release(0,0);
 EXPECT_FALSE(layer_state_is(1));press(0,0);EXPECT_TRUE(layer_state_is(1));release(0,0);
}
TEST_F(EraLocalReleaseGate, ClosedUnadmittedReleaseCannotSwitchOffPersistentLayer) {
 mapping({KeymapKey(0,0,0,MO(1))});layer_on(1);gate_stale_frames();press(0,0);release(0,0);
 EXPECT_TRUE(layer_state_is(1));gate_reopen(false);idle_for(1);EXPECT_TRUE(layer_state_is(1));
}
TEST_F(EraLocalReleaseGate, MomentaryReleasePreservesToggleAndDefaultLayer) {
 mapping({KeymapKey(0,0,0,MO(1)),KeymapKey(0,1,0,TG(2))});press(0,1);release(0,1);
 ASSERT_TRUE(layer_state_is(2));default_layer_set((layer_state_t)1<<3);press(0,0);
 ASSERT_TRUE(layer_state_is(1));gate_stale_frames();release(0,0);
 EXPECT_FALSE(layer_state_is(1));EXPECT_TRUE(layer_state_is(2));EXPECT_EQ(default_layer_state,(layer_state_t)1<<3);
 gate_reopen(true);idle_for(1);EXPECT_TRUE(layer_state_is(2));EXPECT_EQ(default_layer_state,(layer_state_t)1<<3);
}
TEST_F(EraLocalReleaseGate, PendingTappingReleaseUsesOriginalAdmittedDown) {
 mapping({KeymapKey(0,0,0,MT(MOD_LCTL,KC_X)),KeymapKey(0,1,0,MO(1))});
 press(0,0);press(0,1);gate_stale_frames();release(0,1);release(0,0);idle_for(TAPPING_TERM+1);
 EXPECT_EQ(get_mods(),0);EXPECT_FALSE(layer_state_is(1));gate_reopen(false);idle_for(TAPPING_TERM+1);
 EXPECT_EQ(get_mods(),0);EXPECT_FALSE(layer_state_is(1));EXPECT_FALSE(last_has_key(KC_X));
}
TEST_F(EraLocalReleaseGate, QueuedTapDanceDownAndReleaseKeepSeatIdentity) {
 mapping({KeymapKey(0,0,0,MT(MOD_LCTL,KC_X)),KeymapKey(0,1,0,TD(0))});
 ASSERT_TRUE(era_tapdance_set_action(0,0,KC_Z));ASSERT_TRUE(era_tapdance_set_action(0,1,MO(1)));
 press(0,0);press(0,1);uint32_t epoch=tap_dance_input_epoch();gate_stale_frames();
 release(0,1);release(0,0);idle_for(TAPPING_TERM*2+1);EXPECT_EQ(tap_dance_input_epoch(),epoch);
 EXPECT_EQ(get_mods(),0);EXPECT_FALSE(layer_state_is(1));EXPECT_EQ(tap_dance_get_state(0),nullptr);
 gate_reopen(false);idle_for(TAPPING_TERM+1);EXPECT_FALSE(layer_state_is(1));EXPECT_FALSE(last_has_key(KC_X));
}
TEST_F(EraLocalReleaseGate, RemoteClosureWaitsForWholeGroupAdmissionAtReopen) {
 ASSERT_EQ(get_mods(),0);ASSERT_FALSE(layer_state_is(1));ASSERT_EQ(action_tapping_remote_room(),66);
 mapping({KeymapKey(0,0,0,MT(MOD_LCTL,KC_X)),KeymapKey(0,0,6,KC_A)});gate_peer(true,false);
 press(0,0);press(6,0);EXPECT_EQ(action_tapping_remote_room(),65);
 gate_stale_frames();gate_peer(false,true);release(0,0);idle_for(TAPPING_TERM+1);
 EXPECT_EQ(gate_peer_syncs(),0U);gate_reopen(false);idle_for(TAPPING_TERM+1);
 EXPECT_EQ(gate_peer_syncs(),1U);EXPECT_EQ(action_tapping_remote_room(),66);EXPECT_EQ(get_mods(),0);
 EXPECT_FALSE(last_has_key(KC_A));EXPECT_FALSE(last_has_key(KC_X));
}
