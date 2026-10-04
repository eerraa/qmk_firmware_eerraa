#include "gtest/gtest.h"
#include "quantum/wear_leveling/tests/backing_mocks.hpp"
extern "C" {
#include "eeprom.h"
#include "eeprom_driver.h"
#include "nvm_dynamic_keymap.h"
#include "via.h"
void raw_hid_receive(uint8_t *data, uint8_t length);
#include "keyboards/era/common/system/era_state_sync.h"
static uint8_t g_response_command;
void raw_hid_send(uint8_t *data, uint8_t) { g_response_command=data[0]; }
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
    ASSERT_GT(programs,0U);
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
    uint8_t old = 0x35, opener = 0xFF, close = 0;
    nvm_dynamic_keymap_macro_update_buffer(3294,1,&opener);
    nvm_dynamic_keymap_macro_update_buffer(1000,1,&old);
    nvm_dynamic_keymap_macro_update_buffer(3294,1,&close);
    ASSERT_TRUE(eeprom_persistence_ok());
    eeprom_driver_init();
    uint8_t baseline;
    nvm_dynamic_keymap_macro_read_buffer(1000,1,&baseline);
    ASSERT_EQ(baseline,old);
    auto &flash=MockBackingStore::Instance();
    flash.set_write_callback([](uint64_t,uint32_t) { return false; });
    nvm_dynamic_keymap_macro_reset();
    ASSERT_FALSE(eeprom_persistence_ok());
    flash.set_write_callback([](uint64_t,uint32_t) { return true; });
    std::array<uint8_t,32> close_packet{};
    close_packet[0]=id_dynamic_keymap_macro_set_buffer;
    close_packet[1]=3294>>8;
    close_packet[2]=3294&255;
    close_packet[3]=1;
    raw_hid_receive(close_packet.data(),close_packet.size());
    ASSERT_EQ(g_response_command,id_unhandled);
    ASSERT_FALSE(eeprom_persistence_ok());
    // Retry the approved full RESET; CLOSE is not proof of that empty image.
    nvm_dynamic_keymap_macro_reset();
    ASSERT_TRUE(eeprom_persistence_ok());
    eeprom_driver_init();
    std::array<uint8_t,3295> actual;
    nvm_dynamic_keymap_macro_read_buffer(0,actual.size(),actual.data());
    for(auto b:actual) EXPECT_EQ(b,0);
}

TEST_F(EraWlQmkDriver, AnotherOwnerRepairOfFailedCloseSealsFurtherPayload) {
    auto &flash=MockBackingStore::Instance();
    uint8_t opener=0xFF, close=0;
    std::array<uint8_t,2> accepted{{65,0}}, replacement{{66,0}}, actual;
    nvm_dynamic_keymap_macro_update_buffer(3294,1,&opener);
    nvm_dynamic_keymap_macro_update_buffer(0,accepted.size(),accepted.data());
    flash.set_write_callback([](uint64_t,uint32_t) { return false; });
    nvm_dynamic_keymap_macro_update_buffer(3294,1,&close);
    ASSERT_FALSE(eeprom_persistence_ok());
    flash.set_write_callback([](uint64_t,uint32_t) { return true; });
    eeprom_update_byte((uint8_t *)4,0x12);
    ASSERT_TRUE(eeprom_persistence_ok());
    auto revision=era_state_sync_macro_revision();
    auto before=flash.write_invoke_count();
    nvm_dynamic_keymap_macro_update_buffer(0,replacement.size(),replacement.data());
    EXPECT_FALSE(eeprom_persistence_ok());
    EXPECT_EQ(flash.write_invoke_count(),before);
    EXPECT_EQ(era_state_sync_macro_revision(),revision + 1);
    eeprom_driver_init();
    nvm_dynamic_keymap_macro_read_buffer(0,actual.size(),actual.data());
    EXPECT_EQ(actual,accepted);
    uint8_t marker;
    nvm_dynamic_keymap_macro_read_buffer(3294,1,&marker);
    EXPECT_EQ(marker,0);
}

