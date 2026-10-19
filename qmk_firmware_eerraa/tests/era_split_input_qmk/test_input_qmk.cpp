// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <vector>
#include <cstring>
#include "test_common.hpp"
extern "C" {
#include "action_tapping.h"
#include "usb_device_state.h"
#include "keyboards/era/common/features/era_tapdance.h"
#include "keyboards/era/common/features/era_tapping.h"
#include "keyboards/era/common/system/era_hid_report_interval.h"
}
namespace {
std::array<uint8_t,280> config;
struct Output { uint8_t lane,size; std::array<uint8_t,32> bytes; };
std::vector<Output> outputs,expected;
bool blocked=false;
unsigned generated=0;
uint8_t leds(void) { return 0; }
void capture(uint8_t lane,const void *report,uint8_t size) {
    ++generated; Output out{}; out.lane=lane; out.size=size; memcpy(out.bytes.data(),report,size); expected.push_back(out);
    ASSERT_TRUE(era_hid_report_interval_hold(lane,report,size));
}
void keyboard_sink(report_keyboard_t *r) { capture(0,r,sizeof(*r)); }
void nkro_sink(report_nkro_t *r) { capture(1,r,sizeof(*r)); }
host_driver_t recording_driver={.keyboard_leds=leds,.send_keyboard=keyboard_sink,.send_nkro=nkro_sink};
}
extern "C" {
uint32_t era_eeprom_read_config(void *buf,uint32_t offset,uint32_t length) {
    if(offset>config.size() || length>config.size()-offset) return 0;
    memcpy(buf,config.data()+offset,length); return length;
}
uint32_t era_eeprom_update_config(const void *buf,uint32_t offset,uint32_t length) {
    if(offset>config.size() || length>config.size()-offset) return 0;
    memcpy(config.data()+offset,buf,length); return length;
}
void host_keyboard_delay(uint16_t ms) { era_hid_report_interval_request(ms,timer_read32()); }
bool era_hid_report_interval_platform_post(uint8_t lane,const uint8_t *data,uint8_t size) {
    if(blocked) return false;
    Output out{}; out.lane=lane; out.size=size; memcpy(out.bytes.data(),data,size); outputs.push_back(out);
    era_hid_report_interval_note_posted(lane); return true;
}
// Real transport claim is tested separately; this physical-matrix fixture
// supplies its HP origin boundary before action_exec's swap-hand processing.
bool era_split_input_event_remote(uint8_t row) { return row>=6 && row<12; }
void era_split_input_action_begin(bool action_open) { (void)action_open; }
void era_split_input_action_end(void) {}
}
class EraInputQmk : public TestFixture {
protected:
 void SetUp() override {
    config.fill(0); era_tapdance_init(); era_tapping_init(); era_tapping_set_hold_on_other_key_press(false);
    era_tapping_set_permissive_hold(false); era_tapping_set_retro_tapping(false);
    era_hid_report_interval_init(1); generated=0; outputs.clear(); expected.clear(); blocked=false;
    usb_device_state_set_protocol(USB_PROTOCOL_REPORT); keymap_config.nkro=true;
    host_set_driver(&recording_driver);
    for(uint8_t r=0;r<12;++r)for(uint8_t c=0;c<11;++c)update_source_layers_cache((keypos_t){.col=c,.row=r},0);
 }
 void TearDown() override { keymap_config.nkro=false; host_set_driver(nullptr); }
 void mapping(uint16_t tap_hold) {
    set_keymap({KeymapKey(0,0,0,tap_hold),KeymapKey(0,1,0,TD(0)),KeymapKey(0,2,0,KC_LSFT)});
    for(uint8_t c=0;c<11;++c)for(uint8_t r=6;r<12;++r)add_key(KeymapKey(0,c,r,KC_A+(r-6)*11+c));
 }
 void event(uint8_t row,uint8_t col,bool down) {
    keyevent_t e{}; e.key.row=row; e.key.col=col; e.pressed=down; e.time=timer_read32(); e.type=KEY_EVENT; action_exec(e);
 }
 void dance_double(uint8_t row=0,uint8_t col=1) {
    ASSERT_TRUE(era_tapdance_set_mode(0,0)); ASSERT_TRUE(era_tapdance_set_action(0,0,KC_Z));
    ASSERT_TRUE(era_tapdance_set_action(0,1,MO(1))); ASSERT_TRUE(era_tapdance_set_action(0,2,KC_NO));
    ASSERT_TRUE(era_tapdance_set_action(0,3,KC_NO));
    tap_key(KeymapKey(0,col,row,TD(0))); tap_key(KeymapKey(0,col,row,TD(0)));
    idle_for(TAPPING_TERM+1);
 }
 era_hid_report_interval_diagnostics_t diag(void) { era_hid_report_interval_diagnostics_t d{};era_hid_report_interval_get_diagnostics(&d);return d; }
 void drain(void) {
    blocked=false;
    uint32_t now=timer_read32();
    size_t completed=0;
    for(unsigned pass=0;pass<100 && era_hid_report_interval_active();++pass) {
        // Complete already accepted reports before advancing the gate clock.
        for(; completed<outputs.size(); ++completed) era_hid_report_interval_note_completed(outputs[completed].lane,now);
        now+=100;
        era_hid_report_interval_task(now);
    }
    ASSERT_EQ(outputs.size(),expected.size());
    for(size_t i=0;i<expected.size();++i) {
        EXPECT_EQ(outputs[i].lane,expected[i].lane); EXPECT_EQ(outputs[i].size,expected[i].size);
        EXPECT_EQ(outputs[i].bytes,expected[i].bytes) << "report " << i;
    }
    EXPECT_EQ(diag().valve_trips,0U);
 }
};
TEST_F(EraInputQmk, LocalMtAndLtAllowEightRemotePressesWithoutClearAll) {
    for(uint16_t hold:{static_cast<uint16_t>(MT(MOD_LCTL,KC_X)),static_cast<uint16_t>(LT(1,KC_X))}) {
        mapping(hold); blocked=true; generated=0; era_hid_report_interval_init(1);
        event(0,0,true);
        for(uint8_t c=0;c<8;++c)event(6,c,true);
        EXPECT_EQ(action_tapping_remote_room(),58U);
        event(0,0,false);
        EXPECT_EQ(action_tapping_remote_room(),66U);
        for(uint8_t c=0;c<8;++c)event(6,c,false);
        EXPECT_EQ(generated,18U); EXPECT_EQ(diag().backlog,18U); EXPECT_EQ(diag().coalesced,0U);
        drain(); EXPECT_EQ(outputs.size(),18U); outputs.clear(); expected.clear();
    }
}
TEST_F(EraInputQmk, WholeFullHas66RemoteSeatsAndRetainsBothOriginQuotas) {
    mapping(MT(MOD_LCTL,KC_X)); blocked=true;
    event(0,0,true);
    for(uint8_t r=6;r<12;++r)for(uint8_t c=0;c<11;++c)event(r,c,true);
    EXPECT_EQ(action_tapping_remote_room(),0U);
    event(0,0,false); EXPECT_EQ(action_tapping_remote_room(),66U);
    for(uint8_t r=6;r<12;++r)for(uint8_t c=0;c<11;++c)event(r,c,false);
    EXPECT_EQ(get_mods(),0U); // no quota overflow clear-all
    // Full66 is deliberately outside the finite HID preservation profile.
    EXPECT_GT(diag().coalesced,0U);
}
TEST_F(EraInputQmk, FiniteMixed25ProfileIsCountedThroughRealQmkAndTdOutputs) {
    mapping(MT(MOD_LCTL,KC_X));
    tap_code(KC_CAPS); // down posted, width anchored, release queued
    ASSERT_EQ(outputs.size(),1U); ASSERT_EQ(diag().backlog,1U);
    blocked=true;
    event(0,0,true); for(uint8_t c=0;c<8;++c)event(6,c,true); event(0,0,false);
    for(uint8_t c=0;c<8;++c)event(6,c,false);
    dance_double(); event(0,2,true); event(0,2,false);
    EXPECT_EQ(generated,26U); EXPECT_EQ(diag().backlog,25U); EXPECT_EQ(diag().coalesced,0U);
    drain(); EXPECT_EQ(outputs.size(),26U); EXPECT_FALSE(era_hid_report_interval_active());
    printf("production profile: 25 queued reports; keyrecord=%zu; waiting=%u physical seats\n",sizeof(keyrecord_t),74U);
}
TEST_F(EraInputQmk, Smaller15And17ProfilesFitTheSamePrefix) {
    mapping(MT(MOD_LCTL,KC_X));
    for(unsigned target:{15U,17U}) {
        era_hid_report_interval_init(1); outputs.clear(); expected.clear(); generated=0; blocked=false;
        tap_code(KC_CAPS); blocked=true;
        for(unsigned i=0;i<4;++i)tap_code(KC_A+i);
        dance_double();
        if(target==15) { event(0,2,true);event(0,2,false); }
        else dance_double();
        EXPECT_EQ(diag().backlog,target); EXPECT_EQ(generated,target+1); EXPECT_EQ(diag().coalesced,0U);
        drain(); EXPECT_EQ(outputs.size(),target+1);
    }
}

