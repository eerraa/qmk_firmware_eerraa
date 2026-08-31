// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/era/common/storage/era_nvm.h"
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

constexpr uint32_t kMacroBase = ERA_NVM_LOGICAL_SIZE_BYTES - ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES;

enum class FaultMode {
    None,
    FailBefore,
    FailPartial,
    FailAfter,
    ReportSuccessNoEffect,
    ReportSuccessMismatch,
};

struct ProgramOp {
    uint32_t offset;
    uint32_t length;
};

class NorFlash {
   public:
    std::array<uint8_t, ERA_NVM_PHYSICAL_SIZE_BYTES> bytes{};
    std::vector<ProgramOp>                            program_ops;
    std::vector<uint32_t>                            erase_ops;

    uint64_t  program_calls = 0;
    uint64_t  erase_calls   = 0;
    uint64_t  mutation_calls = 0;
    uint64_t  fail_program_call = 0;
    uint64_t  fail_erase_call   = 0;
    uint64_t  fail_mutation_call = 0;
    FaultMode program_fault = FaultMode::None;
    FaultMode erase_fault   = FaultMode::None;
    FaultMode mutation_fault = FaultMode::None;
    bool      invalid_program_geometry = false;
    bool      invalid_program_transition = false;
    bool      invalid_erase_geometry = false;

    NorFlash() {
        bytes.fill(0xFF);
    }

    void clear_faults() {
        fail_program_call  = 0;
        fail_erase_call    = 0;
        fail_mutation_call = 0;
        program_fault      = FaultMode::None;
        erase_fault        = FaultMode::None;
        mutation_fault     = FaultMode::None;
    }

    static bool Init(void *) {
        return true;
    }

    static bool Read(void *context, uint32_t offset, void *data, size_t length) {
        auto *self = static_cast<NorFlash *>(context);
        if (length > self->bytes.size() || offset > self->bytes.size() - length) {
            return false;
        }
        std::memcpy(data, self->bytes.data() + offset, length);
        return true;
    }

    static bool Program(void *context, uint32_t offset, const void *data, size_t length) {
        auto *self = static_cast<NorFlash *>(context);
        self->program_calls++;
        self->mutation_calls++;
        self->program_ops.push_back({offset, static_cast<uint32_t>(length)});

        if (length == 0 || length > ERA_NVM_PROGRAM_PAGE_BYTES || offset > self->bytes.size() - length ||
            offset / ERA_NVM_PROGRAM_PAGE_BYTES != (offset + length - 1U) / ERA_NVM_PROGRAM_PAGE_BYTES) {
            self->invalid_program_geometry = true;
            return false;
        }

        auto *source = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < length; ++i) {
            if ((self->bytes[offset + i] & source[i]) != source[i]) {
                self->invalid_program_transition = true;
                return false;
            }
        }

        FaultMode fault = FaultMode::None;
        if (self->fail_program_call != 0 && self->program_calls == self->fail_program_call) {
            fault = self->program_fault;
        }
        if (self->fail_mutation_call != 0 && self->mutation_calls == self->fail_mutation_call) {
            fault = self->mutation_fault;
        }

        if (fault == FaultMode::FailBefore || fault == FaultMode::ReportSuccessNoEffect) {
            return fault == FaultMode::ReportSuccessNoEffect;
        }

        size_t accepted = length;
        if (fault == FaultMode::FailPartial) {
            accepted = std::max<size_t>(1U, length / 2U);
        }
        for (size_t i = 0; i < accepted; ++i) {
            self->bytes[offset + i] &= source[i];
        }

        if (fault == FaultMode::ReportSuccessMismatch) {
            bool changed = false;
            for (size_t i = 0; i < length && !changed; ++i) {
                for (uint8_t bit = 0; bit < 8U; ++bit) {
                    uint8_t mask = static_cast<uint8_t>(1U << bit);
                    if ((source[i] & mask) != 0U) {
                        self->bytes[offset + i] &= static_cast<uint8_t>(~mask);
                        changed = true;
                        break;
                    }
                }
            }
            return true;
        }

        return fault != FaultMode::FailPartial && fault != FaultMode::FailAfter;
    }

    static bool EraseSector(void *context, uint32_t offset) {
        auto *self = static_cast<NorFlash *>(context);
        self->erase_calls++;
        self->mutation_calls++;
        self->erase_ops.push_back(offset);

        if (offset % ERA_NVM_ERASE_SECTOR_BYTES != 0U || offset > self->bytes.size() - ERA_NVM_ERASE_SECTOR_BYTES) {
            self->invalid_erase_geometry = true;
            return false;
        }

        FaultMode fault = FaultMode::None;
        if (self->fail_erase_call != 0 && self->erase_calls == self->fail_erase_call) {
            fault = self->erase_fault;
        }
        if (self->fail_mutation_call != 0 && self->mutation_calls == self->fail_mutation_call) {
            fault = self->mutation_fault;
        }

        if (fault == FaultMode::FailBefore) {
            return false;
        }
        size_t accepted = ERA_NVM_ERASE_SECTOR_BYTES;
        if (fault == FaultMode::FailPartial) {
            accepted /= 2U;
        }
        std::memset(self->bytes.data() + offset, 0xFF, accepted);

        if (fault == FaultMode::ReportSuccessMismatch) {
            self->bytes[offset] = 0x00;
            return true;
        }
        return fault != FaultMode::FailPartial && fault != FaultMode::FailAfter;
    }

    era_nvm_flash_t interface() {
        return era_nvm_flash_t{
            .context      = this,
            .init         = Init,
            .read         = Read,
            .program      = Program,
            .erase_sector = EraseSector,
        };
    }
};

class Rig {
   public:
    NorFlash flash;
    era_nvm_t nvm{};

    era_nvm_result_t mount() {
        auto flash_if = flash.interface();
        era_nvm_config_t config{
            .macro_address   = kMacroBase,
            .macro_size      = ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES,
            .commit_notifier = nullptr,
            .commit_context  = nullptr,
        };
        era_nvm_setup(&nvm, &flash_if, &config);
        return era_nvm_mount(&nvm);
    }

