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
#include "quantum.h"
}

#include <array>
#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kMacroAddress = 617U;
constexpr uint32_t kMacroSize    = 16384U;
constexpr uint32_t kMacroMarker  = kMacroAddress + kMacroSize - 1U;

struct FakeNor {
    std::array<uint8_t, ERA_NVM_PHYSICAL_SIZE_BYTES> bytes{};
    uint64_t program_calls     = 0;
    uint64_t erase_calls       = 0;
    uint64_t fail_program_call = 0;

    FakeNor() {
        bytes.fill(0xFF);
    }

    static bool Init(void *) {
        return true;
    }

    static bool Read(void *context, uint32_t offset, void *data, size_t length) {
        auto *self = static_cast<FakeNor *>(context);
        if (length > self->bytes.size() || offset > self->bytes.size() - length) {
            return false;
        }
        std::memcpy(data, self->bytes.data() + offset, length);
        return true;
    }

    static bool Program(void *context, uint32_t offset, const void *data, size_t length) {
        auto *self = static_cast<FakeNor *>(context);
        self->program_calls++;
        if (self->fail_program_call != 0 && self->program_calls == self->fail_program_call) {
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

extern "C" bool process_record_via(uint16_t, keyrecord_t *) {
    return true;
}

extern "C" bool via_eeprom_is_valid(void) {
    return true;
}

extern "C" void via_init(void) {}
extern "C" void eeconfig_init_via(void) {}

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

    /* A 16-KiB append is 68 page-program callbacks; fail the final commit
     * authority exactly as Session 1's bounded fault test does. */
    g_flash.fail_program_call = g_flash.program_calls + 68U;
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
