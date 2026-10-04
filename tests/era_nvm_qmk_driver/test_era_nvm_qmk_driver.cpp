// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "eeprom.h"
#include "eeprom_driver.h"
#include "keyboards/era/common/storage/era_eeprom_driver.h"
#define _Static_assert static_assert
#include "keyboards/era/common/storage/era_eeprom_layout.h"
#include "keyboards/era/common/split/era_split_sync_storage.h"
#undef _Static_assert
#include "keyboards/era/common/storage/era_nvm_rp2040.h"
#include "nvm_dynamic_keymap.h"
#include "via.h"
#include "quantum.h"
void raw_hid_receive(uint8_t *data, uint8_t length);
}

#include <array>
#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kMacroAddress = DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR;
constexpr uint32_t kMacroSize    = 16384U;
constexpr uint32_t kMacroMarker  = kMacroAddress + kMacroSize - 1U;

struct FakeNor {
    std::array<uint8_t, ERA_NVM_PHYSICAL_SIZE_BYTES> bytes{};
    uint64_t program_calls     = 0;
    uint64_t erase_calls       = 0;
    uint64_t fail_program_call = 0;
    uint64_t read_calls = 0, fail_read_call = 0, fail_erase_call = 0;
    bool cut_power = false, powered = true, reject_programs = false;

    FakeNor() {
        bytes.fill(0xFF);
    }

    static bool Init(void *) {
        return true;
    }

    static bool Read(void *context, uint32_t offset, void *data, size_t length) {
        auto *self = static_cast<FakeNor *>(context);
        self->read_calls++;
        if (!self->powered || (self->fail_read_call && self->read_calls == self->fail_read_call)) return false;
        if (length > self->bytes.size() || offset > self->bytes.size() - length) {
            return false;
        }
        std::memcpy(data, self->bytes.data() + offset, length);
        return true;
    }

    static bool Program(void *context, uint32_t offset, const void *data, size_t length) {
        auto *self = static_cast<FakeNor *>(context);
        self->program_calls++;
        if (!self->powered || self->reject_programs) return false;
        if (self->fail_program_call != 0 && self->program_calls == self->fail_program_call) {
            if (self->cut_power) {
                const auto *partial = static_cast<const uint8_t *>(data);
                for (size_t i = 0; i < length / 2; ++i) self->bytes[offset + i] &= partial[i];
                self->powered = false;
            }
            return false;
        }
        if (length == 0 || length > ERA_NVM_PROGRAM_PAGE_BYTES || offset > self->bytes.size() - length ||
            offset / ERA_NVM_PROGRAM_PAGE_BYTES != (offset + length - 1U) / ERA_NVM_PROGRAM_PAGE_BYTES) {
            return false;
        }
        const auto *source = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < length; ++i) {
            if ((self->bytes[offset + i] & source[i]) != source[i]) {
                return false;
            }
        }
        for (size_t i = 0; i < length; ++i) {
            self->bytes[offset + i] &= source[i];
        }
        return true;
    }

    static bool Erase(void *context, uint32_t offset) {
        auto *self = static_cast<FakeNor *>(context);
        self->erase_calls++;
        if (!self->powered || (self->fail_erase_call && self->erase_calls == self->fail_erase_call)) return false;
        if (offset % ERA_NVM_ERASE_SECTOR_BYTES != 0U || offset > self->bytes.size() - ERA_NVM_ERASE_SECTOR_BYTES) {
            return false;
        }
        std::memset(self->bytes.data() + offset, 0xFF, ERA_NVM_ERASE_SECTOR_BYTES);
        return true;
    }
};

FakeNor g_flash;
uint32_t g_notify_count;
uint16_t g_notify_offset;
uint16_t g_notify_length;
uint8_t g_response_command;
unsigned g_response_count;

void reset_notifications() {
    g_notify_count  = 0;
    g_notify_offset = 0;
    g_notify_length = 0;
}