    void expect_geometry_clean() const {
        EXPECT_FALSE(flash.invalid_program_geometry);
        EXPECT_FALSE(flash.invalid_program_transition);
        EXPECT_FALSE(flash.invalid_erase_geometry);
        for (const auto &op : flash.program_ops) {
            ASSERT_GT(op.length, 0U);
            EXPECT_LE(op.length, ERA_NVM_PROGRAM_PAGE_BYTES);
            EXPECT_EQ(op.offset / ERA_NVM_PROGRAM_PAGE_BYTES, (op.offset + op.length - 1U) / ERA_NVM_PROGRAM_PAGE_BYTES);
        }
    }
};

std::array<uint8_t, ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES> make_large(uint8_t seed) {
    std::array<uint8_t, ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES> data{};
    for (uint32_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(seed + i * 29U);
    }
    return data;
}

era_nvm_result_t stock_qmk_update_block(Rig &rig, uint32_t address, const uint8_t *data, size_t length) {
    std::vector<uint8_t> current(length);
    era_nvm_result_t     result = era_nvm_qmk_read(&rig.nvm, address, current.data(), current.size());
    if (result != ERA_NVM_RESULT_OK) {
        return result;
    }
    if (std::memcmp(current.data(), data, length) == 0) {
        return ERA_NVM_RESULT_NO_CHANGE;
    }
    return era_nvm_qmk_write(&rig.nvm, address, data, length);
}

void fill_two_large_records(Rig &rig) {
    auto a = make_large(0x11);
    auto b = make_large(0x42);
    ASSERT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, a.data(), a.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, b.data(), b.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_nvm_generation(&rig.nvm), 1U);
}

uint32_t active_cursor(const era_nvm_t &nvm) {
    return ERA_NVM_BANK_SIZE_BYTES - era_nvm_journal_free_bytes(&nvm);
}

uint32_t absolute_active_cursor(const era_nvm_t &nvm) {
    return static_cast<uint32_t>(era_nvm_active_bank(&nvm)) * ERA_NVM_BANK_SIZE_BYTES + active_cursor(nvm);
}

} // namespace

TEST(EraNvm, BlankFlashFormatsFreshBankAndReplayUsesSameImage) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    EXPECT_EQ(era_nvm_state(&rig.nvm), ERA_NVM_STATE_READY);
    EXPECT_EQ(era_nvm_active_bank(&rig.nvm), 0U);
    EXPECT_EQ(era_nvm_generation(&rig.nvm), 1U);

    std::array<uint8_t, ERA_NVM_LOGICAL_SIZE_BYTES> live{};
    std::array<uint8_t, ERA_NVM_LOGICAL_SIZE_BYTES> replay{};
    ASSERT_EQ(era_nvm_read(&rig.nvm, 0U, live.data(), live.size()), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_nvm_replay_read(&rig.nvm, 0U, replay.data(), replay.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(live, replay);
    EXPECT_TRUE(std::all_of(live.begin(), live.end(), [](uint8_t byte) { return byte == 0U; }));
    rig.expect_geometry_clean();
}

TEST(EraNvm, NewestValidBankWinsAfterRotation) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    fill_two_large_records(rig);
    auto newest = make_large(0x73);
    ASSERT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, newest.data(), newest.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_nvm_active_bank(&rig.nvm), 1U);
    ASSERT_EQ(era_nvm_generation(&rig.nvm), 2U);

    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    EXPECT_EQ(era_nvm_active_bank(&rig.nvm), 1U);
    EXPECT_EQ(era_nvm_generation(&rig.nvm), 2U);
    std::array<uint8_t, ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES> actual{};
    ASSERT_EQ(era_nvm_read(&rig.nvm, kMacroBase, actual.data(), actual.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(actual, newest);
}

TEST(EraNvm, TornNewerBankNeverDisplacesOlderAuthority) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    fill_two_large_records(rig);
    auto old_image = make_large(0x42);
    auto candidate = make_large(0x75);

    const uint64_t before = rig.flash.program_calls;
    rig.flash.fail_program_call = before + 99U; // activation COMMIT of a blank inactive bank
    rig.flash.program_fault     = FaultMode::FailPartial;
    EXPECT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, candidate.data(), candidate.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_IO_ERROR);
    EXPECT_EQ(era_nvm_active_bank(&rig.nvm), 0U);
    EXPECT_EQ(era_nvm_generation(&rig.nvm), 1U);

    rig.flash.clear_faults();
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    EXPECT_EQ(era_nvm_active_bank(&rig.nvm), 0U);
    std::array<uint8_t, ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES> actual{};
    ASSERT_EQ(era_nvm_read(&rig.nvm, kMacroBase, actual.data(), actual.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(actual, old_image);
}

TEST(EraNvm, SmallAndOverlappingWritesReplayExactly) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    const std::array<uint8_t, 7> first{1, 2, 3, 4, 5, 6, 7};
    const std::array<uint8_t, 4> second{0xA1, 0xA2, 0xA3, 0xA4};
    ASSERT_EQ(era_nvm_replace(&rig.nvm, 101U, first.data(), first.size(), ERA_NVM_ORIGIN_LOCAL_QMK), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_nvm_replace(&rig.nvm, 104U, second.data(), second.size(), ERA_NVM_ORIGIN_LOCAL_QMK), ERA_NVM_RESULT_OK);

    std::array<uint8_t, 7> expected{1, 2, 3, 0xA1, 0xA2, 0xA3, 0xA4};
    std::array<uint8_t, 7> live{};
    std::array<uint8_t, 7> replay{};
    ASSERT_EQ(era_nvm_read(&rig.nvm, 101U, live.data(), live.size()), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_nvm_replay_read(&rig.nvm, 101U, replay.data(), replay.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(live, expected);
    EXPECT_EQ(replay, expected);
}

TEST(EraNvm, PhysicalProgramsSplitAtEvery256ByteBoundary) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    rig.flash.program_ops.clear();
    std::array<uint8_t, 700> data{};
    for (uint32_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i + 3U);
    ASSERT_EQ(era_nvm_replace(&rig.nvm, 17U, data.data(), data.size(), ERA_NVM_ORIGIN_LOCAL_QMK), ERA_NVM_RESULT_OK);
    rig.expect_geometry_clean();
    EXPECT_GE(rig.flash.program_ops.size(), 6U); // header + split payload + trailer + commit
}

TEST(EraNvm, LogicalBoundsAreExactly24KiB) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    uint8_t byte = 0x5A;
    EXPECT_EQ(era_nvm_replace(&rig.nvm, ERA_NVM_LOGICAL_SIZE_BYTES - 1U, &byte, 1U, ERA_NVM_ORIGIN_CLEAN_PREPARE), ERA_NVM_RESULT_OK);
    EXPECT_EQ(era_nvm_replace(&rig.nvm, ERA_NVM_LOGICAL_SIZE_BYTES, &byte, 1U, ERA_NVM_ORIGIN_CLEAN_PREPARE), ERA_NVM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(era_nvm_replace(&rig.nvm, ERA_NVM_LOGICAL_SIZE_BYTES - 1U, &byte, 2U, ERA_NVM_ORIGIN_CLEAN_PREPARE), ERA_NVM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(era_nvm_read(&rig.nvm, ERA_NVM_LOGICAL_SIZE_BYTES, &byte, 0U), ERA_NVM_RESULT_OK);
}

TEST(EraNvm, SixteenKiBReplaceIsOneAtomicPageScaleRecord) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    auto candidate = make_large(0x39);
    const uint64_t programs_before = rig.flash.program_calls;
    ASSERT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, candidate.data(), candidate.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_OK);
    const uint64_t program_count = rig.flash.program_calls - programs_before;
    std::cout << "ERA_NVM_16K_PROGRAM_OPS=" << program_count << std::endl;
    EXPECT_EQ(program_count, 68U);
    rig.expect_geometry_clean();

    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    std::array<uint8_t, ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES> replay{};
    ASSERT_EQ(era_nvm_read(&rig.nvm, kMacroBase, replay.data(), replay.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(replay, candidate);
}

TEST(EraNvm, JournalFullPreflightsWholeTransactionAndRotates) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    fill_two_large_records(rig);
    const uint32_t old_cursor = active_cursor(rig.nvm);
    ASSERT_LT(era_nvm_journal_free_bytes(&rig.nvm), static_cast<uint32_t>(sizeof(era_nvm_record_header_t) + ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES + sizeof(era_nvm_record_trailer_t)));
    for (uint32_t i = old_cursor; i < ERA_NVM_BANK_SIZE_BYTES; ++i) {
        ASSERT_EQ(rig.flash.bytes[i], 0xFFU) << i;
    }

    auto candidate = make_large(0x99);
    ASSERT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, candidate.data(), candidate.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_OK);
    EXPECT_EQ(era_nvm_generation(&rig.nvm), 2U);
    EXPECT_EQ(era_nvm_active_bank(&rig.nvm), 1U);
    for (uint32_t i = old_cursor; i < ERA_NVM_BANK_SIZE_BYTES; ++i) {
        EXPECT_EQ(rig.flash.bytes[i], 0xFFU) << i;
    }
}

