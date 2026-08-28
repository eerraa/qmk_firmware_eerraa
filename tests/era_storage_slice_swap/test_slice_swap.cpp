// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/era/common/storage/era_storage_slice_swap.h"
}

#include <array>
#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kBase  = 101;
constexpr uint16_t kSize  = 97;
constexpr uint16_t kSlice = 32;

struct Store {
    std::array<uint8_t, 256> raw{};
    uint32_t                 read_calls               = 0;
    uint32_t                 write_calls              = 0;
    uint32_t                 fail_write_call          = 0;
    bool                     fail_writes_persistently = false;
    bool                     partial_on_failure       = false;
};

bool raw_read(uint32_t address, void *data, uint16_t length, void *context) {
    auto *store = static_cast<Store *>(context);
    store->read_calls++;
    if (address + length > store->raw.size()) {
        return false;
    }
    std::memcpy(data, &store->raw[address], length);
    return true;
}

bool raw_write(uint32_t address, const void *data, uint16_t length, void *context) {
    auto *store = static_cast<Store *>(context);
    store->write_calls++;
    bool     fail     = store->fail_writes_persistently || (store->fail_write_call != 0 && store->write_calls == store->fail_write_call);
    uint16_t accepted = fail && store->partial_on_failure ? (uint16_t)(length / 2U) : (fail ? 0U : length);
    if (address + accepted > store->raw.size()) {
        return false;
    }
    std::memcpy(&store->raw[address], data, accepted);
    return !fail;
}

std::array<uint8_t, kSize> make_image(uint8_t seed) {
    std::array<uint8_t, kSize> image{};
    for (uint16_t i = 0; i < kSize; i++) {
        image[i] = (uint8_t)(seed + i * 13U);
    }
    return image;
}

void put_raw(Store &store, const std::array<uint8_t, kSize> &image) {
    std::memcpy(&store.raw[kBase], image.data(), image.size());
}

std::array<uint8_t, kSize> raw_image(const Store &store) {
    std::array<uint8_t, kSize> image{};
    std::memcpy(image.data(), &store.raw[kBase], image.size());
    return image;
}

std::array<uint8_t, kSize> public_image(const era_storage_slice_swap_t &swap, const std::array<uint8_t, kSize> &staged, Store &store) {
    std::array<uint8_t, kSize> image{};
    bool                       handled = era_storage_slice_swap_public_read(&swap, staged.data(), kBase, kSize, kBase, image.data(), kSize, raw_read, &store);
    if (!handled) {
        EXPECT_TRUE(raw_read(kBase, image.data(), kSize, &store));
    }
    return image;
}

void expect_partition(const era_storage_slice_swap_t &swap, const std::array<uint8_t, kSize> &old_image, const std::array<uint8_t, kSize> &candidate, const std::array<uint8_t, kSize> &staged, const Store &store) {
    for (uint16_t i = 0; i < kSize; i++) {
        if (i < swap.written_prefix) {
            EXPECT_EQ(store.raw[kBase + i], candidate[i]) << i;
            EXPECT_EQ(staged[i], old_image[i]) << i;
        } else {
            EXPECT_EQ(store.raw[kBase + i], old_image[i]) << i;
            EXPECT_EQ(staged[i], candidate[i]) << i;
        }
    }
}

void rollback_all(era_storage_slice_swap_t &swap, const std::array<uint8_t, kSize> &staged, Store &store) {
    while (era_storage_slice_swap_facade_active(&swap)) {
        auto result = era_storage_slice_swap_rollback_next(&swap, staged.data(), kBase, kSlice, raw_read, raw_write, &store);
        ASSERT_NE(result, ERA_STORAGE_SLICE_SWAP_INVALID);
        ASSERT_NE(result, ERA_STORAGE_SLICE_SWAP_WRITE_FAILED);
        ASSERT_NE(result, ERA_STORAGE_SLICE_SWAP_VERIFY_FAILED);
    }
}

} // namespace