class EraNvmQmkDriver : public ::testing::Test {
   protected:
    void SetUp() override {
        g_flash = FakeNor{};
        reset_notifications();
        eeprom_driver_init();
        ASSERT_TRUE(era_eeprom_driver_ready());
    }
};

} // namespace

extern "C" bool era_nvm_rp2040_flash_bind(era_nvm_flash_t *flash) {
    if (flash == nullptr) {
        return false;
    }
    *flash = era_nvm_flash_t{
        .context      = &g_flash,
        .init         = FakeNor::Init,
        .read         = FakeNor::Read,
        .program      = FakeNor::Program,
        .erase_sector = FakeNor::Erase,
    };
    return true;
}

extern "C" void era_state_sync_note_eeprom_span(uint16_t offset, uint16_t length) {
    g_notify_count++;
    g_notify_offset = offset;
    g_notify_length = length;
}

// This target observes physical completions. The production State Sync masks
// and revision semantics execute in era_persistence_failures.
extern "C" uint32_t era_state_sync_eeprom_span_mask(uint16_t, uint16_t length) { return length ? 1U : 0U; }
extern "C" void era_state_sync_note_eeprom_mask(uint32_t mask) { if (mask) g_notify_count++; }

extern "C" void raw_hid_send(uint8_t *data, uint8_t) { g_response_command=data[0]; ++g_response_count; }

TEST_F(EraNvmQmkDriver, StockReadWriteUpdateUsesReplayableEraNvmAndExactChangedEnvelope) {
    uint8_t initial[4] = {0x11, 0x22, 0x33, 0x44};
    eeprom_write_block(initial, reinterpret_cast<void *>(100U), sizeof(initial));
    EXPECT_EQ(g_notify_count, 1U);
    EXPECT_EQ(g_notify_offset, 100U);
    EXPECT_EQ(g_notify_length, 4U);

    reset_notifications();
    uint8_t updated[4] = {0x11, 0xA2, 0xB3, 0x44};
    eeprom_update_block(updated, reinterpret_cast<void *>(100U), sizeof(updated));
    EXPECT_EQ(g_notify_count, 1U);
    EXPECT_EQ(g_notify_offset, 101U);
    EXPECT_EQ(g_notify_length, 2U);

    uint8_t replay[4] = {};
    ASSERT_EQ(era_eeprom_driver_replay_read(100U, replay, sizeof(replay)), ERA_NVM_RESULT_OK);
    EXPECT_EQ(std::memcmp(replay, updated, sizeof(updated)), 0);

    eeprom_driver_init();
    uint8_t remounted[4] = {};
    eeprom_read_block(remounted, reinterpret_cast<const void *>(100U), sizeof(remounted));
    EXPECT_EQ(std::memcmp(remounted, updated, sizeof(updated)), 0);
}