TEST(EraNvm, InactiveBankEraseAdvancesOneSectorAndResumesAfterMount) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    fill_two_large_records(rig);
    auto candidate = make_large(0x71);
    ASSERT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, candidate.data(), candidate.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_nvm_active_bank(&rig.nvm), 1U);

    const size_t erases_before = rig.flash.erase_ops.size();
    bool did_work = false;
    ASSERT_EQ(era_nvm_maintenance_erase_one_sector(&rig.nvm, &did_work), ERA_NVM_RESULT_OK);
    ASSERT_TRUE(did_work);
    ASSERT_EQ(rig.flash.erase_ops.size(), erases_before + 1U);
    EXPECT_EQ(rig.flash.erase_ops.back(), 0U);

    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_nvm_maintenance_erase_one_sector(&rig.nvm, &did_work), ERA_NVM_RESULT_OK);
    ASSERT_TRUE(did_work);
    EXPECT_EQ(rig.flash.erase_ops.back(), ERA_NVM_ERASE_SECTOR_BYTES);
}

TEST(EraNvm, BankConstructionFaultsBeforeActivationPreserveOldActiveBank) {
    Rig base;
    ASSERT_EQ(base.mount(), ERA_NVM_RESULT_OK);
    fill_two_large_records(base);
    auto old_image = make_large(0x42);
    auto candidate = make_large(0x7E);

    const std::array<uint64_t, 4> relative_program_faults{1U, 20U, 98U, 99U}; // metadata, snapshot, activation body, activation commit
    for (uint64_t relative : relative_program_faults) {
        Rig attempt;
        attempt.flash = base.flash;
        ASSERT_EQ(attempt.mount(), ERA_NVM_RESULT_OK);
        const uint64_t before = attempt.flash.program_calls;
        attempt.flash.fail_program_call = before + relative;
        attempt.flash.program_fault     = FaultMode::FailPartial;
        EXPECT_EQ(era_nvm_replace(&attempt.nvm, kMacroBase, candidate.data(), candidate.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_IO_ERROR) << relative;
        EXPECT_EQ(era_nvm_active_bank(&attempt.nvm), 0U) << relative;

        attempt.flash.clear_faults();
        ASSERT_EQ(attempt.mount(), ERA_NVM_RESULT_OK);
        EXPECT_EQ(era_nvm_active_bank(&attempt.nvm), 0U) << relative;
        std::array<uint8_t, ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES> actual{};
        ASSERT_EQ(era_nvm_read(&attempt.nvm, kMacroBase, actual.data(), actual.size()), ERA_NVM_RESULT_OK);
        EXPECT_EQ(actual, old_image) << relative;
    }
}

TEST(EraNvm, ExactActivationCommitCanBecomeAuthorityEvenIfCallerLosesResult) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    fill_two_large_records(rig);
    auto candidate = make_large(0x6C);
    const uint64_t before = rig.flash.program_calls;
    rig.flash.fail_program_call = before + 99U;
    rig.flash.program_fault     = FaultMode::FailAfter;
    EXPECT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, candidate.data(), candidate.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_IO_ERROR);
    EXPECT_EQ(era_nvm_active_bank(&rig.nvm), 0U); // live call never published

    rig.flash.clear_faults();
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    EXPECT_EQ(era_nvm_active_bank(&rig.nvm), 1U); // physical COMMIT is exact
    std::array<uint8_t, ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES> actual{};
    ASSERT_EQ(era_nvm_read(&rig.nvm, kMacroBase, actual.data(), actual.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(actual, candidate);
}

TEST(EraNvm, EveryAppendPhaseFaultLeavesPreviousImageReplayable) {
    Rig base;
    ASSERT_EQ(base.mount(), ERA_NVM_RESULT_OK);
    const uint8_t old_value = 0x21;
    ASSERT_EQ(era_nvm_replace(&base.nvm, 77U, &old_value, 1U, ERA_NVM_ORIGIN_LOCAL_QMK), ERA_NVM_RESULT_OK);
    const uint8_t candidate = 0xA5;

    for (uint64_t relative = 1U; relative <= 4U; ++relative) { // header, payload, trailer integrity, final commit
        Rig attempt;
        attempt.flash = base.flash;
        ASSERT_EQ(attempt.mount(), ERA_NVM_RESULT_OK);
        const uint64_t before = attempt.flash.program_calls;
        attempt.flash.fail_program_call = before + relative;
        attempt.flash.program_fault     = FaultMode::FailPartial;
        EXPECT_EQ(era_nvm_replace(&attempt.nvm, 77U, &candidate, 1U, ERA_NVM_ORIGIN_LOCAL_QMK), ERA_NVM_RESULT_IO_ERROR) << relative;
        EXPECT_TRUE(era_nvm_tail_is_sealed(&attempt.nvm)) << relative;
        uint8_t live = 0;
        ASSERT_EQ(era_nvm_read(&attempt.nvm, 77U, &live, 1U), ERA_NVM_RESULT_OK);
        EXPECT_EQ(live, old_value) << relative;

        attempt.flash.clear_faults();
        ASSERT_EQ(attempt.mount(), ERA_NVM_RESULT_OK);
        uint8_t replay = 0;
        ASSERT_EQ(era_nvm_read(&attempt.nvm, 77U, &replay, 1U), ERA_NVM_RESULT_OK);
        EXPECT_EQ(replay, old_value) << relative;
    }
}

TEST(EraNvm, CommitIsAbsentUntilHeaderPayloadAndIntegrityAreComplete) {
    Rig base;
    ASSERT_EQ(base.mount(), ERA_NVM_RESULT_OK);
    std::array<uint8_t, 32> candidate{};
    candidate.fill(0xA6);
    const uint32_t record_start = absolute_active_cursor(base.nvm);
    const uint32_t trailer       = record_start + ((sizeof(era_nvm_record_header_t) + candidate.size() + 3U) & ~3U);
    const uint32_t commit        = trailer + offsetof(era_nvm_record_trailer_t, commit);

    for (uint64_t relative = 1U; relative <= 3U; ++relative) {
        Rig attempt;
        attempt.flash = base.flash;
        ASSERT_EQ(attempt.mount(), ERA_NVM_RESULT_OK);
        const uint64_t before = attempt.flash.program_calls;
        attempt.flash.fail_program_call = before + relative;
        attempt.flash.program_fault     = FaultMode::FailPartial;
        EXPECT_EQ(era_nvm_replace(&attempt.nvm, 44U, candidate.data(), candidate.size(), ERA_NVM_ORIGIN_LOCAL_QMK), ERA_NVM_RESULT_IO_ERROR);
        uint32_t physical_commit = 0;
        std::memcpy(&physical_commit, attempt.flash.bytes.data() + commit, sizeof(physical_commit));
        EXPECT_EQ(physical_commit, 0xFFFFFFFFUL) << relative;
    }
}

TEST(EraNvm, TornAppendSealsTailAndNextDurableWriteRotates) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    const uint8_t old_value = 0x2C;
    ASSERT_EQ(era_nvm_replace(&rig.nvm, 51U, &old_value, 1U, ERA_NVM_ORIGIN_LOCAL_QMK), ERA_NVM_RESULT_OK);

    uint8_t failed = 0xD1;
    rig.flash.fail_program_call = rig.flash.program_calls + 2U; // payload
    rig.flash.program_fault     = FaultMode::FailPartial;
    ASSERT_EQ(era_nvm_replace(&rig.nvm, 51U, &failed, 1U, ERA_NVM_ORIGIN_LOCAL_QMK), ERA_NVM_RESULT_IO_ERROR);
    ASSERT_TRUE(era_nvm_tail_is_sealed(&rig.nvm));

    rig.flash.clear_faults();
    uint8_t next = 0xE7;
    ASSERT_EQ(era_nvm_replace(&rig.nvm, 52U, &next, 1U, ERA_NVM_ORIGIN_LOCAL_QMK), ERA_NVM_RESULT_OK);
    EXPECT_EQ(era_nvm_generation(&rig.nvm), 2U);
    EXPECT_EQ(era_nvm_active_bank(&rig.nvm), 1U);

    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    uint8_t values[2]{};
    ASSERT_EQ(era_nvm_read(&rig.nvm, 51U, values, sizeof(values)), ERA_NVM_RESULT_OK);
    EXPECT_EQ(values[0], old_value);
    EXPECT_EQ(values[1], next);
}