TEST_F(EraInputQmk, RemoteSelfReleaseAndDanceKeepLocalModifierAndAllIntermediateReports) {
    set_keymap({KeymapKey(0,2,0,KC_LSFT),KeymapKey(0,0,6,MT(MOD_LCTL,KC_X)),KeymapKey(0,1,6,TD(0))});
    blocked=true;
    event(0,2,true); event(6,0,true); event(6,0,false);
    dance_double(6,1); event(0,2,false);
    EXPECT_EQ(action_tapping_remote_room(),66U); EXPECT_EQ(get_mods(),0U);
    bool saw_x=false,saw_z=false;
    for(const auto &out:expected) {
        ASSERT_EQ(out.lane,1U); report_nkro_t r{}; memcpy(&r,out.bytes.data(),out.size);
        if(r.bits[KC_X/8] & (1U<<(KC_X%8))) {saw_x=true;EXPECT_NE(r.mods&MOD_BIT(KC_LSFT),0U);}
        if(r.bits[KC_Z/8] & (1U<<(KC_Z%8))) {saw_z=true;EXPECT_NE(r.mods&MOD_BIT(KC_LSFT),0U);}
    }
    EXPECT_TRUE(saw_x); EXPECT_TRUE(saw_z); EXPECT_EQ(diag().coalesced,0U); drain();
}
