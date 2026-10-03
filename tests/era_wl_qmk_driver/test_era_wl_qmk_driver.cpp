#include "gtest/gtest.h"
#include "quantum/wear_leveling/tests/backing_mocks.hpp"
extern "C" {
#include "eeprom.h"
#include "eeprom_driver.h"
#include "nvm_dynamic_keymap.h"
#include "keyboards/era/common/system/era_state_sync.h"
void raw_hid_send(uint8_t *, uint8_t) {}
}
#include <array>

class EraWlQmkDriver : public ::testing::Test {
protected:
    void SetUp() override { MockBackingStore::Instance().reset_instance(); eeprom_driver_init(); }
};

TEST_F(EraWlQmkDriver, FailedPacketRetainsEveryAcceptedByteUntilDurableRepair) {
    auto &flash = MockBackingStore::Instance();
    std::array<uint8_t,28> chunk;
    chunk.fill(0x5A);
    uint8_t opener = 0xFF, close = 0, visible;
    nvm_dynamic_keymap_macro_update_buffer(3294,1,&opener);
    auto before = flash.write_invoke_count();
    nvm_dynamic_keymap_macro_update_buffer(0,chunk.size(),chunk.data());
    auto programs = flash.write_invoke_count() - before;
    for (uint64_t boundary=1; boundary<=programs; ++boundary) {
        SetUp();
        nvm_dynamic_keymap_macro_update_buffer(3294,1,&opener);
        auto fail = flash.write_invoke_count() + boundary;
        flash.set_write_callback([fail](uint64_t count, uint32_t) { return count != fail; });
        nvm_dynamic_keymap_macro_update_buffer(0,chunk.size(),chunk.data());
        ASSERT_FALSE(eeprom_persistence_ok()) << boundary;
        std::array<uint8_t,28> desired;
        nvm_dynamic_keymap_macro_read_buffer(0,desired.size(),desired.data());
        EXPECT_EQ(desired,chunk) << boundary;
        nvm_dynamic_keymap_macro_read_buffer(3294,1,&visible);
        EXPECT_NE(visible,0);
        flash.set_write_callback([](uint64_t,uint32_t) { return true; });
        nvm_dynamic_keymap_macro_update_buffer(3294,1,&close);
        ASSERT_TRUE(eeprom_persistence_ok());
        eeprom_driver_init();
        nvm_dynamic_keymap_macro_read_buffer(0,desired.size(),desired.data());
        EXPECT_EQ(desired,chunk);
        nvm_dynamic_keymap_macro_read_buffer(3294,1,&visible);
        EXPECT_EQ(visible,0);
    }
    printf("FAULT wear-level macro packet physical-write boundaries=%llu\n",(unsigned long long)programs);
}

TEST_F(EraWlQmkDriver, FailedResetRetainsTheWholeZeroImageForSameValueRetry) {
    uint8_t old = 0x35, close = 0;
    nvm_dynamic_keymap_macro_update_buffer(1000,1,&old);
    auto &flash=MockBackingStore::Instance();
    flash.set_write_callback([](uint64_t,uint32_t) { return false; });
    nvm_dynamic_keymap_macro_reset();
    ASSERT_FALSE(eeprom_persistence_ok());
    flash.set_write_callback([](uint64_t,uint32_t) { return true; });
    nvm_dynamic_keymap_macro_update_buffer(3294,1,&close);
    ASSERT_TRUE(eeprom_persistence_ok());
    eeprom_driver_init();
    std::array<uint8_t,3295> actual;
    nvm_dynamic_keymap_macro_read_buffer(0,actual.size(),actual.data());
    for(auto b:actual) EXPECT_EQ(b,0);
}

TEST_F(EraWlQmkDriver, RepairPublishesAllRecoveredDomainsAcrossMacroEnvelope) {
    auto &flash = MockBackingStore::Instance();
    era_state_sync_set_revisions_for_testing(1,1,1);
    flash.set_write_callback([](uint64_t,uint32_t) { return false; });
    nvm_dynamic_keymap_update_keycode(0,0,0,0x1234);
    uint8_t opener=0xFF;
    nvm_dynamic_keymap_macro_update_buffer(3294,1,&opener);
    EXPECT_FALSE(eeprom_persistence_ok());
    EXPECT_EQ(era_state_sync_keymap_revision(),1U);
    EXPECT_EQ(era_state_sync_macro_revision(),1U);
    flash.set_write_callback([](uint64_t,uint32_t) { return true; });
    eeprom_update_byte((uint8_t *)4,0x12);
    ASSERT_TRUE(eeprom_persistence_ok());
    EXPECT_GT(era_state_sync_keymap_revision(),1U);
    EXPECT_EQ(era_state_sync_macro_revision(),1U);
    EXPECT_GT(era_state_sync_config_revision(),1U);
    eeprom_driver_init();
    EXPECT_EQ(nvm_dynamic_keymap_read_keycode(0,0,0),0x1234);
}