TEST(EraNvm, ProgramReadbackMismatchFailsAndSealsTail) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    uint8_t candidate = 0xB7;
    rig.flash.fail_program_call = rig.flash.program_calls + 1U;
    rig.flash.program_fault     = FaultMode::ReportSuccessMismatch;
    EXPECT_EQ(era_nvm_replace(&rig.nvm, 99U, &candidate, 1U, ERA_NVM_ORIGIN_LOCAL_QMK), ERA_NVM_RESULT_IO_ERROR);
    EXPECT_TRUE(era_nvm_tail_is_sealed(&rig.nvm));
}

TEST(EraNvm, EraseVerificationMismatchDoesNotTouchActiveAuthority) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    fill_two_large_records(rig);
    auto candidate = make_large(0x61);
    ASSERT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, candidate.data(), candidate.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_nvm_active_bank(&rig.nvm), 1U);

    rig.flash.fail_erase_call = rig.flash.erase_calls + 1U;
    rig.flash.erase_fault     = FaultMode::ReportSuccessMismatch;
    bool did_work = true;
    EXPECT_EQ(era_nvm_maintenance_erase_one_sector(&rig.nvm, &did_work), ERA_NVM_RESULT_IO_ERROR);
    EXPECT_FALSE(did_work);
    EXPECT_EQ(era_nvm_active_bank(&rig.nvm), 1U);
    EXPECT_EQ(era_nvm_generation(&rig.nvm), 2U);
}

