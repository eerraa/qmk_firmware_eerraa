// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* The hardware-free half of the PIO+DMA matrix scanner: what the PIO program
 * is made of, how a row pattern and a decode table are built from a pin list,
 * and how a frame is located in the DMA sample ring. era_rp2040_matrix_pio.c
 * owns the hardware and includes this; tests/era_rp2040_matrix_pio includes it
 * alone. Nothing here touches a register, so everything here can be proved on
 * a host, which is the whole reason for the split.
 *
 * The scan model these functions serve (the contract is in
 * era_board_adoption.md, the residency in era_sram_residency_contract.md):
 *
 *   pattern ring --DMA--> TX FIFO --> [ out pins,32 ; settle ; in pins,32 ;
 *                                       release ] --> RX FIFO --DMA--> sample ring
 *
 * One frame is FRAME_WORDS slots, one slot per row, padded to a power of two
 * so both DMA rings can wrap on a power-of-two byte size and a frame boundary
 * is a fixed ring offset. The padding slots carry the all-rows-released
 * pattern and their samples are never read. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef ERA_RP2040_MATRIX_PIO_ROW_T
#    define ERA_RP2040_MATRIX_PIO_ROW_T uint32_t
#endif
typedef ERA_RP2040_MATRIX_PIO_ROW_T era_rp2040_matrix_pio_row_t;

/* PIO1 owns no other program, so the whole 32-instruction memory is the
   budget; the encoder refuses to emit past this. */
#define ERA_RP2040_MATRIX_PIO_PROGRAM_MAX_INSTRUCTIONS 32U

/* Every GPIO reaches PIO input regardless of function select, so IN reads all
   30 pins in one word from base 0; OUT writes all 30 from base 0 and only the
   pins whose function select is PIO1 (the rows) take the value. */
#define ERA_RP2040_MATRIX_PIO_PIN_COUNT 32U

/* Instruction encodings, written out rather than taken from pio_instructions.h
   so this header stays hardware-free. Each is checked bit-for-bit by the host
   test against the RP2040 datasheet field layout (3.4). */
#define ERA_RP2040_MATRIX_PIO_INSTR_OUT_PINS_32 0x6000U /* out pins, 32 (count 32 encodes as 0) */
#define ERA_RP2040_MATRIX_PIO_INSTR_IN_PINS_32 0x4000U  /* in pins, 32 */
#define ERA_RP2040_MATRIX_PIO_INSTR_NOP 0xA042U         /* mov y, y */
#define ERA_RP2040_MATRIX_PIO_INSTR_MOV_PINS_NOT_NULL 0xA00BU /* mov pins, ~null */
#define ERA_RP2040_MATRIX_PIO_INSTR_DELAY_MAX 31U
#define ERA_RP2040_MATRIX_PIO_INSTR_DELAY_SHIFT 8U

/* The RP2040 PIO input path carries a two-flop synchroniser, so `in pins`
   observes the pad as it was two cycles earlier. The settle the CPU engine
   measured (wait_cpuclock(N) then a SIO read, which has no synchroniser) is
   therefore reproduced by N + 2 PIO cycles between the drive edge and the IN. */
#define ERA_RP2040_MATRIX_PIO_INPUT_SYNC_CYCLES 2U