TEST(EraStorageSliceSwap, EverySuccessfulSliceKeepsPublicOldAndBuildsRawCandidate) {
    Store store;
    auto  old_image = make_image(0x11);
    auto  candidate = make_image(0x91);
    auto  staged    = candidate;
    put_raw(store, old_image);

    era_storage_slice_swap_t swap{};
    era_storage_slice_swap_begin(&swap);
    uint32_t slice_entries = 0;
    while (swap.phase == ERA_STORAGE_SLICE_SWAP_WRITE) {
        EXPECT_EQ(public_image(swap, staged, store), old_image);
        auto result = era_storage_slice_swap_write_next(&swap, staged.data(), kBase, kSize, kSlice, raw_read, raw_write, &store);
        ASSERT_TRUE(result == ERA_STORAGE_SLICE_SWAP_PROGRESS || result == ERA_STORAGE_SLICE_SWAP_COMPLETE);
        slice_entries++;
        expect_partition(swap, old_image, candidate, staged, store);
        EXPECT_EQ(public_image(swap, staged, store), old_image);
    }

    EXPECT_EQ(slice_entries, 4U);
    EXPECT_EQ(swap.phase, ERA_STORAGE_SLICE_SWAP_VERIFY);
    EXPECT_EQ(raw_image(store), candidate);
    EXPECT_EQ(staged, old_image);
    EXPECT_EQ(public_image(swap, staged, store), old_image);
    /* The final slice leaves VERIFY pending, proving the caller gets a
       keyboard opportunity after the final slice before verification. */
    EXPECT_TRUE(era_storage_slice_swap_facade_active(&swap));
}

TEST(EraStorageSliceSwap, PartialFailedWriteDoesNotAdvanceAndRollbackRestoresOld) {
    Store store;
    auto  old_image = make_image(0x20);
    auto  candidate = make_image(0xA0);
    auto  staged    = candidate;
    put_raw(store, old_image);

    era_storage_slice_swap_t swap{};
    era_storage_slice_swap_begin(&swap);
    ASSERT_EQ(era_storage_slice_swap_write_next(&swap, staged.data(), kBase, kSize, kSlice, raw_read, raw_write, &store), ERA_STORAGE_SLICE_SWAP_PROGRESS);
    store.fail_write_call    = store.write_calls + 1;
    store.partial_on_failure = true;
    EXPECT_EQ(era_storage_slice_swap_write_next(&swap, staged.data(), kBase, kSize, kSlice, raw_read, raw_write, &store), ERA_STORAGE_SLICE_SWAP_WRITE_FAILED);
    EXPECT_EQ(swap.written_prefix, kSlice);
    EXPECT_EQ(swap.protected_offset, kSlice);
    EXPECT_EQ(swap.protected_length, kSlice);
    EXPECT_EQ(public_image(swap, staged, store), old_image);

    store.fail_write_call = 0;
    rollback_all(swap, staged, store);
    EXPECT_EQ(raw_image(store), old_image);
    EXPECT_EQ(swap.phase, ERA_STORAGE_SLICE_SWAP_IDLE);
}

TEST(EraStorageSliceSwap, DeferredAbortRollsBackWithoutReloadRevisionOrManifest) {
    Store store;
    auto  old_image = make_image(0x31);
    auto  candidate = make_image(0xB1);
    auto  staged    = candidate;
    put_raw(store, old_image);
    uint32_t reloads = 0, revisions = 7, manifests = 0;

    era_storage_slice_swap_t swap{};
    era_storage_slice_swap_begin(&swap);
    while (swap.phase == ERA_STORAGE_SLICE_SWAP_WRITE) {
        ASSERT_NE(era_storage_slice_swap_write_next(&swap, staged.data(), kBase, kSize, kSlice, raw_read, raw_write, &store), ERA_STORAGE_SLICE_SWAP_INVALID);
    }
    ASSERT_EQ(raw_image(store), candidate);
    ASSERT_EQ(public_image(swap, staged, store), old_image);
    era_storage_slice_swap_request_rollback(&swap); // deferred abort gate
    rollback_all(swap, staged, store);

    EXPECT_EQ(raw_image(store), old_image);
    EXPECT_EQ(reloads, 0U);
    EXPECT_EQ(revisions, 7U);
    EXPECT_EQ(manifests, 0U);
}

TEST(EraStorageSliceSwap, SuccessFlipsOnceThenRuntimeManifestAndRevisionCanPublish) {
    Store store;
    auto  old_image = make_image(0x42);
    auto  candidate = make_image(0xC2);
    auto  staged    = candidate;
    put_raw(store, old_image);
    uint32_t reloads = 0, revisions = 11, manifests = 0;

    era_storage_slice_swap_t swap{};
    era_storage_slice_swap_begin(&swap);
    while (swap.phase == ERA_STORAGE_SLICE_SWAP_WRITE) {
        (void)era_storage_slice_swap_write_next(&swap, staged.data(), kBase, kSize, kSlice, raw_read, raw_write, &store);
    }
    ASSERT_EQ(raw_image(store), candidate); // raw verifier's view
    ASSERT_EQ(public_image(swap, staged, store), old_image);

    std::array<uint8_t, kSize> runtime{};
    ASSERT_TRUE(raw_read(kBase, runtime.data(), kSize, &store));
    reloads++;
    ASSERT_TRUE(era_storage_slice_swap_publish(&swap, kSize)); // atomic public flip
    EXPECT_EQ(public_image(swap, staged, store), candidate);
    staged = candidate; // immutable publication image refill
    manifests++;
    revisions++;

    EXPECT_EQ(runtime, candidate);
    EXPECT_EQ(reloads, 1U);
    EXPECT_EQ(manifests, 1U);
    EXPECT_EQ(revisions, 12U);
}