TEST(EraNvm, FailedMandatoryInactiveEraseLeavesOldActiveAuthoritative) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    fill_two_large_records(rig);
    auto first_rotation = make_large(0x70);
    ASSERT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, first_rotation.data(), first_rotation.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_nvm_active_bank(&rig.nvm), 1U);
    auto b1 = make_large(0x80);
    auto b2 = make_large(0x90);
    ASSERT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, b1.data(), b1.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, b2.data(), b2.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_OK);

    rig.flash.fail_erase_call = rig.flash.erase_calls + 1U;
    rig.flash.erase_fault     = FaultMode::FailPartial;
    auto candidate = make_large(0xA0);
    EXPECT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, candidate.data(), candidate.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_IO_ERROR);
    EXPECT_EQ(era_nvm_active_bank(&rig.nvm), 1U);

    rig.flash.clear_faults();
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    EXPECT_EQ(era_nvm_active_bank(&rig.nvm), 1U);
    std::array<uint8_t, ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES> actual{};
    ASSERT_EQ(era_nvm_read(&rig.nvm, kMacroBase, actual.data(), actual.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(actual, b2);
}

TEST(EraNvm, MacroOpenPayloadCloseDurablyPublishesOnlyAtFinalZero) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    const uint32_t marker = kMacroBase + ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES - 1U;
    uint8_t opener = 0xFF;
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &opener, 1U), ERA_NVM_RESULT_STAGED);
    uint8_t public_marker = 0;
    ASSERT_EQ(era_nvm_read(&rig.nvm, marker, &public_marker, 1U), ERA_NVM_RESULT_OK);
    EXPECT_NE(public_marker, 0U);

    const std::array<uint8_t, 6> payload{1, 3, 5, 7, 9, 11};
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, kMacroBase, payload.data(), payload.size()), ERA_NVM_RESULT_STAGED);
    uint8_t durable_before = 0xFF;
    ASSERT_EQ(era_nvm_replay_read(&rig.nvm, kMacroBase, &durable_before, 1U), ERA_NVM_RESULT_OK);
    EXPECT_EQ(durable_before, 0U);

    uint8_t close = 0U;
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &close, 1U), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_nvm_read(&rig.nvm, marker, &public_marker, 1U), ERA_NVM_RESULT_OK);
    EXPECT_EQ(public_marker, 0U);

    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    std::array<uint8_t, payload.size()> actual{};
    ASSERT_EQ(era_nvm_read(&rig.nvm, kMacroBase, actual.data(), actual.size()), ERA_NVM_RESULT_OK);
    EXPECT_TRUE(std::equal(actual.begin(), actual.end(), payload.begin()));
}

TEST(EraNvm, MacroPayloadAndZeroWithoutOpenerAreRefused) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    const uint32_t marker = kMacroBase + ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES - 1U;
    uint8_t payload = 0x55;
    EXPECT_EQ(era_nvm_qmk_write(&rig.nvm, kMacroBase + 12U, &payload, 1U), ERA_NVM_RESULT_PROTOCOL);
    uint8_t zero = 0U;
    EXPECT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &zero, 1U), ERA_NVM_RESULT_PROTOCOL);
    std::array<uint8_t, 16> zero_block{};
    EXPECT_EQ(era_nvm_qmk_write(&rig.nvm, kMacroBase, zero_block.data(), zero_block.size()), ERA_NVM_RESULT_PROTOCOL);
    uint8_t actual = 0xFF;
    ASSERT_EQ(era_nvm_read(&rig.nvm, kMacroBase + 12U, &actual, 1U), ERA_NVM_RESULT_OK);
    EXPECT_EQ(actual, 0U);
}

TEST(EraNvm, MacroZeroBeforePayloadStaysInvalidAndCanLaterComplete) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    const uint32_t marker = kMacroBase + ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES - 1U;
    uint8_t opener = 0xFF;
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &opener, 1U), ERA_NVM_RESULT_STAGED);
    uint8_t zero = 0U;
    EXPECT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &zero, 1U), ERA_NVM_RESULT_PROTOCOL);
    uint8_t marker_read = 0U;
    ASSERT_EQ(era_nvm_read(&rig.nvm, marker, &marker_read, 1U), ERA_NVM_RESULT_OK);
    EXPECT_NE(marker_read, 0U);

    uint8_t payload = 0x6A;
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, kMacroBase, &payload, 1U), ERA_NVM_RESULT_STAGED);
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &zero, 1U), ERA_NVM_RESULT_OK);
}