TEST_F(EraWlQmkDriver, CombinedOpenerRetiresExecutionBeforeAnyPayloadProgram) {
    auto &flash=MockBackingStore::Instance();
    std::array<uint8_t,3> packet{{77,78,0xFF}};
    auto seed = [&]() { SetUp(); nvm_dynamic_keymap_macro_reset(); ASSERT_TRUE(eeprom_persistence_ok()); };
    seed();
    auto before=flash.write_invoke_count();
    nvm_dynamic_keymap_macro_update_buffer(3292,packet.size(),packet.data());
    ASSERT_TRUE(eeprom_persistence_ok());
    auto programs=flash.write_invoke_count()-before;
    ASSERT_GT(programs,0U);
    for(uint64_t cut=1;cut<=programs;++cut) {
        seed();
        auto stop=flash.write_invoke_count()+cut;
        flash.set_write_callback([stop](uint64_t count,uint32_t) { return count<stop; });
        nvm_dynamic_keymap_macro_update_buffer(3292,packet.size(),packet.data());
        ASSERT_FALSE(eeprom_persistence_ok()) << cut;
        flash.set_write_callback([](uint64_t,uint32_t) { return true; });
        eeprom_driver_init();
        std::array<uint8_t,3> cold;
        nvm_dynamic_keymap_macro_read_buffer(3292,cold.size(),cold.data());
        if(cold[2]==0) { EXPECT_EQ(cold[0],0) << cut; EXPECT_EQ(cold[1],0) << cut; }
    }
    printf("POWER CUT combined wear-level opener boundaries=%llu\n",(unsigned long long)programs);
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
    EXPECT_EQ(era_state_sync_macro_revision(),2U);
    flash.set_write_callback([](uint64_t,uint32_t) { return true; });
    eeprom_update_byte((uint8_t *)4,0x12);
    ASSERT_TRUE(eeprom_persistence_ok());
    EXPECT_GT(era_state_sync_keymap_revision(),1U);
    EXPECT_EQ(era_state_sync_macro_revision(),2U);
    EXPECT_GT(era_state_sync_config_revision(),1U);
    eeprom_driver_init();
    EXPECT_EQ(nvm_dynamic_keymap_read_keycode(0,0,0),0x1234);
}

TEST_F(EraWlQmkDriver, ColdMountAfterEveryPacketCutKeepsMacroUncommitted) {
    auto &flash = MockBackingStore::Instance();
    std::array<uint8_t,28> chunk;
    chunk.fill(0x5A);
    uint8_t opener=0xFF, close=0, marker;
    nvm_dynamic_keymap_macro_update_buffer(3294,1,&opener);
    auto before=flash.write_invoke_count();
    nvm_dynamic_keymap_macro_update_buffer(0,chunk.size(),chunk.data());
    auto programs=flash.write_invoke_count()-before;
    ASSERT_GT(programs,0U);
    for (uint64_t cut=1; cut<=programs; ++cut) {
        SetUp();
        nvm_dynamic_keymap_update_keycode(0,0,0,0x1234);
        nvm_dynamic_keymap_macro_update_buffer(3294,1,&opener);
        auto stop=flash.write_invoke_count()+cut;
        flash.set_write_callback([stop](uint64_t count,uint32_t) { return count<stop; });
        nvm_dynamic_keymap_macro_update_buffer(0,chunk.size(),chunk.data());
        ASSERT_FALSE(eeprom_persistence_ok()) << cut;
        flash.set_write_callback([](uint64_t,uint32_t) { return true; });
        eeprom_driver_init(); // discard the desired cache before any repair
        ASSERT_TRUE(eeprom_persistence_ok()) << cut;
        EXPECT_EQ(nvm_dynamic_keymap_read_keycode(0,0,0),0x1234) << cut;
        nvm_dynamic_keymap_macro_read_buffer(3294,1,&marker);
        EXPECT_NE(marker,0) << cut;
        nvm_dynamic_keymap_macro_update_buffer(3294,1,&close);
        EXPECT_FALSE(eeprom_persistence_ok()) << cut;
        nvm_dynamic_keymap_macro_read_buffer(3294,1,&marker);
        EXPECT_NE(marker,0) << cut;
        nvm_dynamic_keymap_macro_update_buffer(3294,1,&opener);
        nvm_dynamic_keymap_macro_update_buffer(3294,1,&close);
        EXPECT_FALSE(eeprom_persistence_ok()) << cut; // opener alone cannot bless a cut prefix
        nvm_dynamic_keymap_macro_update_buffer(3294,1,&opener);
        nvm_dynamic_keymap_macro_update_buffer(0,chunk.size(),chunk.data());
        nvm_dynamic_keymap_macro_update_buffer(3294,1,&close);
        ASSERT_TRUE(eeprom_persistence_ok());
        eeprom_driver_init();
        std::array<uint8_t,28> actual;
        nvm_dynamic_keymap_macro_read_buffer(0,actual.size(),actual.data());
        EXPECT_EQ(actual,chunk) << cut;
        nvm_dynamic_keymap_macro_read_buffer(3294,1,&marker);
        EXPECT_EQ(marker,0) << cut;
    }
    printf("POWER CUT wear-level macro packet boundaries=%llu\n",(unsigned long long)programs);
}

