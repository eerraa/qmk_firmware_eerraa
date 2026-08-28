// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"
#include "backing_mocks.hpp"

#include <cstring>

extern "C" {
#include "eeprom_driver.h"
#include "nvm_eeconfig.h"
#include "wear_leveling.h"
}

namespace {

constexpr uintptr_t kMagicAddress = 0;
constexpr uint32_t  kLogStart     = WEAR_LEVELING_LOGICAL_SIZE + 8;
constexpr uint32_t  kNoFaultAddress = UINT32_MAX;

bool     g_inject_checked_prepare;
bool     g_checked_write_result;
bool     g_checked_read_result;
uint16_t g_checked_readback;
uint32_t g_yield_count;
uint32_t g_drop_addresses[2];
uint8_t  g_drop_address_count;
uint8_t  g_drop_address_index;
uint32_t g_reported_success_drop_count;
uint32_t g_reported_failure_before_write_address;
uint32_t g_reported_failure_before_write_count;
uint32_t g_reported_failure_after_write_address;
uint32_t g_reported_failure_after_write_count;
uint32_t g_reported_failure_wrong_write_address;
uint32_t g_reported_failure_wrong_write_count;
backing_store_int_t g_reported_failure_wrong_write_value;

void* magic_address() {
    return reinterpret_cast<void*>(kMagicAddress);
}

bool checked_write(uint16_t value) {
    return eeprom_driver_write_block_raw_checked(&value, magic_address(), sizeof(value));
}

bool checked_block(uint32_t address, const void *data, size_t length) {
    return eeprom_driver_write_block_raw_checked(data, reinterpret_cast<void *>(static_cast<uintptr_t>(address)), length);
}

bool reboot_checked_write(uint16_t value) {
    return eeprom_driver_write_word_reboot_checked(value, magic_address());
}

uint16_t checked_read() {
    uint16_t value = 0;
    EXPECT_TRUE(eeprom_driver_read_block_raw(&value, magic_address(), sizeof(value)));
    return value;
}

class EraEepromCleanPersistence : public testing::Test {
   protected:
    void SetUp() override {
        MockBackingStore::Instance().reset_instance();
        g_inject_checked_prepare      = false;
        g_checked_write_result        = false;
        g_checked_read_result         = false;
        g_checked_readback            = 0;
        g_yield_count                 = 0;
        g_drop_addresses[0]           = 0;
        g_drop_addresses[1]           = 0;
        g_drop_address_count          = 0;
        g_drop_address_index          = 0;
        g_reported_success_drop_count = 0;
        g_reported_failure_before_write_address = kNoFaultAddress;
        g_reported_failure_before_write_count   = 0;
        g_reported_failure_after_write_address = kNoFaultAddress;
        g_reported_failure_after_write_count   = 0;
        g_reported_failure_wrong_write_address = kNoFaultAddress;
        g_reported_failure_wrong_write_count   = 0;
        g_reported_failure_wrong_write_value   = 0;

        ASSERT_NE(wear_leveling_init(), WEAR_LEVELING_FAILED);
        ASSERT_TRUE(checked_write(EECONFIG_MAGIC_NUMBER));
        ASSERT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER);
    }
};

} // namespace

extern "C" bool __real_backing_store_write(uint32_t address, backing_store_int_t value);

/* Model the RP2040 backend's four program-result fault classes: reported
 * success with no effect, failure before effect, failure after the requested
 * effect, and failure after a different but syntactically valid log word. */