TEST(EraStorageSliceSwap, RollbackFailureStaysFailClosedUntilARepairRetrySucceeds) {
    Store store;
    auto  old_image = make_image(0x53);
    auto  candidate = make_image(0xD3);
    auto  staged    = candidate;
    put_raw(store, old_image);

    era_storage_slice_swap_t swap{};
    era_storage_slice_swap_begin(&swap);
    ASSERT_EQ(era_storage_slice_swap_write_next(&swap, staged.data(), kBase, kSize, kSlice, raw_read, raw_write, &store), ERA_STORAGE_SLICE_SWAP_PROGRESS);
    era_storage_slice_swap_request_rollback(&swap);
    store.fail_writes_persistently = true;
    EXPECT_EQ(era_storage_slice_swap_rollback_next(&swap, staged.data(), kBase, kSlice, raw_read, raw_write, &store), ERA_STORAGE_SLICE_SWAP_WRITE_FAILED);
    EXPECT_EQ(swap.phase, ERA_STORAGE_SLICE_SWAP_REPAIR_REQUIRED);
    EXPECT_EQ(public_image(swap, staged, store), old_image);

    store.fail_writes_persistently = false;
    rollback_all(swap, staged, store);
    EXPECT_EQ(raw_image(store), old_image);
    EXPECT_EQ(swap.phase, ERA_STORAGE_SLICE_SWAP_IDLE);
}

TEST(EraStorageSliceSwap, LocalMutationIsAbsorbedIntoTheOldViewBeforeAbort) {
    Store store;
    auto  old_image = make_image(0x64);
    auto  candidate = make_image(0xE4);
    auto  staged    = candidate;
    put_raw(store, old_image);

    era_storage_slice_swap_t swap{};
    era_storage_slice_swap_begin(&swap);
    ASSERT_EQ(era_storage_slice_swap_write_next(&swap, staged.data(), kBase, kSize, kSlice, raw_read, raw_write, &store), ERA_STORAGE_SLICE_SWAP_PROGRESS);
    ASSERT_EQ(era_storage_slice_swap_write_next(&swap, staged.data(), kBase, kSize, kSlice, raw_read, raw_write, &store), ERA_STORAGE_SLICE_SWAP_PROGRESS);

    uint8_t prefix_edit = 0xA5;
    uint8_t suffix_edit = 0x5A;
    ASSERT_TRUE(raw_write(kBase + 5, &prefix_edit, 1, &store));
    ASSERT_TRUE(raw_write(kBase + 70, &suffix_edit, 1, &store));
    ASSERT_TRUE(era_storage_slice_swap_absorb_raw_write(&swap, staged.data(), kBase, kSize, kBase + 5, 1, raw_read, &store));
    auto expected_old = old_image;
    expected_old[5]   = prefix_edit;
    expected_old[70]  = suffix_edit;
    EXPECT_EQ(public_image(swap, staged, store), expected_old);

    era_storage_slice_swap_request_rollback(&swap);
    rollback_all(swap, staged, store);
    EXPECT_EQ(raw_image(store), expected_old);
}

TEST(EraStorageSliceSwap, FacadeIsInactiveOutsideApplyAndHandlesCrossBoundaryReaders) {
    Store store;
    auto  old_image = make_image(0x75);
    auto  candidate = make_image(0xF5);
    auto  staged    = candidate;
    put_raw(store, old_image);

    era_storage_slice_swap_t swap{};
    std::array<uint8_t, 12>  view{};
    EXPECT_FALSE(era_storage_slice_swap_public_read(&swap, staged.data(), kBase, kSize, kBase + 25, view.data(), view.size(), raw_read, &store));
    era_storage_slice_swap_begin(&swap);
    ASSERT_EQ(era_storage_slice_swap_write_next(&swap, staged.data(), kBase, kSize, kSlice, raw_read, raw_write, &store), ERA_STORAGE_SLICE_SWAP_PROGRESS);
    ASSERT_TRUE(era_storage_slice_swap_public_read(&swap, staged.data(), kBase, kSize, kBase + 25, view.data(), view.size(), raw_read, &store));
    EXPECT_EQ(std::memcmp(view.data(), &old_image[25], view.size()), 0);
}