TEST_F(EraNvmQmkDriver, StorageMetadataIsDurableWithoutLocalSemanticNotification) {
    std::array<uint8_t, 32> metadata{};
    for (size_t i = 0; i < metadata.size(); ++i) {
        metadata[i] = static_cast<uint8_t>(0x40U + i);
    }

    reset_notifications();
    ASSERT_EQ(era_eeprom_driver_write_storage_metadata(257U, metadata.data(), metadata.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(g_notify_count, 0U);

    std::array<uint8_t, 32> replay{};
    ASSERT_EQ(era_eeprom_driver_replay_read(257U, replay.data(), replay.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(replay, metadata);
}

TEST_F(EraNvmQmkDriver, StorageMetadataCounterFailureKeepsPublishedCounterOldUntilRetry) {
    constexpr uint32_t kCounterAddress = ERA_EEPROM_CONFIG_ADDR + ERA_SPLIT_EEPROM_SYNC_POLICY_CONFIG_OFFSET +
                                         ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_OFFSET;
    std::array<uint8_t, ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_BYTES> old_counter = {7U, 0U};
    std::array<uint8_t, ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_BYTES> new_counter = {8U, 0U};

    ASSERT_EQ(era_eeprom_driver_write_storage_metadata(kCounterAddress, old_counter.data(), old_counter.size()), ERA_NVM_RESULT_OK);
    g_flash.fail_program_call = g_flash.program_calls + 1U;
    EXPECT_EQ(era_eeprom_driver_write_storage_metadata(kCounterAddress, new_counter.data(), new_counter.size()), ERA_NVM_RESULT_IO_ERROR);

    std::array<uint8_t, ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_BYTES> published{};
    eeprom_read_block(published.data(), reinterpret_cast<const void *>(kCounterAddress), published.size());
    EXPECT_EQ(published, old_counter);
    std::array<uint8_t, ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_BYTES> replay{};
    ASSERT_EQ(era_eeprom_driver_replay_read(kCounterAddress, replay.data(), replay.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(replay, old_counter);

    ASSERT_EQ(era_eeprom_driver_write_storage_metadata(kCounterAddress, new_counter.data(), new_counter.size()), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_eeprom_driver_replay_read(kCounterAddress, replay.data(), replay.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(replay, new_counter);
}

TEST_F(EraNvmQmkDriver, StorageMetadataConvergenceEnvelopeFailurePublishesNeitherCounterNorBaseline) {
    constexpr uint32_t kRecencyConfigOffset = ERA_SPLIT_EEPROM_SYNC_POLICY_CONFIG_OFFSET +
                                               ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_OFFSET;
    constexpr uint32_t kRecencyAddress = ERA_EEPROM_CONFIG_ADDR + kRecencyConfigOffset;
    constexpr size_t kRecencySize = ERA_EEPROM_PROTECTED_RESERVED_OFFSET - kRecencyConfigOffset;
    static_assert(kRecencySize == 66U, "Host test must cover the exact counter-through-baseline convergence envelope.");

    std::array<uint8_t, kRecencySize> before{};
    for (size_t i = 0; i < before.size(); ++i) {
        before[i] = static_cast<uint8_t>(0x20U + i);
    }
    ASSERT_EQ(era_eeprom_driver_write_storage_metadata(kRecencyAddress, before.data(), before.size()), ERA_NVM_RESULT_OK);

    auto candidate = before;
    /* Domain-0 counter clear plus one baseline CRC/guard-shaped change. The
     * production convergence helper publishes this entire protected envelope
     * as one ERA NVM record precisely so neither half can become durable alone. */
    candidate[0] = 0U;
    candidate[1] = 0U;
    constexpr size_t kBaselineIndex = ERA_EEPROM_SYNC_BASELINE_CONFIG_OFFSET - kRecencyConfigOffset;
    candidate[kBaselineIndex + 0U] ^= 0x5AU;
    candidate[kBaselineIndex + ERA_EEPROM_SYNC_BASELINE_CONFIG_SIZE - 1U] ^= 0xA5U;

    g_flash.fail_program_call = g_flash.program_calls + 1U;
    EXPECT_EQ(era_eeprom_driver_write_storage_metadata(kRecencyAddress, candidate.data(), candidate.size()), ERA_NVM_RESULT_IO_ERROR);

    std::array<uint8_t, kRecencySize> published{};
    eeprom_read_block(published.data(), reinterpret_cast<const void *>(kRecencyAddress), published.size());
    EXPECT_EQ(published, before);
    std::array<uint8_t, kRecencySize> replay{};
    ASSERT_EQ(era_eeprom_driver_replay_read(kRecencyAddress, replay.data(), replay.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(replay, before);

    ASSERT_EQ(era_eeprom_driver_write_storage_metadata(kRecencyAddress, candidate.data(), candidate.size()), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_eeprom_driver_replay_read(kRecencyAddress, replay.data(), replay.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(replay, candidate);
}

TEST_F(EraNvmQmkDriver, StockMacroResetLoopIsOneDurableTransactionAndPublishesOnlyAfterClose) {
    /* Seed the macro using the same public transcript VIA uses. Payload while
     * IDLE is intentionally refused, so a direct byte write would not be a
     * valid setup for the stock RESET loop. */
    uint8_t invalid = 0xFF;
    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &invalid);
    uint8_t payload = 0x5A;
    nvm_dynamic_keymap_macro_update_buffer(31U, 1U, &payload);
    uint8_t valid = 0U;
    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &valid);
    reset_notifications();

    era_nvm_diagnostics_t before{};
    era_eeprom_driver_get_nvm_diagnostics(&before);
    nvm_dynamic_keymap_macro_reset();
    era_nvm_diagnostics_t after{};
    era_eeprom_driver_get_nvm_diagnostics(&after);

    EXPECT_EQ(g_notify_count, 1U);
    EXPECT_EQ(g_notify_offset, kMacroAddress);
    EXPECT_EQ(g_notify_length, kMacroSize);
    EXPECT_GT(after.program_count, before.program_count);

    uint8_t zero = 0xFF;
    ASSERT_EQ(era_eeprom_driver_replay_read(kMacroAddress + 31U, &zero, 1U), ERA_NVM_RESULT_OK);
    EXPECT_EQ(zero, 0U);
}

TEST_F(EraNvmQmkDriver, StagedMacroAllowsDeferredRgbStyleWriteAndPersistsItAcrossMount) {
    uint8_t invalid = 0xFF;
    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &invalid);
    ASSERT_TRUE(era_eeprom_driver_macro_transaction_open());

    reset_notifications();
    uint32_t rgb = 0xA1B2C3D4U;
    eeprom_update_dword(reinterpret_cast<uint32_t *>(23U), rgb);
    EXPECT_EQ(g_notify_count, 1U);
    EXPECT_EQ(g_notify_offset, 23U);
    EXPECT_EQ(g_notify_length, sizeof(rgb));
    ASSERT_TRUE(era_eeprom_driver_macro_transaction_open());

    uint32_t replayed_rgb = 0;
    ASSERT_EQ(era_eeprom_driver_replay_read(23U, &replayed_rgb, sizeof(replayed_rgb)), ERA_NVM_RESULT_OK);
    EXPECT_EQ(replayed_rgb, rgb);

    uint8_t payload = 0x42;
    nvm_dynamic_keymap_macro_update_buffer(0U, 1U, &payload);
    uint8_t valid = 0;
    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &valid);
    ASSERT_FALSE(era_eeprom_driver_macro_transaction_open());

    eeprom_driver_init();
    EXPECT_EQ(eeprom_read_dword(reinterpret_cast<const uint32_t *>(23U)), rgb);
    uint8_t replayed_payload = 0;
    ASSERT_EQ(era_eeprom_driver_replay_read(kMacroAddress, &replayed_payload, 1U), ERA_NVM_RESULT_OK);
    EXPECT_EQ(replayed_payload, payload);
}

TEST_F(EraNvmQmkDriver, StockMacroStagingSurvivesUnrelatedRotationWithoutPublishingOrReplacingCommittedBytes) {
    uint8_t invalid = 0xFF;
    uint8_t valid = 0U;
    uint8_t old_payload = 0x31;
    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &invalid);
    nvm_dynamic_keymap_macro_update_buffer(31U, 1U, &old_payload);
    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &valid);
    ASSERT_FALSE(era_eeprom_driver_macro_transaction_open());
    eeprom_update_byte(reinterpret_cast<uint8_t *>(kMacroAddress - 1U), 0x7BU);
    eeprom_update_byte(reinterpret_cast<uint8_t *>(kMacroMarker + 1U), 0x9CU);

    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &invalid);
    uint8_t staged = 0x58;
    nvm_dynamic_keymap_macro_update_buffer(0U, 1U, &staged);
    reset_notifications();
    std::array<uint8_t, 8> rgb{};
    // More than a complete journal of real QMK RGB-sized updates. The macro
    // starts at an unaligned address, so both boundary pages have neighbours.
    for (uint32_t i = 0U; i < 800U; ++i) {
        rgb.fill(static_cast<uint8_t>(i + 1U));
        eeprom_update_block(rgb.data(), reinterpret_cast<void *>(23U), rgb.size());
        ASSERT_EQ(g_notify_count, i + 1U);
        ASSERT_EQ(g_notify_offset, 23U);
        ASSERT_EQ(g_notify_length, rgb.size());
    }
    ASSERT_TRUE(era_eeprom_driver_macro_transaction_open());
    EXPECT_EQ(eeprom_read_byte(reinterpret_cast<const uint8_t *>(kMacroAddress)), staged);
    EXPECT_EQ(eeprom_read_byte(reinterpret_cast<const uint8_t *>(kMacroMarker)), invalid);
    std::array<uint8_t, kMacroSize> expected{};
    expected[31U] = old_payload;
    std::array<uint8_t, kMacroSize> replay{};
    ASSERT_EQ(era_eeprom_driver_replay_read(kMacroAddress, replay.data(), replay.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(replay, expected);
    const auto interrupted_flash = g_flash.bytes;

    reset_notifications();
    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &valid);
    EXPECT_FALSE(era_eeprom_driver_macro_transaction_open());
    EXPECT_EQ(g_notify_count, 1U);
    EXPECT_EQ(g_notify_offset, kMacroAddress);
    EXPECT_EQ(g_notify_length, kMacroSize);
    expected[0U] = staged;
    eeprom_driver_init();
    eeprom_read_block(replay.data(), reinterpret_cast<const void *>(kMacroAddress), replay.size());
    EXPECT_EQ(replay, expected);

    // Independent reboot branch: power loss before CLOSE recovers the old
    // complete macro and the new unrelated setting, never the staged marker.
    g_flash.bytes = interrupted_flash;
    eeprom_driver_init();
    expected[0U] = 0U;
    eeprom_read_block(replay.data(), reinterpret_cast<const void *>(kMacroAddress), replay.size());
    EXPECT_EQ(replay, expected);
    std::array<uint8_t, 8> rgb_after{};
    eeprom_read_block(rgb_after.data(), reinterpret_cast<const void *>(23U), rgb_after.size());
    EXPECT_EQ(rgb_after, rgb);
    EXPECT_EQ(eeprom_read_byte(reinterpret_cast<const uint8_t *>(kMacroAddress - 1U)), 0x7BU);
    EXPECT_EQ(eeprom_read_byte(reinterpret_cast<const uint8_t *>(kMacroMarker + 1U)), 0x9CU);
}

TEST_F(EraNvmQmkDriver, MacroStagingPrecedesDurableSemanticPublication) {
    reset_notifications();

    uint8_t invalid = 0xFF;
    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &invalid);
    ASSERT_TRUE(era_eeprom_driver_macro_transaction_open());
    EXPECT_EQ(g_notify_count, 0U);

    uint8_t payload = 0x5CU;
    nvm_dynamic_keymap_macro_update_buffer(0U, 1U, &payload);
    ASSERT_TRUE(era_eeprom_driver_macro_transaction_open());
    EXPECT_EQ(g_notify_count, 0U);

    uint8_t valid = 0U;
    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &valid);
    ASSERT_FALSE(era_eeprom_driver_macro_transaction_open());
    EXPECT_EQ(g_notify_count, 1U);
    EXPECT_EQ(g_notify_offset, kMacroAddress);
    EXPECT_EQ(g_notify_length, kMacroSize);
}

TEST_F(EraNvmQmkDriver, MacroTouchingReplacementIsRefusedWhileStaged) {
    uint8_t invalid = 0xFF;
    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &invalid);
    ASSERT_TRUE(era_eeprom_driver_macro_transaction_open());

    uint8_t candidate[4] = {1, 2, 3, 4};
    EXPECT_EQ(era_eeprom_driver_replace(kMacroAddress, candidate, sizeof(candidate), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_BUSY);
}

TEST_F(EraNvmQmkDriver, FailedMacroCloseLeavesMarkerNonzeroAndNoMacroPublication) {
    uint8_t invalid = 0xFF;
    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &invalid);
    uint8_t payload = 0x77;
    nvm_dynamic_keymap_macro_update_buffer(0U, 1U, &payload);
    reset_notifications();

    /* A 16-KiB append is 66 page-program callbacks; fail the final commit
     * authority exactly as Session 1's bounded fault test does. */
    g_flash.fail_program_call = g_flash.program_calls + 66U;
    uint8_t valid = 0;
    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &valid);

    EXPECT_TRUE(era_eeprom_driver_macro_transaction_open());
    EXPECT_EQ(g_notify_count, 0U);
    uint8_t marker = 0;
    eeprom_read_block(&marker, reinterpret_cast<const void *>(kMacroMarker), 1U);
    EXPECT_NE(marker, 0U);
}

TEST_F(EraNvmQmkDriver, DriverEraseFormatsWholeStoreAndReplayParserSeesFreshZeros) {
    uint32_t value = 0x12345678U;
    eeprom_write_dword(reinterpret_cast<uint32_t *>(400U), value);
    eeprom_driver_erase();
    ASSERT_TRUE(era_eeprom_driver_ready());

    uint32_t replay = UINT32_MAX;
    ASSERT_EQ(era_eeprom_driver_replay_read(400U, &replay, sizeof(replay)), ERA_NVM_RESULT_OK);
    EXPECT_EQ(replay, 0U);
}

TEST_F(EraNvmQmkDriver, CleanPrepareIsProvedByProductionReplayParser) {
    constexpr uint16_t kMagicOff = 0xFFFFU;
    ASSERT_EQ(era_eeprom_driver_prepare_reboot_word(0U, kMagicOff), ERA_NVM_RESULT_OK);
    uint16_t replay = 0;
    ASSERT_EQ(era_eeprom_driver_replay_read(0U, &replay, sizeof(replay)), ERA_NVM_RESULT_OK);
    EXPECT_EQ(replay, kMagicOff);
}

TEST_F(EraNvmQmkDriver, CleanPreparePhysicalRecordFailureCannotClaimPrepared) {
    constexpr uint16_t kMagicOn  = 0x1234U;
    constexpr uint16_t kMagicOff = 0xFFFFU;
    eeprom_update_word(reinterpret_cast<uint16_t *>(0U), kMagicOn);

    /* Fail the next physical program mutation. The result-bearing CLEAN seam
     * must surface that failure; the restart agreement's prepare-failure tests
     * then prove such a false result cannot become PREPARED or arm a deadline. */
    g_flash.fail_program_call = g_flash.program_calls + 1U;
    EXPECT_NE(era_eeprom_driver_prepare_reboot_word(0U, kMagicOff), ERA_NVM_RESULT_OK);

    uint16_t replay = 0;
    ASSERT_EQ(era_eeprom_driver_replay_read(0U, &replay, sizeof(replay)), ERA_NVM_RESULT_OK);
    EXPECT_EQ(replay, kMagicOn);
}

TEST_F(EraNvmQmkDriver, StorageCostEvidence) {
    const auto start = g_flash.program_calls;
    nvm_dynamic_keymap_update_keycode(0, 0, 0, 0x1234);
    const auto key = g_flash.program_calls;
    std::array<uint8_t, 28> payload;
    payload.fill(0x5A);
    nvm_dynamic_keymap_update_buffer(2, payload.size(), payload.data());
    const auto block = g_flash.program_calls;
    nvm_dynamic_keymap_update_buffer(2, payload.size(), payload.data());
    std::printf("COST keycode=%llu buffer28=%llu unchanged=%llu erase=%llu nvm_ram=%zu\n",
                (unsigned long long)(key-start), (unsigned long long)(block-key),
                (unsigned long long)(g_flash.program_calls-block),
                (unsigned long long)g_flash.erase_calls, sizeof(era_nvm_t));
    EXPECT_EQ(key-start, 2U);
    EXPECT_EQ(block-key, 2U);
    EXPECT_EQ(g_flash.program_calls, block);
}

TEST_F(EraNvmQmkDriver, ProductionBootInitializesOnceAndWarmBootWritesNothing) {
    const auto programs = g_flash.program_calls;
    const auto erases = g_flash.erase_calls;
    keyboard_init();
    ASSERT_TRUE(eeconfig_is_enabled());
    era_nvm_diagnostics_t diagnostics;
    era_eeprom_driver_get_nvm_diagnostics(&diagnostics);
    printf("COST first_boot_programs=%llu first_boot_erases=%llu\n",
           (unsigned long long)(g_flash.program_calls - programs), (unsigned long long)(g_flash.erase_calls - erases));
    EXPECT_EQ(g_flash.program_calls - programs, 99U);
    auto after = g_flash.program_calls;
    eeprom_driver_init();
    keyboard_init();
    EXPECT_EQ(g_flash.program_calls, after);
    EXPECT_TRUE(eeconfig_is_enabled());
}

TEST_F(EraNvmQmkDriver, NestedScopeHasOneDurablePublicationAndRetry) {
    uint8_t old = 0x13, next = 0x57;
    eeprom_update_byte((uint8_t *)100, old);
    reset_notifications();
    auto before = g_flash.program_calls;
    ASSERT_TRUE(eeprom_begin_transaction());
    eeprom_update_byte((uint8_t *)100, next);
    ASSERT_TRUE(eeprom_begin_transaction());
    eeprom_update_byte((uint8_t *)101, next);
    ASSERT_TRUE(eeprom_end_transaction());
    EXPECT_EQ(g_flash.program_calls, before);
    EXPECT_EQ(g_notify_count, 0U);
    g_flash.fail_program_call = before + 2U;
    EXPECT_FALSE(eeprom_end_transaction());
    EXPECT_EQ(eeprom_read_byte((uint8_t *)100), old);
    EXPECT_EQ(g_notify_count, 0U);
    g_flash.fail_program_call = 0U;
    ASSERT_TRUE(eeprom_begin_transaction());
    eeprom_update_byte((uint8_t *)100, next);
    eeprom_update_byte((uint8_t *)101, next);
    ASSERT_TRUE(eeprom_end_transaction());
    EXPECT_EQ(g_notify_count, 1U);
    eeprom_driver_init();
    EXPECT_EQ(eeprom_read_byte((uint8_t *)100), next);
    EXPECT_EQ(eeprom_read_byte((uint8_t *)101), next);
}

TEST_F(EraNvmQmkDriver, FactoryCommitFaultMatrixKeepsOldOrCompleteDefaults) {
    eeconfig_init();
    ASSERT_TRUE(eeconfig_is_enabled());
    nvm_dynamic_keymap_update_keycode(0, 0, 0, 0x1234);
    const FakeNor seed = g_flash;
    auto p = g_flash.program_calls, r = g_flash.read_calls, e = g_flash.erase_calls;
    eeconfig_init();
    const auto programs = g_flash.program_calls - p, reads = g_flash.read_calls - r, erases = g_flash.erase_calls - e;
    for (unsigned mode = 0; mode < 4; ++mode) {
        uint64_t count = mode < 2 ? programs : mode == 2 ? reads : erases;
        for (uint64_t boundary = 1; boundary <= count; ++boundary) {
            g_flash = seed;
            eeprom_driver_init();
            if (mode < 2) { g_flash.fail_program_call = g_flash.program_calls + boundary; g_flash.cut_power = mode == 1; }
            if (mode == 2) g_flash.fail_read_call = g_flash.read_calls + boundary;
            if (mode == 3) g_flash.fail_erase_call = g_flash.erase_calls + boundary;
            eeconfig_init();
            EXPECT_FALSE(eeprom_persistence_ok()) << mode << ':' << boundary;
            g_flash.powered = true;
            g_flash.fail_program_call = g_flash.fail_read_call = g_flash.fail_erase_call = 0U;
            eeprom_driver_init();
            ASSERT_TRUE(era_eeprom_driver_ready());
            auto recovered = nvm_dynamic_keymap_read_keycode(0, 0, 0);
            EXPECT_TRUE(recovered == 0x1234 || recovered == KC_NO) << mode << ':' << boundary;
            if (recovered == KC_NO) EXPECT_TRUE(via_eeprom_is_valid());
            eeconfig_init();
            ASSERT_TRUE(eeconfig_is_enabled());
            EXPECT_EQ(nvm_dynamic_keymap_read_keycode(0, 0, 0), KC_NO);
        }
    }
    printf("FAULT factory programs=%llu reads=%llu erases=%llu (plus torn-program power cuts)\n",
           (unsigned long long)programs, (unsigned long long)reads, (unsigned long long)erases);
}

TEST_F(EraNvmQmkDriver, ScopeEnvelopeCannotCommitAnUnrelatedOpenMacro) {
    uint8_t open = 0xFF, payload = 0x7A;
    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &open);
    nvm_dynamic_keymap_macro_update_buffer(0U, 1U, &payload);
    ASSERT_TRUE(eeprom_begin_transaction());
    eeprom_update_byte((uint8_t *)100, 0x37);
    eeprom_update_byte((uint8_t *)(uintptr_t)(kMacroMarker + 1U), 0x38);
    EXPECT_FALSE(eeprom_end_transaction());
    EXPECT_TRUE(era_eeprom_driver_macro_transaction_open());
    EXPECT_EQ(eeprom_read_byte((uint8_t *)100), 0U);
    uint8_t staged;
    nvm_dynamic_keymap_macro_read_buffer(0U, 1U, &staged);
    EXPECT_EQ(staged, payload);
    uint8_t closed = 0;
    nvm_dynamic_keymap_macro_update_buffer(kMacroSize - 1U, 1U, &closed);
    EXPECT_FALSE(era_eeprom_driver_macro_transaction_open());
    eeprom_driver_init();
    EXPECT_EQ(eeprom_read_byte((uint8_t *)(uintptr_t)kMacroAddress), payload);
}

TEST_F(EraNvmQmkDriver, ViaSynchronousWriteFailureCannotEchoLayoutSuccess) {
    const std::array<std::array<uint8_t,32>,5> commands = {{
        {{id_dynamic_keymap_set_keycode,0,0,0,0x12,0x34}},
        {{id_dynamic_keymap_set_buffer,0,0,2,0x12,0x34}},
        {{id_dynamic_keymap_reset}},
        {{id_set_keyboard_value,id_layout_options,0,0,0,0x77}},
        {{id_eeprom_reset}},
    }};
    for (const auto &command : commands) {
        SetUp();
        nvm_dynamic_keymap_update_keycode(0,0,0,0x4321);
        via_eeprom_set_valid(true);
        reset_notifications();
        g_response_count=0;
        g_flash.reject_programs=true;
        auto packet=command;
        raw_hid_receive(packet.data(),packet.size());
        EXPECT_EQ(g_response_count,1U);
        EXPECT_EQ(g_response_command,id_unhandled) << unsigned(command[0]);
        EXPECT_FALSE(eeprom_persistence_ok()) << unsigned(command[0]);
        EXPECT_EQ(g_notify_count,0U);
        EXPECT_EQ(nvm_dynamic_keymap_read_keycode(0,0,0),0x4321);
        g_flash.reject_programs=false;
        packet=command;
        raw_hid_receive(packet.data(),packet.size());
        EXPECT_EQ(g_response_command,command[0]) << unsigned(command[0]);
        ASSERT_TRUE(eeprom_persistence_ok());
        eeprom_driver_init();
        EXPECT_EQ(nvm_dynamic_keymap_read_keycode(0,0,0),
                  command[0]==id_set_keyboard_value ? 0x4321 :
                  command[0]==id_dynamic_keymap_reset || command[0]==id_eeprom_reset ? 0 : 0x1234);
        if (command[0]==id_set_keyboard_value) EXPECT_EQ(via_get_layout_options(),0x77U);
    }
}