extern "C" bool __wrap_backing_store_write(uint32_t address, backing_store_int_t value) {
    if (g_drop_address_index < g_drop_address_count && address == g_drop_addresses[g_drop_address_index]) {
        g_drop_address_index++;
        g_reported_success_drop_count++;
        return true;
    }
    if (address == g_reported_failure_before_write_address &&
        g_reported_failure_before_write_count == 0U) {
        g_reported_failure_before_write_count++;
        return false;
    }
    if (address == g_reported_failure_after_write_address &&
        g_reported_failure_after_write_count == 0U) {
        g_reported_failure_after_write_count++;
        (void)__real_backing_store_write(address, value);
        return false;
    }
    if (address == g_reported_failure_wrong_write_address &&
        g_reported_failure_wrong_write_count == 0U) {
        g_reported_failure_wrong_write_count++;
        (void)__real_backing_store_write(address, g_reported_failure_wrong_write_value);
        return false;
    }
    return __real_backing_store_write(address, value);
}

/* This is the strong board hook reached while wear_leveling.c's private
 * erase-yield interlock is live. It executes ERA CLEAN's reboot-checked word
 * seam and records both halves of its success decision. */
extern "C" void backing_store_erase_yield_kb(void) {
    g_yield_count++;
    if (!g_inject_checked_prepare) {
        return;
    }

    g_inject_checked_prepare = false;
    const uint16_t off       = EECONFIG_MAGIC_NUMBER_OFF;
    g_checked_write_result   = eeprom_driver_write_word_reboot_checked(off, magic_address());
    g_checked_read_result    = eeprom_driver_read_block_raw(&g_checked_readback, magic_address(), sizeof(g_checked_readback));
}

TEST_F(EraEepromCleanPersistence, RebootCheckedPrepareRefusesEraseYieldWithoutChangingCache) {
    auto& backing = MockBackingStore::Instance();

    const uint64_t writes_before_gap = backing.write_invoke_count();
    g_inject_checked_prepare         = true;
    wear_leveling_backing_erase_yield();

    ASSERT_EQ(g_yield_count, 1U);
    ASSERT_FALSE(g_checked_write_result);
    ASSERT_TRUE(g_checked_read_result);
    ASSERT_EQ(g_checked_readback, EECONFIG_MAGIC_NUMBER);
    EXPECT_EQ(backing.write_invoke_count(), writes_before_gap);

    /* Reinitialisation preserves the mock backing store and discards RAM. The
     * failed prepare must have changed neither view. */
    ASSERT_NE(wear_leveling_init(), WEAR_LEVELING_FAILED);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER);
}

TEST_F(EraEepromCleanPersistence, RebootCheckedPreparePersistsOffAcrossPlayback) {
    auto& backing = MockBackingStore::Instance();

    const uint64_t writes_before_prepare = backing.write_invoke_count();
    ASSERT_TRUE(reboot_checked_write(EECONFIG_MAGIC_NUMBER_OFF));
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER_OFF);
    EXPECT_GT(backing.write_invoke_count(), writes_before_prepare);

    ASSERT_NE(wear_leveling_init(), WEAR_LEVELING_FAILED);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER_OFF);
}

TEST_F(EraEepromCleanPersistence, CacheEqualityCannotSubstituteForPhysicalPlayback) {
    auto& backing = MockBackingStore::Instance();

    /* Stage OFF normally, then remove only its two physical log entries. RAM
     * remains OFF while an ordinary boot replay still reconstructs ON. The
     * first checked attempt therefore performs no write; replay must expose
     * the mismatch and make the one bounded retry append OFF physically. */
    ASSERT_TRUE(checked_write(EECONFIG_MAGIC_NUMBER_OFF));
    ASSERT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER_OFF);
    auto log_start = backing.storage_begin() + (kLogStart / sizeof(backing_store_int_t));
    ASSERT_FALSE((log_start + 2)->is_erased());
    ASSERT_FALSE((log_start + 3)->is_erased());
    (log_start + 2)->erase();
    (log_start + 3)->erase();

    const uint64_t writes_before_prepare = backing.write_invoke_count();
    ASSERT_TRUE(reboot_checked_write(EECONFIG_MAGIC_NUMBER_OFF));
    EXPECT_EQ(backing.write_invoke_count(), writes_before_prepare + 2U);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER_OFF);

    ASSERT_NE(wear_leveling_init(), WEAR_LEVELING_FAILED);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER_OFF);
}