TEST(EraNvm, MacroFinalCommitFailureLeavesMarkerInvalidAndOldDurableImageRecoverable) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    const uint32_t marker = kMacroBase + ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES - 1U;
    uint8_t opener = 0xFF;
    uint8_t payload = 0x7B;
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &opener, 1U), ERA_NVM_RESULT_STAGED);
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, kMacroBase, &payload, 1U), ERA_NVM_RESULT_STAGED);

    const uint32_t record_start  = absolute_active_cursor(rig.nvm);
    const uint32_t trailer       = record_start + ((sizeof(era_nvm_record_header_t) + ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES + 3U) & ~3U);
    const uint32_t commit_offset = trailer + offsetof(era_nvm_record_trailer_t, commit);
    const uint64_t programs_before = rig.flash.program_calls;
    (void)programs_before;
    // The final callback for a 16-KiB append is the four-byte commit authority.
    rig.flash.fail_program_call = rig.flash.program_calls + 68U;
    rig.flash.program_fault     = FaultMode::FailBefore;
    uint8_t close = 0U;
    EXPECT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &close, 1U), ERA_NVM_RESULT_IO_ERROR);
    uint8_t marker_read = 0U;
    ASSERT_EQ(era_nvm_read(&rig.nvm, marker, &marker_read, 1U), ERA_NVM_RESULT_OK);
    EXPECT_NE(marker_read, 0U);
    uint32_t physical_commit = 0;
    std::memcpy(&physical_commit, rig.flash.bytes.data() + commit_offset, sizeof(physical_commit));
    EXPECT_EQ(physical_commit, 0xFFFFFFFFUL);

    rig.flash.clear_faults();
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    uint8_t durable_payload = 0xFF;
    ASSERT_EQ(era_nvm_read(&rig.nvm, kMacroBase, &durable_payload, 1U), ERA_NVM_RESULT_OK);
    EXPECT_EQ(durable_payload, 0U);
}

TEST(EraNvm, StockStyleMacroResetStagesChunksAndCommitsWholeDomainOnce) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    const uint32_t marker = kMacroBase + ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES - 1U;
    uint8_t opener = 0xFF;
    uint8_t payload = 0xD3;
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &opener, 1U), ERA_NVM_RESULT_STAGED);
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, kMacroBase + 1U, &payload, 1U), ERA_NVM_RESULT_STAGED);
    uint8_t zero = 0U;
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &zero, 1U), ERA_NVM_RESULT_OK);

    const uint64_t programs_before_reset = rig.flash.program_calls;
    std::array<uint8_t, 16> zeros{};
    uint32_t skipped_writes = 0U;
    for (uint32_t offset = 0U; offset < ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES; offset += zeros.size()) {
        era_nvm_result_t result = stock_qmk_update_block(rig, kMacroBase + offset, zeros.data(), zeros.size());
        if (result == ERA_NVM_RESULT_NO_CHANGE) {
            skipped_writes++;
        } else if (offset + zeros.size() < ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES) {
            ASSERT_EQ(result, ERA_NVM_RESULT_STAGED) << offset;
        } else {
            ASSERT_EQ(result, ERA_NVM_RESULT_OK) << offset;
        }
        if (offset > 0U && offset + zeros.size() < ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES) {
            uint8_t marker_read = 0U;
            ASSERT_EQ(era_nvm_read(&rig.nvm, marker, &marker_read, 1U), ERA_NVM_RESULT_OK);
            ASSERT_NE(marker_read, 0U) << offset;
        }
    }
    EXPECT_GT(skipped_writes, 1000U); // generic eeprom_update_block skipped almost every zero chunk
    EXPECT_EQ(rig.flash.program_calls - programs_before_reset, 68U);

    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    std::array<uint8_t, ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES> actual{};
    ASSERT_EQ(era_nvm_read(&rig.nvm, kMacroBase, actual.data(), actual.size()), ERA_NVM_RESULT_OK);
    EXPECT_TRUE(std::all_of(actual.begin(), actual.end(), [](uint8_t value) { return value == 0U; }));
}

TEST(EraNvm, StockStyleMacroResetCanSkipEveryWriteUntilFinalChunk) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    const uint32_t marker = kMacroBase + ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES - 1U;
    uint8_t opener = 0xFF;
    uint8_t payload = 0xD5;
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &opener, 1U), ERA_NVM_RESULT_STAGED);
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, marker - 1U, &payload, 1U), ERA_NVM_RESULT_STAGED);
    uint8_t close = 0U;
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &close, 1U), ERA_NVM_RESULT_OK);

    const uint64_t programs_before_reset = rig.flash.program_calls;
    std::array<uint8_t, 16> zeros{};
    uint32_t skipped_writes = 0U;
    for (uint32_t offset = 0U; offset < ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES; offset += zeros.size()) {
        era_nvm_result_t result = stock_qmk_update_block(rig, kMacroBase + offset, zeros.data(), zeros.size());
        if (offset + zeros.size() < ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES) {
            ASSERT_EQ(result, ERA_NVM_RESULT_NO_CHANGE) << offset;
            skipped_writes++;
        } else {
            ASSERT_EQ(result, ERA_NVM_RESULT_OK);
        }
    }
    EXPECT_EQ(skipped_writes, ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES / zeros.size() - 1U);
    EXPECT_EQ(rig.flash.program_calls - programs_before_reset, 68U);

    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    std::array<uint8_t, ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES> actual{};
    ASSERT_EQ(era_nvm_read(&rig.nvm, kMacroBase, actual.data(), actual.size()), ERA_NVM_RESULT_OK);
    EXPECT_TRUE(std::all_of(actual.begin(), actual.end(), [](uint8_t value) { return value == 0U; }));
}

