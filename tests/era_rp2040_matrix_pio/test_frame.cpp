// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* The hardware-free half of the PIO+DMA matrix sampler, proved on the host.
 * What is proved and against what:
 *  - the four instruction encodings, bit for bit against the RP2040 datasheet
 *    field layout (section 3.4), so the sampler cannot ship a wrong opcode;
 *  - the settle/release program: length, cycle sum, and shape;
 *  - row patterns for both tomak79h hands, one LOW bit each, padding released;
 *  - decode tables against the per-column reference rule in the header (the
 *    retired CPU engine's) over random samples, both hands, both polarities;
 *  - the ring arithmetic: which frame is the newest complete one for every
 *    write-pointer position, wrap included, and when a read counts as torn.
 * The pin lists are tomak79h's (keyboard.json) written out, not derived, so a
 * change to that file does not silently move the proof. */

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/era/common/system/era_rp2040_matrix_pio_frame.h"
}

#include <cstdint>
#include <cstdlib>

namespace {

constexpr uint32_t kNoPin = 0xFFFFFFFFu;

uint32_t pin_mask(uint32_t pin) {
    return pin == kNoPin || pin > 29u ? 0u : (1u << pin);
}

/* tomak79h, from keyboard.json: LEFT default matrix_pins, RIGHT split.matrix_pins.right. */
const uint32_t kLeftRows[6]  = {24, 8, 26, 25, 27, 28};
const uint32_t kLeftCols[9]  = {29, 9, 7, 6, 5, 4, 3, 2, kNoPin};
const uint32_t kRightRows[6] = {29, 28, 27, 26, 11, 3};
const uint32_t kRightCols[9] = {25, 24, 23, 22, 7, 6, 5, 4, 2};

} // namespace

TEST(EraMatrixPioFrame, InstructionEncodingsMatchTheDatasheet) {
    /* Bits 15..13 opcode, 12..8 delay/side-set, 7..5 destination/source, 4..0 count/op+src. */
    const uint32_t opcode_in  = 0x2u << 13;
    const uint32_t opcode_out = 0x3u << 13;
    const uint32_t opcode_mov = 0x5u << 13;
    const uint32_t dest_pins  = 0x0u << 5;
    const uint32_t src_pins   = 0x0u << 5;
    const uint32_t dest_y     = 0x2u << 5;
    const uint32_t mov_op_none   = 0x0u << 3;
    const uint32_t mov_op_invert = 0x1u << 3;
    const uint32_t mov_src_y     = 0x2u;
    const uint32_t mov_src_null  = 0x3u;
    const uint32_t count_32      = 0x0u; /* a bit count of 32 encodes as 0 */

    EXPECT_EQ(ERA_RP2040_MATRIX_PIO_INSTR_OUT_PINS_32, opcode_out | dest_pins | count_32);
    EXPECT_EQ(ERA_RP2040_MATRIX_PIO_INSTR_IN_PINS_32, opcode_in | src_pins | count_32);
    EXPECT_EQ(ERA_RP2040_MATRIX_PIO_INSTR_NOP, opcode_mov | dest_y | mov_op_none | mov_src_y);
    EXPECT_EQ(ERA_RP2040_MATRIX_PIO_INSTR_MOV_PINS_NOT_NULL, opcode_mov | dest_pins | mov_op_invert | mov_src_null);
    EXPECT_EQ(ERA_RP2040_MATRIX_PIO_INSTR_DELAY_SHIFT, 8u);
    EXPECT_EQ(ERA_RP2040_MATRIX_PIO_INSTR_DELAY_MAX, 31u);
}

static uint32_t program_cycles(const uint16_t *words, size_t count) {
    uint32_t cycles = 0;
    for (size_t i = 0; i < count; i++) {
        cycles += 1u + ((words[i] >> ERA_RP2040_MATRIX_PIO_INSTR_DELAY_SHIFT) & 0x1Fu);
    }
    return cycles;
}