TEST_F(EraEepromCleanPersistence, PhysicalReplayInitFailureFailsClosedWithoutRetry) {
    auto& backing = MockBackingStore::Instance();

    const uint64_t writes_before_prepare = backing.write_invoke_count();
    const uint64_t failing_init          = backing.init_invoke_count() + 1U;
    backing.set_init_callback([failing_init](uint64_t count) { return count != failing_init; });

    EXPECT_FALSE(reboot_checked_write(EECONFIG_MAGIC_NUMBER_OFF));
    EXPECT_EQ(backing.init_invoke_count(), failing_init);
    EXPECT_EQ(backing.write_invoke_count(), writes_before_prepare + 2U);

    backing.set_init_callback([](uint64_t) { return true; });
    ASSERT_NE(wear_leveling_init(), WEAR_LEVELING_FAILED);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER_OFF);
}

TEST_F(EraEepromCleanPersistence, ReportedWriteFailureRollsForwardBeforePhysicalPlaybackProof) {
    /* The second OFF byte reaches physical flash and only its return status is
     * changed to failure. The return still makes that slot ambiguous: repair
     * erases the whole log, consolidates the already-complete OFF cache, and
     * only then lets CLEAN's ordinary boot playback grant PREPARED. */
    g_reported_failure_after_write_address = kLogStart + (3U * BACKING_STORE_WRITE_SIZE);

    auto&          backing               = MockBackingStore::Instance();
    const uint64_t writes_before_prepare = backing.write_invoke_count();
    const uint64_t erases_before_prepare = backing.erasure_count();
    ASSERT_TRUE(reboot_checked_write(EECONFIG_MAGIC_NUMBER_OFF));
    EXPECT_EQ(g_reported_failure_after_write_count, 1U);
    EXPECT_GT(backing.write_invoke_count(), writes_before_prepare + 2U);
    EXPECT_EQ(backing.erasure_count(), erases_before_prepare + 1U);

    ASSERT_NE(wear_leveling_init(), WEAR_LEVELING_FAILED);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER_OFF);
}

TEST_F(EraEepromCleanPersistence, FailedAppendRollsForwardItsWholeCallBoundary) {
    /* Five optimized bytes occupy five log slots. Fail the third after it was
     * physically programmed: recovery must not parse the ambiguous slot or
     * expose the two-entry prefix. The already-complete request cache is the
     * same authority used by log-full consolidation, so one whole-store
     * roll-forward makes all five bytes canonical together. */
    constexpr uint32_t address = 2;
    const uint8_t      candidate[5] = {0x31, 0x42, 0x53, 0x64, 0x75};
    g_reported_failure_after_write_address = kLogStart + (4U * BACKING_STORE_WRITE_SIZE);

    auto& backing = MockBackingStore::Instance();
    const uint64_t erases_before = backing.erasure_count();
    ASSERT_TRUE(checked_block(address, candidate, sizeof(candidate)));
    EXPECT_EQ(g_reported_failure_after_write_count, 1U);
    EXPECT_EQ(backing.erasure_count(), erases_before + 1U);
    EXPECT_TRUE(wear_leveling_is_healthy());

    uint8_t cache_readback[sizeof(candidate)] = {};
    ASSERT_EQ(wear_leveling_read(address, cache_readback, sizeof(cache_readback)), WEAR_LEVELING_SUCCESS);
    EXPECT_EQ(std::memcmp(cache_readback, candidate, sizeof(candidate)), 0);

    ASSERT_NE(wear_leveling_init(), WEAR_LEVELING_FAILED);
    uint8_t replay_readback[sizeof(candidate)] = {};
    ASSERT_EQ(wear_leveling_read(address, replay_readback, sizeof(replay_readback)), WEAR_LEVELING_SUCCESS);
    EXPECT_EQ(std::memcmp(replay_readback, candidate, sizeof(candidate)), 0);
}