TEST_F(EraWlQmkDriver, ColdConsolidationCutsExposeSingleBankRecoveryLimit) {
    auto &flash=MockBackingStore::Instance();
    std::array<uint8_t,4096> old_image, desired, actual, blank{};
    old_image.fill(0x35);
    desired.fill(0x5A);
    auto seed_pending = [&]() {
        SetUp();
        eeprom_write_block(old_image.data(),(void *)0,old_image.size());
        ASSERT_TRUE(eeprom_persistence_ok());
        flash.set_write_callback([](uint64_t,uint32_t) { return false; });
        eeprom_write_block(desired.data(),(void *)0,desired.size());
        ASSERT_FALSE(eeprom_persistence_ok());
        flash.set_write_callback([](uint64_t,uint32_t) { return true; });
    };
    seed_pending();
    auto before=flash.write_invoke_count();
    eeprom_write_block(desired.data(),(void *)0,desired.size());
    ASSERT_TRUE(eeprom_persistence_ok());
    auto programs=flash.write_invoke_count()-before;
    ASSERT_GT(programs,0U);
    for (uint64_t cut=1; cut<=programs; ++cut) {
        seed_pending();
        auto stop=flash.write_invoke_count()+cut;
        flash.set_write_callback([stop](uint64_t count,uint32_t) { return count<stop; });
        eeprom_write_block(desired.data(),(void *)0,desired.size());
        ASSERT_FALSE(eeprom_persistence_ok()) << cut;
        flash.set_write_callback([](uint64_t,uint32_t) { return true; });
        eeprom_driver_init();
        ASSERT_TRUE(eeprom_persistence_ok()) << cut;
        eeprom_read_block(actual.data(),(void *)0,actual.size());
        // The old bank was erased. An incomplete consolidated image fails its
        // checksum and mounts blank; this backend does not promise old-or-new.
        EXPECT_EQ(actual,blank) << cut;
        eeprom_write_block(desired.data(),(void *)0,desired.size());
        ASSERT_TRUE(eeprom_persistence_ok());
        eeprom_driver_init();
        eeprom_read_block(actual.data(),(void *)0,actual.size());
        EXPECT_EQ(actual,desired) << cut;
    }
    // The RP2040 backing allocation spans two 4 KiB erase sectors. Also model
    // a cut part way through a sector, without claiming silicon erase ordering.
    for (unsigned erased_words : {0U, 1024U, 2048U, 4096U}) {
        seed_pending();
        unsigned visited=0;
        flash.set_erase_callback([&](uint64_t) { return visited++<erased_words; });
        flash.set_write_callback([](uint64_t,uint32_t) { return false; });
        eeprom_write_block(desired.data(),(void *)0,desired.size());
        ASSERT_FALSE(eeprom_persistence_ok());
        flash.set_erase_callback([](uint64_t) { return true; });
        flash.set_write_callback([](uint64_t,uint32_t) { return true; });
        eeprom_driver_init();
        ASSERT_TRUE(eeprom_persistence_ok());
        eeprom_read_block(actual.data(),(void *)0,actual.size());
        EXPECT_EQ(actual,erased_words==0U ? old_image : blank) << erased_words;
        eeprom_write_block(desired.data(),(void *)0,desired.size());
        ASSERT_TRUE(eeprom_persistence_ok());
        eeprom_driver_init();
        eeprom_read_block(actual.data(),(void *)0,actual.size());
        EXPECT_EQ(actual,desired);
    }
    printf("POWER CUT wear-level consolidation program=%llu erase=4\n",(unsigned long long)programs);
}

TEST_F(EraWlQmkDriver, ViaReportsKnownFailureAndAcceptsIdenticalKeymapRetry) {
    std::array<uint8_t,32> packet;
    packet.fill(0x5A);
    packet[0]=id_dynamic_keymap_set_buffer;
    packet[1]=packet[2]=0;
    packet[3]=28;
    era_state_sync_set_revisions_for_testing(1,1,1);
    auto &flash=MockBackingStore::Instance();
    flash.set_write_callback([](uint64_t,uint32_t) { return false; });
    raw_hid_receive(packet.data(),packet.size());
    EXPECT_EQ(g_response_command,id_unhandled);
    EXPECT_FALSE(eeprom_persistence_ok());
    EXPECT_EQ(era_state_sync_keymap_revision(),1U);
    flash.set_write_callback([](uint64_t,uint32_t) { return true; });
    packet[0]=id_dynamic_keymap_set_buffer;
    raw_hid_receive(packet.data(),packet.size());
    EXPECT_EQ(g_response_command,id_dynamic_keymap_set_buffer);
    EXPECT_TRUE(eeprom_persistence_ok());
    EXPECT_GT(era_state_sync_keymap_revision(),1U);
    eeprom_driver_init();
    std::array<uint8_t,28> actual;
    nvm_dynamic_keymap_read_buffer(0,actual.size(),actual.data());
    for (auto byte : actual) EXPECT_EQ(byte,0x5A);
}