TEST(EraNvm, StockStyleMacroResetAlreadyZeroNeedsNoDurableWrite) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    const uint64_t programs_before_reset = rig.flash.program_calls;
    std::array<uint8_t, 16> zeros{};
    for (uint32_t offset = 0U; offset < ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES; offset += zeros.size()) {
        EXPECT_EQ(stock_qmk_update_block(rig, kMacroBase + offset, zeros.data(), zeros.size()), ERA_NVM_RESULT_NO_CHANGE) << offset;
    }
    EXPECT_EQ(rig.flash.program_calls, programs_before_reset);
}

TEST(EraNvm, CleanStyleWriteIsProvedByProductionReplayParser) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    const uint16_t on  = 0xBEEF;
    const uint16_t off = 0x0000;
    ASSERT_EQ(era_nvm_replace(&rig.nvm, 0U, &on, sizeof(on), ERA_NVM_ORIGIN_LOCAL_QMK), ERA_NVM_RESULT_OK);
    ASSERT_EQ(era_nvm_replace(&rig.nvm, 0U, &off, sizeof(off), ERA_NVM_ORIGIN_CLEAN_PREPARE), ERA_NVM_RESULT_OK);
    uint16_t replay = 0xFFFF;
    ASSERT_EQ(era_nvm_replay_read(&rig.nvm, 0U, &replay, sizeof(replay)), ERA_NVM_RESULT_OK);
    EXPECT_EQ(replay, off);
}

TEST(EraNvm, OldWearLevelLookingTailIsNeverParsedOrMigrated) {
    Rig rig;
    constexpr uint32_t old_wear_offset = ERA_NVM_PHYSICAL_SIZE_BYTES - (48U * 1024U);
    for (uint32_t i = old_wear_offset; i < ERA_NVM_PHYSICAL_SIZE_BYTES; ++i) {
        rig.flash.bytes[i] = static_cast<uint8_t>((i * 37U) ^ 0xA5U);
    }
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    EXPECT_EQ(era_nvm_active_bank(&rig.nvm), 0U);
    EXPECT_EQ(era_nvm_generation(&rig.nvm), 1U);
    std::array<uint8_t, 64> logical{};
    ASSERT_EQ(era_nvm_read(&rig.nvm, 0U, logical.data(), logical.size()), ERA_NVM_RESULT_OK);
    EXPECT_TRUE(std::all_of(logical.begin(), logical.end(), [](uint8_t byte) { return byte == 0U; }));
}

TEST(EraNvm, BoundedPowerCutsAtEverySmallAppendProgramBoundaryReplayOld) {
    Rig base;
    ASSERT_EQ(base.mount(), ERA_NVM_RESULT_OK);
    const uint8_t old_value = 0x14;
    ASSERT_EQ(era_nvm_replace(&base.nvm, 333U, &old_value, 1U, ERA_NVM_ORIGIN_LOCAL_QMK), ERA_NVM_RESULT_OK);

    for (uint64_t cut = 1U; cut <= 4U; ++cut) {
        Rig attempt;
        attempt.flash = base.flash;
        ASSERT_EQ(attempt.mount(), ERA_NVM_RESULT_OK);
        attempt.flash.fail_program_call = attempt.flash.program_calls + cut;
        attempt.flash.program_fault     = FaultMode::FailPartial;
        uint8_t candidate = 0xE9;
        EXPECT_EQ(era_nvm_replace(&attempt.nvm, 333U, &candidate, 1U, ERA_NVM_ORIGIN_LOCAL_QMK), ERA_NVM_RESULT_IO_ERROR) << cut;
        attempt.flash.clear_faults();
        ASSERT_EQ(attempt.mount(), ERA_NVM_RESULT_OK);
        uint8_t replay = 0;
        ASSERT_EQ(era_nvm_read(&attempt.nvm, 333U, &replay, 1U), ERA_NVM_RESULT_OK);
        EXPECT_EQ(replay, old_value) << cut;
    }
}

TEST(EraNvm, BoundedPowerCutsAcrossEveryNewBankMutationKeepOldBankUnlessActivationCommitted) {
    Rig base;
    ASSERT_EQ(base.mount(), ERA_NVM_RESULT_OK);
    fill_two_large_records(base);
    auto old_image = make_large(0x42);
    auto candidate = make_large(0xC1);

    // Inactive bank B is erased, so a rotation is exactly 99 program mutations.
    for (uint64_t cut = 1U; cut <= 99U; ++cut) {
        Rig attempt;
        attempt.flash = base.flash;
        ASSERT_EQ(attempt.mount(), ERA_NVM_RESULT_OK);
        attempt.flash.fail_mutation_call = attempt.flash.mutation_calls + cut;
        attempt.flash.mutation_fault     = FaultMode::FailPartial;
        EXPECT_EQ(era_nvm_replace(&attempt.nvm, kMacroBase, candidate.data(), candidate.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_IO_ERROR) << cut;
        attempt.flash.clear_faults();
        ASSERT_EQ(attempt.mount(), ERA_NVM_RESULT_OK);
        EXPECT_EQ(era_nvm_active_bank(&attempt.nvm), 0U) << cut;
        std::array<uint8_t, ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES> replay{};
        ASSERT_EQ(era_nvm_read(&attempt.nvm, kMacroBase, replay.data(), replay.size()), ERA_NVM_RESULT_OK);
        EXPECT_EQ(replay, old_image) << cut;
    }
}

// The keyboard writes EEPROM during a macro upload even though VIA does not.
// QK_RGB_MATRIX_TOGGLE reaches eeconfig_update_rgb_matrix(), which this fork
// defers and then flushes from housekeeping - unattended, inside the transfer,
// and with its dirty flag already consumed so nothing retries a refusal.
TEST(EraNvm, NonMacroWriteStaysDurableWhileAMacroIsStaged) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    const uint32_t marker = kMacroBase + ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES - 1U;
    constexpr uint32_t kRgbMatrixAddress = 23U; // EECONFIG_RGB_MATRIX
    const std::array<uint8_t, 8> before{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
    const std::array<uint8_t, 8> toggled{0xA5, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, kRgbMatrixAddress, before.data(), before.size()), ERA_NVM_RESULT_OK);

    uint8_t opener = 0xFF;
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &opener, 1U), ERA_NVM_RESULT_STAGED);
    const std::array<uint8_t, 6> payload{2, 4, 6, 8, 10, 12};
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, kMacroBase, payload.data(), payload.size()), ERA_NVM_RESULT_STAGED);

    EXPECT_EQ(era_nvm_qmk_write(&rig.nvm, kRgbMatrixAddress, toggled.data(), toggled.size()), ERA_NVM_RESULT_OK);
    std::array<uint8_t, 8> durable{};
    ASSERT_EQ(era_nvm_replay_read(&rig.nvm, kRgbMatrixAddress, durable.data(), durable.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(durable, toggled);

    // The upload is unaffected and still publishes only at its own close.
    uint8_t staged_marker = 0;
    ASSERT_EQ(era_nvm_read(&rig.nvm, marker, &staged_marker, 1U), ERA_NVM_RESULT_OK);
    EXPECT_NE(staged_marker, 0U);
    uint8_t close = 0U;
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &close, 1U), ERA_NVM_RESULT_OK);

    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    std::array<uint8_t, 8> after_boot{};
    ASSERT_EQ(era_nvm_read(&rig.nvm, kRgbMatrixAddress, after_boot.data(), after_boot.size()), ERA_NVM_RESULT_OK);
    EXPECT_EQ(after_boot, toggled);
    std::array<uint8_t, payload.size()> macro_after{};
    ASSERT_EQ(era_nvm_read(&rig.nvm, kMacroBase, macro_after.data(), macro_after.size()), ERA_NVM_RESULT_OK);
    EXPECT_TRUE(std::equal(macro_after.begin(), macro_after.end(), payload.begin()));
    rig.expect_geometry_clean();
}