TEST_F(EraEepromCleanPersistence, ReportedNoOpFailureRollsForwardOnce) {
    g_reported_failure_before_write_address = kLogStart + (2U * BACKING_STORE_WRITE_SIZE);

    auto& backing = MockBackingStore::Instance();
    const uint64_t erases_before = backing.erasure_count();
    ASSERT_TRUE(checked_write(EECONFIG_MAGIC_NUMBER_OFF));
    EXPECT_EQ(g_reported_failure_before_write_count, 1U);
    EXPECT_EQ(backing.erasure_count(), erases_before + 1U);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER_OFF);

    ASSERT_NE(wear_leveling_init(), WEAR_LEVELING_FAILED);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER_OFF);
}

TEST_F(EraEepromCleanPersistence, ValidWrongEntryAtFailedSlotIsNeverParsed) {
    auto& backing   = MockBackingStore::Instance();
    auto  log_start = backing.storage_begin() + (kLogStart / sizeof(backing_store_int_t));

    /* Replace the candidate's physical append with a different, syntactically
     * valid optimized entry copied from SetUp's first slot, then report false.
     * Parsing or retrying that ambiguous slot could mutate an unrelated byte;
     * recovery must instead erase it and consolidate the complete request
     * cache exactly once. */
    ASSERT_FALSE((log_start + 0)->is_erased());
    g_reported_failure_wrong_write_value   = (log_start + 0)->get();
    g_reported_failure_wrong_write_address = kLogStart + (2U * BACKING_STORE_WRITE_SIZE);

    constexpr uint32_t address   = 2;
    const uint8_t      candidate = 0x6D;
    const uint64_t     erases_before = backing.erasure_count();
    ASSERT_TRUE(checked_block(address, &candidate, sizeof(candidate)));
    EXPECT_EQ(g_reported_failure_wrong_write_count, 1U);
    EXPECT_EQ(backing.erasure_count(), erases_before + 1U);
    EXPECT_TRUE(wear_leveling_is_healthy());

    ASSERT_NE(wear_leveling_init(), WEAR_LEVELING_FAILED);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER);
    uint8_t replay_readback = 0;
    ASSERT_EQ(wear_leveling_read(address, &replay_readback, sizeof(replay_readback)), WEAR_LEVELING_SUCCESS);
    EXPECT_EQ(replay_readback, candidate);
}

TEST_F(EraEepromCleanPersistence, EraseVerificationFailureRetriesBeforePublishingEmptyCache) {
    auto& backing = MockBackingStore::Instance();
    backing.set_erase_callback([](uint64_t invocation) { return invocation != 1U; });

    EXPECT_EQ(wear_leveling_erase(), WEAR_LEVELING_SUCCESS);
    EXPECT_EQ(backing.erase_invoke_count(), 2U);
    EXPECT_TRUE(wear_leveling_is_healthy());

    uint16_t cache_value = UINT16_MAX;
    ASSERT_EQ(wear_leveling_read(kMagicAddress, &cache_value, sizeof(cache_value)), WEAR_LEVELING_SUCCESS);
    EXPECT_EQ(cache_value, 0U);
}

TEST_F(EraEepromCleanPersistence, RepeatedEraseFailureLeavesTheStoreUnpublishedAndWritesClosed) {
    auto& backing = MockBackingStore::Instance();
    backing.set_erase_callback([](uint64_t) { return false; });

    const uint64_t writes_before_erase = backing.write_invoke_count();
    EXPECT_EQ(wear_leveling_erase(), WEAR_LEVELING_FAILED);
    EXPECT_EQ(backing.erase_invoke_count(), 2U);
    EXPECT_FALSE(wear_leveling_is_healthy());
    EXPECT_FALSE(checked_write(EECONFIG_MAGIC_NUMBER_OFF));
    EXPECT_EQ(backing.write_invoke_count(), writes_before_erase);

    /* The latch is not permanent. A later explicit verified erase reopens the
     * store, after which boot-default writes and reboot playback work again. */
    backing.set_erase_callback([](uint64_t) { return true; });
    EXPECT_EQ(wear_leveling_erase(), WEAR_LEVELING_SUCCESS);
    EXPECT_TRUE(wear_leveling_is_healthy());
    EXPECT_TRUE(checked_write(EECONFIG_MAGIC_NUMBER));
    ASSERT_NE(wear_leveling_init(), WEAR_LEVELING_FAILED);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER);
}