static inline uint32_t era_rp2040_matrix_pio_pow2ceil(uint32_t value) {
    uint32_t result = 1U;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

static inline uint32_t era_rp2040_matrix_pio_log2(uint32_t pow2) {
    uint32_t shift = 0;
    while ((1UL << shift) < pow2) {
        shift++;
    }
    return shift;
}

/* Appends `cycles` cycles of pure delay after `head` (an instruction word
   without delay bits), as the head's own delay slot plus as many `nop [31]`
   as needed and one final partial nop. Returns the number of words written, 0
   when `cap` is too small. A delay of 0 emits only the head. */
static inline size_t era_rp2040_matrix_pio_emit_with_delay(uint16_t *out, size_t cap, uint16_t head, uint32_t cycles) {
    size_t count = 0;
    if (cap == 0) {
        return 0;
    }
    uint32_t head_delay = cycles > ERA_RP2040_MATRIX_PIO_INSTR_DELAY_MAX ? ERA_RP2040_MATRIX_PIO_INSTR_DELAY_MAX : cycles;
    out[count++]        = (uint16_t)(head | (head_delay << ERA_RP2040_MATRIX_PIO_INSTR_DELAY_SHIFT));
    cycles -= head_delay;
    /* Each nop is one cycle plus its delay slot, so a full nop covers 32. */
    while (cycles > 0) {
        if (count >= cap) {
            return 0;
        }
        uint32_t nop_covers = cycles >= (ERA_RP2040_MATRIX_PIO_INSTR_DELAY_MAX + 1U) ? (ERA_RP2040_MATRIX_PIO_INSTR_DELAY_MAX + 1U) : cycles;
        out[count++]        = (uint16_t)(ERA_RP2040_MATRIX_PIO_INSTR_NOP | ((nop_covers - 1U) << ERA_RP2040_MATRIX_PIO_INSTR_DELAY_SHIFT));
        cycles -= nop_covers;
    }
    return count;
}

/* The scan program, one slot per wrap:
 *   out pins,32 [settle...]   drive the pattern; the pins change at the end of
 *                             this cycle, and the settle counts from there
 *   in pins,32                sample; autopush 32 pushes the word
 *   mov pins,~null [release]  release every row (all HIGH) for the gap
 * `settle_cycles` is the drive-edge-to-sample distance the CPU engine used
 * (ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY), extended by the input synchroniser
 * inside; `release_cycles` is the all-released gap before the next drive.
 * Returns the instruction count, 0 if it does not fit. */
static inline size_t era_rp2040_matrix_pio_program_encode(uint16_t *out, size_t cap, uint32_t settle_cycles, uint32_t release_cycles) {
    size_t count = 0;
    /* out pins,32 then (settle + sync - 1) more cycles before the IN executes:
       the OUT itself is one cycle and its pin update lands at its end, so the
       IN must issue settle+sync cycles after the OUT issued. */
    uint32_t settle_total = settle_cycles + ERA_RP2040_MATRIX_PIO_INPUT_SYNC_CYCLES;
    if (settle_total < 1U) {
        settle_total = 1U;
    }
    size_t emitted = era_rp2040_matrix_pio_emit_with_delay(out, cap, ERA_RP2040_MATRIX_PIO_INSTR_OUT_PINS_32, settle_total - 1U);
    if (emitted == 0) {
        return 0;
    }
    count += emitted;
    if (count >= cap) {
        return 0;
    }
    out[count++] = ERA_RP2040_MATRIX_PIO_INSTR_IN_PINS_32;
    /* Release for release_cycles: the MOV is one cycle, so its delay covers
       release_cycles - 1; a release of 0 still emits the MOV so the rows are
       released for at least the one cycle the next OUT takes to land. */
    uint32_t release_delay = release_cycles > 0 ? release_cycles - 1U : 0U;
    emitted                = era_rp2040_matrix_pio_emit_with_delay(&out[count], cap - count, ERA_RP2040_MATRIX_PIO_INSTR_MOV_PINS_NOT_NULL, release_delay);
    if (emitted == 0) {
        return 0;
    }
    count += emitted;
    return count;
}

/* Cycle length of one slot for the program above, for the model figures and
   the host test: 1 (out) + settle+sync-1 + 1 (in) + max(release,1). */
static inline uint32_t era_rp2040_matrix_pio_slot_cycles(uint32_t settle_cycles, uint32_t release_cycles) {
    uint32_t settle_total = settle_cycles + ERA_RP2040_MATRIX_PIO_INPUT_SYNC_CYCLES;
    if (settle_total < 1U) {
        settle_total = 1U;
    }
    return settle_total + 1U + (release_cycles > 0 ? release_cycles : 1U);
}

/* Row patterns: every row HIGH except the selected one LOW; a slot past
   `row_count` (padding) releases every row. Bits outside the row set are 1 and
   never reach a pad, because no other pin has PIO1 as its function. A row
   with no SIO-mappable pin (NO_PIN, or above GPIO 29) selects nothing. */
static inline void era_rp2040_matrix_pio_patterns_build(uint32_t *patterns, uint32_t frame_words, const uint32_t *row_masks, uint32_t row_count) {
    for (uint32_t slot = 0; slot < frame_words; slot++) {
        uint32_t mask   = slot < row_count ? row_masks[slot] : 0U;
        patterns[slot]  = ~mask;
    }
}

/* Decode tables: for each of the four sample bytes, the row value contributed
   by the columns whose pins live in that byte, indexed by the byte's value.
   `pressed_state` is MATRIX_INPUT_PRESSED_STATE (0 on every ERA board: a
   pressed key reads the column LOW against its pull-up). A column entry with
   mask 0 (unpopulated, or not SIO-mappable) contributes nothing. */
static inline void era_rp2040_matrix_pio_decode_tables_build(era_rp2040_matrix_pio_row_t tables[4][256], const uint32_t *col_masks, const era_rp2040_matrix_pio_row_t *col_bits, uint32_t col_count, unsigned pressed_state) {
    for (uint32_t byte = 0; byte < 4U; byte++) {
        for (uint32_t value = 0; value < 256U; value++) {
            era_rp2040_matrix_pio_row_t row = 0;
            for (uint32_t col = 0; col < col_count; col++) {
                /* A mask holds one bit, so its byte-`byte` slice is that bit
                   when the column lives in this byte and zero otherwise. */
                uint32_t bit_in_byte = (col_masks[col] >> (byte * 8U)) & 0xFFU;
                if (bit_in_byte == 0U) {
                    continue;
                }
                bool high = (value & bit_in_byte) != 0U;
                if (high == (pressed_state != 0U)) {
                    row |= col_bits[col];
                }
            }
            tables[byte][value] = row;
        }
    }
}

/* One row from one sample word, four table reads. */
static inline era_rp2040_matrix_pio_row_t era_rp2040_matrix_pio_decode_row(const era_rp2040_matrix_pio_row_t tables[4][256], uint32_t sample) {
    return (era_rp2040_matrix_pio_row_t)(tables[0][sample & 0xFFU] | tables[1][(sample >> 8) & 0xFFU] | tables[2][(sample >> 16) & 0xFFU] | tables[3][(sample >> 24) & 0xFFU]);
}

/* The reference decode the tables are proved against on the host: the CPU
   engine's rule, one column at a time. */
static inline era_rp2040_matrix_pio_row_t era_rp2040_matrix_pio_decode_row_reference(const uint32_t *col_masks, const era_rp2040_matrix_pio_row_t *col_bits, uint32_t col_count, uint32_t sample, unsigned pressed_state) {
    era_rp2040_matrix_pio_row_t row = 0;
    for (uint32_t col = 0; col < col_count; col++) {
        if (col_masks[col] == 0U) {
            continue;
        }
        bool high = (sample & col_masks[col]) != 0U;
        if (high == (pressed_state != 0U)) {
            row |= col_bits[col];
        }
    }
    return row;
}

/* Where the latest complete frame is, given the DMA write pointer.
 * The writer is inside frame k = (write_addr - ring_base) / frame_bytes (or
 * exactly at its start, having just finished k - 1); either way k - 1 is the
 * newest frame no longer being written. Returns the ring word offset of that
 * frame. `frame_shift` = log2(frame_bytes), `frame_count_mask` = frames - 1. */
static inline uint32_t era_rp2040_matrix_pio_latest_complete_frame_offset(uint32_t write_addr, uint32_t ring_base, uint32_t frame_shift, uint32_t frame_count_mask, uint32_t frame_words_shift) {
    uint32_t in_progress = (write_addr - ring_base) >> frame_shift;
    return ((in_progress - 1U) & frame_count_mask) << frame_words_shift;
}

/* Whether a copy of the frame behind the writer may have been overwritten
   while it was being taken, from the number of words the writer moved during
   the copy. The copied frame is the one behind the writer's, so the writer
   must finish its own frame (at least one word, at most frame_words) and then
   frames - 2 whole frames before its next word lands in the copied one. Any
   copy that saw fewer than (frames - 2) * frame_words words move is therefore
   intact; at or beyond that the copy is treated as torn and taken again. The
   count comes from the DMA transfer counter, so a ring wrap cannot confuse it
   the way an address subtraction would. Needs frames >= 4 to leave a margin
   at all, which the sampler asserts. */
static inline bool era_rp2040_matrix_pio_copy_torn(uint32_t words_moved_during_copy, uint32_t frame_words, uint32_t ring_frames) {
    return words_moved_during_copy >= (ring_frames - 2U) * frame_words;
}