// The scope is the rule: only a range that touches the staged domain is refused.
TEST(EraNvm, MacroDomainReplaceIsStillRefusedWhileStaged) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    const uint32_t marker = kMacroBase + ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES - 1U;
    uint8_t opener = 0xFF;
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, marker, &opener, 1U), ERA_NVM_RESULT_STAGED);
    uint8_t payload = 0x33;
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, kMacroBase, &payload, 1U), ERA_NVM_RESULT_STAGED);

    auto candidate = make_large(0x5C);
    EXPECT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, candidate.data(), candidate.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_BUSY);
    uint8_t straddle[2] = {0x01, 0x02};
    EXPECT_EQ(era_nvm_replace(&rig.nvm, kMacroBase - 1U, straddle, sizeof(straddle), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_BUSY);
    uint8_t outside = 0x77;
    EXPECT_EQ(era_nvm_replace(&rig.nvm, kMacroBase - 1U, &outside, 1U, ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_OK);
}

// eeprom_driver_erase()/CLEAN cannot reach this through era_nvm_replace(): a
// 24-KiB range needs a 24-KiB caller buffer and straddles the macro domain.
TEST(EraNvm, FormatBuildsFreshErasedGenerationProvenByReplay) {
    Rig rig;
    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    auto candidate = make_large(0x9E);
    ASSERT_EQ(era_nvm_replace(&rig.nvm, kMacroBase, candidate.data(), candidate.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_OK);
    const uint8_t marker_opener = 0xFF;
    ASSERT_EQ(era_nvm_qmk_write(&rig.nvm, kMacroBase + ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES - 1U, &marker_opener, 1U), ERA_NVM_RESULT_STAGED);

    const uint32_t generation_before = era_nvm_generation(&rig.nvm);
    ASSERT_EQ(era_nvm_format(&rig.nvm), ERA_NVM_RESULT_OK);
    EXPECT_EQ(era_nvm_generation(&rig.nvm), generation_before + 1U);

    // A staged upload does not survive a format, and the marker is valid again.
    uint8_t marker_after = 0xFF;
    ASSERT_EQ(era_nvm_read(&rig.nvm, kMacroBase + ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES - 1U, &marker_after, 1U), ERA_NVM_RESULT_OK);
    EXPECT_EQ(marker_after, 0U);

    ASSERT_EQ(rig.mount(), ERA_NVM_RESULT_OK);
    std::vector<uint8_t> image(ERA_NVM_LOGICAL_SIZE_BYTES, 0xFF);
    ASSERT_EQ(era_nvm_replay_read(&rig.nvm, 0U, image.data(), image.size()), ERA_NVM_RESULT_OK);
    EXPECT_TRUE(std::all_of(image.begin(), image.end(), [](uint8_t byte) { return byte == 0U; }));
    rig.expect_geometry_clean();
}

TEST(EraNvm, FormatFaultAtEveryMutationLeavesPreviousImageAuthoritative) {
    Rig base;
    ASSERT_EQ(base.mount(), ERA_NVM_RESULT_OK);
    auto old_image = make_large(0x21);
    ASSERT_EQ(era_nvm_replace(&base.nvm, kMacroBase, old_image.data(), old_image.size(), ERA_NVM_ORIGIN_REMOTE_APPLY), ERA_NVM_RESULT_OK);

    for (uint64_t cut = 1U; cut <= 99U; ++cut) {
        Rig attempt;
        attempt.flash = base.flash;
        ASSERT_EQ(attempt.mount(), ERA_NVM_RESULT_OK);
        attempt.flash.fail_mutation_call = attempt.flash.mutation_calls + cut;
        attempt.flash.mutation_fault     = FaultMode::FailPartial;
        EXPECT_NE(era_nvm_format(&attempt.nvm), ERA_NVM_RESULT_OK) << cut;
        attempt.flash.clear_faults();
        ASSERT_EQ(attempt.mount(), ERA_NVM_RESULT_OK);
        std::array<uint8_t, ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES> replay{};
        ASSERT_EQ(era_nvm_read(&attempt.nvm, kMacroBase, replay.data(), replay.size()), ERA_NVM_RESULT_OK);
        EXPECT_EQ(replay, old_image) << cut;
    }
}