TEST_F(EraEepromCleanPersistence, FailedRollForwardLetsRollbackRepairWithoutEqualitySuccess) {
    auto& backing = MockBackingStore::Instance();

    constexpr uint32_t address   = 2;
    const uint8_t      old_value = 0x19;
    const uint8_t      candidate = 0x6D;
    ASSERT_TRUE(checked_block(address, &old_value, sizeof(old_value)));

    /* Magic used two slots and old_value used one. Program candidate into the
     * fourth slot but report failure; then fail both whole-store recovery erase
     * attempts. The cache remains the complete candidate image, while the
     * backing/cursor are explicitly non-canonical. */
    g_reported_failure_after_write_address = kLogStart + (3U * BACKING_STORE_WRITE_SIZE);
    backing.set_erase_callback([](uint64_t invocation) { return invocation > 2U; });
    EXPECT_FALSE(checked_block(address, &candidate, sizeof(candidate)));
    EXPECT_EQ(g_reported_failure_after_write_count, 1U);
    EXPECT_FALSE(wear_leveling_is_healthy());

    uint8_t cache_value = 0;
    ASSERT_EQ(wear_leveling_read(address, &cache_value, sizeof(cache_value)), WEAR_LEVELING_SUCCESS);
    EXPECT_EQ(cache_value, candidate);

    /* Production Apply now supplies its protected old byte. This write may not
     * pass through cache equality or append at the failed slot: it repairs the
     * complete old cache with a verified consolidation. */
    ASSERT_TRUE(checked_block(address, &old_value, sizeof(old_value)));
    EXPECT_TRUE(wear_leveling_is_healthy());
    EXPECT_EQ(backing.erase_invoke_count(), 3U);

    ASSERT_NE(wear_leveling_init(), WEAR_LEVELING_FAILED);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER);
    ASSERT_EQ(wear_leveling_read(address, &cache_value, sizeof(cache_value)), WEAR_LEVELING_SUCCESS);
    EXPECT_EQ(cache_value, old_value);
}

TEST_F(EraEepromCleanPersistence, FailedLogFullConsolidationDoesNotSpendASecondRecoveryBudget) {
    auto& backing = MockBackingStore::Instance();

    /* SetUp consumes two of this target's twelve log slots. Fill nine more so
     * the candidate occupies the last slot and immediately enters the
     * log-full consolidation path. */
    for (uint8_t address = 2; address <= 10; ++address) {
        const uint8_t value = static_cast<uint8_t>(0x20U + address);
        ASSERT_TRUE(checked_block(address, &value, sizeof(value)));
    }

    backing.set_erase_callback([](uint64_t invocation) { return invocation > 2U; });
    const uint64_t erases_before = backing.erase_invoke_count();
    const uint8_t  candidate     = 0x7A;
    EXPECT_FALSE(checked_block(11, &candidate, sizeof(candidate)));
    EXPECT_EQ(backing.erase_invoke_count(), erases_before + 2U);
    EXPECT_FALSE(wear_leveling_is_healthy());

    /* The generation change proves write_raw() already spent its bounded
     * whole-image budget, so the outer write did not silently spend another.
     * Apply's protected old byte then owns the next repair attempt. */
    const uint8_t old_value = 0;
    ASSERT_TRUE(checked_block(11, &old_value, sizeof(old_value)));
    EXPECT_EQ(backing.erase_invoke_count(), erases_before + 3U);
    EXPECT_TRUE(wear_leveling_is_healthy());

    ASSERT_NE(wear_leveling_init(), WEAR_LEVELING_FAILED);
    uint8_t replay_readback = UINT8_MAX;
    ASSERT_EQ(wear_leveling_read(11, &replay_readback, sizeof(replay_readback)), WEAR_LEVELING_SUCCESS);
    EXPECT_EQ(replay_readback, old_value);
}