TEST(EraMatrixPioFrame, ProgramForTheShippedSettleAndRelease) {
    uint16_t words[ERA_RP2040_MATRIX_PIO_PROGRAM_MAX_INSTRUCTIONS];
    size_t   len = era_rp2040_matrix_pio_program_encode(words, ERA_RP2040_MATRIX_PIO_PROGRAM_MAX_INSTRUCTIONS, 128, 64);
    ASSERT_EQ(len, 8u);
    /* out [31], nop [31] x3, nop [1], in, mov pins,~null [31], nop [31] */
    EXPECT_EQ(words[0], ERA_RP2040_MATRIX_PIO_INSTR_OUT_PINS_32 | (31u << 8));
    EXPECT_EQ(words[1], ERA_RP2040_MATRIX_PIO_INSTR_NOP | (31u << 8));
    EXPECT_EQ(words[2], ERA_RP2040_MATRIX_PIO_INSTR_NOP | (31u << 8));
    EXPECT_EQ(words[3], ERA_RP2040_MATRIX_PIO_INSTR_NOP | (31u << 8));
    EXPECT_EQ(words[4], ERA_RP2040_MATRIX_PIO_INSTR_NOP | (1u << 8));
    EXPECT_EQ(words[5], ERA_RP2040_MATRIX_PIO_INSTR_IN_PINS_32);
    EXPECT_EQ(words[6], ERA_RP2040_MATRIX_PIO_INSTR_MOV_PINS_NOT_NULL | (31u << 8));
    EXPECT_EQ(words[7], ERA_RP2040_MATRIX_PIO_INSTR_NOP | (31u << 8));
    /* Drive edge to IN issue: 130 cycles (128 + the two synchroniser cycles);
       IN one; release 64. */
    EXPECT_EQ(program_cycles(words, len), 195u);
    EXPECT_EQ(era_rp2040_matrix_pio_slot_cycles(128, 64), 195u);
    /* The IN issues exactly settle+sync cycles after the OUT issued. */
    EXPECT_EQ(program_cycles(words, 5), 128u + ERA_RP2040_MATRIX_PIO_INPUT_SYNC_CYCLES);
}

TEST(EraMatrixPioFrame, ProgramEdgesAndBudget) {
    uint16_t words[ERA_RP2040_MATRIX_PIO_PROGRAM_MAX_INSTRUCTIONS];
    /* Zero settle and zero release still make a legal three-word program. */
    size_t len = era_rp2040_matrix_pio_program_encode(words, ERA_RP2040_MATRIX_PIO_PROGRAM_MAX_INSTRUCTIONS, 0, 0);
    ASSERT_EQ(len, 3u);
    EXPECT_EQ(words[0], ERA_RP2040_MATRIX_PIO_INSTR_OUT_PINS_32 | (1u << 8)); /* sync cycles only */
    EXPECT_EQ(words[1], ERA_RP2040_MATRIX_PIO_INSTR_IN_PINS_32);
    EXPECT_EQ(words[2], ERA_RP2040_MATRIX_PIO_INSTR_MOV_PINS_NOT_NULL);
    EXPECT_EQ(era_rp2040_matrix_pio_slot_cycles(0, 0), program_cycles(words, len));
    /* Every settle/release pair the fixed baseline could plausibly take fits,
       and the cycle sum always equals the model. */
    for (uint32_t settle = 0; settle <= 512; settle += 7) {
        for (uint32_t release = 0; release <= 256; release += 5) {
            len = era_rp2040_matrix_pio_program_encode(words, ERA_RP2040_MATRIX_PIO_PROGRAM_MAX_INSTRUCTIONS, settle, release);
            ASSERT_NE(len, 0u) << settle << " " << release;
            ASSERT_LE(len, ERA_RP2040_MATRIX_PIO_PROGRAM_MAX_INSTRUCTIONS);
            EXPECT_EQ(program_cycles(words, len), era_rp2040_matrix_pio_slot_cycles(settle, release)) << settle << " " << release;
        }
    }
    /* Past the instruction memory the encoder refuses rather than truncates. */
    EXPECT_EQ(era_rp2040_matrix_pio_program_encode(words, ERA_RP2040_MATRIX_PIO_PROGRAM_MAX_INSTRUCTIONS, 2000, 2000), 0u);
    EXPECT_EQ(era_rp2040_matrix_pio_program_encode(words, 4, 128, 64), 0u);
    /* The bound era_rp2040_matrix_pio.c's `_Static_assert` believes: a
       settle+release sum of 900 still fits the 32 instructions. That assert
       states the limit and cannot test it -- it refuses a configuration past
       900 without anything showing 900 itself encodes -- so the case is here.
       Every split of the sum, because the two halves round up separately. */
    for (uint32_t settle = 0; settle <= 900; settle += 3) {
        len = era_rp2040_matrix_pio_program_encode(words, ERA_RP2040_MATRIX_PIO_PROGRAM_MAX_INSTRUCTIONS, settle, 900 - settle);
        ASSERT_NE(len, 0u) << "settle " << settle << " release " << 900 - settle;
        EXPECT_EQ(program_cycles(words, len), era_rp2040_matrix_pio_slot_cycles(settle, 900 - settle));
    }
}

TEST(EraMatrixPioFrame, PatternsSelectOneRowAndPadReleased) {
    uint32_t masks[6];
    for (int hand = 0; hand < 2; hand++) {
        const uint32_t *rows = hand == 0 ? kLeftRows : kRightRows;
        for (int r = 0; r < 6; r++) {
            masks[r] = pin_mask(rows[r]);
        }
        uint32_t patterns[8];
        era_rp2040_matrix_pio_patterns_build(patterns, 8, masks, 6);
        for (int slot = 0; slot < 8; slot++) {
            if (slot < 6) {
                EXPECT_EQ(~patterns[slot], masks[slot]) << "hand " << hand << " slot " << slot;
            } else {
                EXPECT_EQ(patterns[slot], 0xFFFFFFFFu) << "hand " << hand << " slot " << slot;
            }
        }
    }
    /* A row with no SIO pin selects nothing: its pattern is all released. */
    uint32_t odd[2] = {pin_mask(kNoPin), pin_mask(3)};
    uint32_t p[2];
    era_rp2040_matrix_pio_patterns_build(p, 2, odd, 2);
    EXPECT_EQ(p[0], 0xFFFFFFFFu);
    EXPECT_EQ(p[1], ~(1u << 3));
}