TEST_F(EraEepromCleanPersistence, ConsolidatedProgramFailureGetsOneWholeImageRetry) {
    auto& backing = MockBackingStore::Instance();

    /* Force the append to fail before effect, then fail the first consolidated
     * image word. The recovery owner must erase and retry the complete image,
     * never continue after that partial consolidated program. */
    g_reported_failure_before_write_address = kLogStart + (2U * BACKING_STORE_WRITE_SIZE);
    bool failed_consolidated_word = false;
    backing.set_write_callback([&failed_consolidated_word](uint64_t, uint32_t address) {
        if (!failed_consolidated_word && address == 0U) {
            failed_consolidated_word = true;
            return false;
        }
        return true;
    });

    ASSERT_TRUE(checked_write(EECONFIG_MAGIC_NUMBER_OFF));
    EXPECT_TRUE(failed_consolidated_word);
    EXPECT_EQ(backing.erase_invoke_count(), 2U);
    EXPECT_TRUE(wear_leveling_is_healthy());

    ASSERT_NE(wear_leveling_init(), WEAR_LEVELING_FAILED);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER_OFF);
}

TEST_F(EraEepromCleanPersistence, ReportedSuccessNoOpIsRepairedAndRetriedOnce) {
    /* SetUp's two optimized magic-byte entries occupy the first two slots.
     * Drop only the first OFF byte at the next slot; its second byte becomes a
     * stale tail behind that physical hole. */
    g_drop_addresses[0]  = kLogStart + (2U * BACKING_STORE_WRITE_SIZE);
    g_drop_address_count = 1;

    ASSERT_TRUE(reboot_checked_write(EECONFIG_MAGIC_NUMBER_OFF));
    EXPECT_EQ(g_reported_success_drop_count, 1U);
    EXPECT_EQ(g_drop_address_index, 1U);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER_OFF);

    ASSERT_NE(wear_leveling_init(), WEAR_LEVELING_FAILED);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER_OFF);
}

TEST_F(EraEepromCleanPersistence, RepeatedReportedSuccessNoOpStopsAfterOneRetry) {
    /* Attempt 1 starts after SetUp's two entries. Its hole/tail repair
     * consolidates the ON prefix and makes the retry start at canonical log
     * slot zero. Drop that retry too. A forbidden third attempt would have no
     * configured drop left and would turn the word OFF, so the final ON view
     * and false result prove the two-attempt bound. */
    g_drop_addresses[0]  = kLogStart + (2U * BACKING_STORE_WRITE_SIZE);
    g_drop_addresses[1]  = kLogStart;
    g_drop_address_count = 2;

    EXPECT_FALSE(reboot_checked_write(EECONFIG_MAGIC_NUMBER_OFF));
    EXPECT_EQ(g_reported_success_drop_count, 2U);
    EXPECT_EQ(g_drop_address_index, 2U);
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER);
}

TEST_F(EraEepromCleanPersistence, InitRepairsEmptyHoleAndStaleTailFromPrefixOnly) {
    auto& backing = MockBackingStore::Instance();

    /* SetUp wrote the two magic bytes as two independent optimized entries.
     * Add one more independent entry, then erase only the middle physical
     * slot: valid prefix, empty hole, nonzero stale tail. */
    const uint8_t stale_only = 0x5A;
    ASSERT_EQ(wear_leveling_write(2, &stale_only, sizeof(stale_only)), WEAR_LEVELING_SUCCESS);

    auto log_start = backing.storage_begin() + (kLogStart / sizeof(backing_store_int_t));
    ASSERT_FALSE((log_start + 0)->is_erased());
    ASSERT_FALSE((log_start + 1)->is_erased());
    ASSERT_FALSE((log_start + 2)->is_erased());
    (log_start + 1)->erase();
    ASSERT_TRUE((log_start + 1)->is_erased());
    ASSERT_FALSE((log_start + 2)->is_erased());

    const uint64_t erases_before_repair = backing.erasure_count();
    ASSERT_EQ(wear_leveling_init(), WEAR_LEVELING_CONSOLIDATED);
    EXPECT_EQ(backing.erasure_count(), erases_before_repair + 1U);

    /* Only the entry before the hole is authoritative. The second magic byte
     * and the address-2 value existed only at/after the hole and cannot return. */
    EXPECT_EQ(checked_read(), static_cast<uint16_t>(EECONFIG_MAGIC_NUMBER & 0x00FFU));
    uint8_t after_tail = 0xFF;
    ASSERT_EQ(wear_leveling_read(2, &after_tail, sizeof(after_tail)), WEAR_LEVELING_SUCCESS);
    EXPECT_EQ(after_tail, 0U);

    for (auto it = log_start; it != backing.storage_end(); ++it) {
        EXPECT_TRUE(it->is_erased());
    }

    /* A second boot is stable, and a new reboot-checked word begins from the
     * repaired canonical cursor rather than programming over stale flash. */
    ASSERT_EQ(wear_leveling_init(), WEAR_LEVELING_SUCCESS);
    EXPECT_EQ(checked_read(), static_cast<uint16_t>(EECONFIG_MAGIC_NUMBER & 0x00FFU));
    ASSERT_TRUE(reboot_checked_write(EECONFIG_MAGIC_NUMBER_OFF));
    EXPECT_EQ(checked_read(), EECONFIG_MAGIC_NUMBER_OFF);
}

TEST_F(EraEepromCleanPersistence, FailedBootTailRepairKeepsDefaultWritesClosed) {
    auto& backing = MockBackingStore::Instance();

    /* Build a valid prefix, an empty slot, and a nonzero hidden tail. Boot
     * playback accepts only the prefix and must consolidate it before any QMK
     * default write can use the reconstructed cache as durable authority. */
    const uint8_t hidden_tail = 0x5A;
    ASSERT_EQ(wear_leveling_write(2, &hidden_tail, sizeof(hidden_tail)), WEAR_LEVELING_SUCCESS);

    auto log_start = backing.storage_begin() + (kLogStart / sizeof(backing_store_int_t));
    ASSERT_FALSE((log_start + 0)->is_erased());
    ASSERT_FALSE((log_start + 1)->is_erased());
    ASSERT_FALSE((log_start + 2)->is_erased());
    (log_start + 1)->erase();

    backing.set_erase_callback([](uint64_t) { return false; });
    const uint64_t erases_before_init = backing.erase_invoke_count();
    const uint64_t writes_before_init = backing.write_invoke_count();

    EXPECT_EQ(wear_leveling_init(), WEAR_LEVELING_FAILED);
    EXPECT_EQ(backing.erase_invoke_count(), erases_before_init + 2U);
    EXPECT_FALSE(wear_leveling_is_healthy());

    /* init() clears its partial playback cache after the failed repair. That
     * zero/partial RAM view must remain unpublished: an ordinary boot-default
     * write fails before touching the ambiguous physical store. */
    EXPECT_FALSE(checked_write(EECONFIG_MAGIC_NUMBER));
    EXPECT_EQ(backing.write_invoke_count(), writes_before_init);
    EXPECT_EQ(backing.erase_invoke_count(), erases_before_init + 2U);
}