TEST(EraMatrixPioFrame, DecodeTablesMatchTheReferenceRule) {
    static era_rp2040_matrix_pio_row_t tables[4][256];
    for (int hand = 0; hand < 2; hand++) {
        const uint32_t *cols = hand == 0 ? kLeftCols : kRightCols;
        uint32_t                    masks[9];
        era_rp2040_matrix_pio_row_t bits[9];
        for (int c = 0; c < 9; c++) {
            masks[c] = pin_mask(cols[c]);
            bits[c]  = 1u << c;
        }
        for (unsigned pressed = 0; pressed <= 1; pressed++) {
            era_rp2040_matrix_pio_decode_tables_build(tables, masks, bits, 9, pressed);
            /* Every column alone, every pair, and a random sweep. */
            for (int c = 0; c < 9; c++) {
                if (masks[c] == 0) continue;
                uint32_t sample = pressed ? masks[c] : ~masks[c];
                EXPECT_EQ(era_rp2040_matrix_pio_decode_row(tables, sample), bits[c]) << "hand " << hand << " col " << c << " pressed " << pressed;
            }
            std::srand(1234u + hand * 7u + pressed);
            for (int i = 0; i < 20000; i++) {
                uint32_t sample = ((uint32_t)std::rand() << 16) ^ (uint32_t)std::rand();
                EXPECT_EQ(era_rp2040_matrix_pio_decode_row(tables, sample), era_rp2040_matrix_pio_decode_row_reference(masks, bits, 9, sample, pressed)) << "hand " << hand << " sample " << sample;
            }
            /* The unpopulated LEFT column 8 never reports. */
            if (hand == 0) {
                for (int i = 0; i < 2000; i++) {
                    uint32_t sample = ((uint32_t)std::rand() << 16) ^ (uint32_t)std::rand();
                    EXPECT_EQ(era_rp2040_matrix_pio_decode_row(tables, sample) & (1u << 8), 0u);
                }
            }
        }
    }
}

TEST(EraMatrixPioFrame, LatestCompleteFrameForEveryWritePosition) {
    const uint32_t base        = 0x20000000u;
    const uint32_t frame_words = 8, frames = 4;
    const uint32_t frame_bytes = frame_words * 4, ring_bytes = frame_bytes * frames;
    const uint32_t frame_shift = era_rp2040_matrix_pio_log2(frame_bytes);
    const uint32_t words_shift = era_rp2040_matrix_pio_log2(frame_words);
    EXPECT_EQ(frame_shift, 5u);
    EXPECT_EQ(words_shift, 3u);
    EXPECT_EQ(era_rp2040_matrix_pio_pow2ceil(6), 8u);
    EXPECT_EQ(era_rp2040_matrix_pio_pow2ceil(8), 8u);
    EXPECT_EQ(era_rp2040_matrix_pio_pow2ceil(10), 16u);
    EXPECT_EQ(era_rp2040_matrix_pio_pow2ceil(1), 1u);
    for (uint32_t k = 0; k < frames; k++) {
        for (uint32_t word = 0; word < frame_words; word++) {
            uint32_t write_addr = base + k * frame_bytes + word * 4;
            uint32_t offset     = era_rp2040_matrix_pio_latest_complete_frame_offset(write_addr, base, frame_shift, frames - 1, words_shift);
            /* The frame behind the one being written, wrapping to the last
               frame when the writer is in frame 0. */
            uint32_t expected_frame = (k + frames - 1) % frames;
            EXPECT_EQ(offset, expected_frame * frame_words) << "k " << k << " word " << word;
        }
    }
    /* Torn: with four frames the writer must move at least two whole frames
       during the copy before its next word can land in the copied one. */
    (void)ring_bytes;
    EXPECT_FALSE(era_rp2040_matrix_pio_copy_torn(0, frame_words, frames));
    EXPECT_FALSE(era_rp2040_matrix_pio_copy_torn(1, frame_words, frames));
    EXPECT_FALSE(era_rp2040_matrix_pio_copy_torn(2 * frame_words - 1, frame_words, frames));
    EXPECT_TRUE(era_rp2040_matrix_pio_copy_torn(2 * frame_words, frame_words, frames));
    EXPECT_TRUE(era_rp2040_matrix_pio_copy_torn(4 * frame_words, frame_words, frames));
    /* The bound is conservative by two words against the worst phase (writer
       one word short of finishing its own frame needs 2*FW+2 words to reach
       the copied one), which costs a rare needless retry and never a miss. */
}
