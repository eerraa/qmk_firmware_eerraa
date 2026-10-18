// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "era_rp2040_matrix_pio_frame.h"

/* Experimental bounded recovery, with no CPU polling. Explicit PULL is
 * required: the GPIO check borrows OSR, but must not consume a DMA pattern. */
static inline size_t era_rp2040_matrix_pio_recovery_encode(uint16_t *out, size_t cap, uint32_t settle, uint32_t release, uint32_t matrix_pins) {
    if (cap > 32) cap = 32;
    if (cap < 2 || !matrix_pins || (matrix_pins & 0xC0000000U)) return 0;
    out[0] = 0x80A0U; // PULL BLOCK
    size_t n = era_rp2040_matrix_pio_program_encode(out + 1, cap - 1, settle, release);
    if (!n) return 0;
    ++n;
    /* A 32-cycle countdown preserves the exact drive-to-IN distance while
       leaving instruction space for fragmented GPIO masks (Riley).
       X is free before the sample and the recovery loop overwrites it. */
    if (settle >= 32 && settle <= 1024 && settle % 32 == 0) {
        size_t sample = 1;
        while (sample < n && (out[sample] & 0xE0FFU) != 0x4000U) ++sample;
        if (sample == n || sample < 4) return 0;
        size_t saved = sample - 4;
        for (size_t i = sample; i < n; ++i) out[i - saved] = out[i];
        n -= saved;
        out[1] = 0x6000U; // OUT PINS,32
        out[2] = (uint16_t)(0xE020U | (settle / 32 - 1)); // SET X,blocks-1
        out[3] = 0x1F43U; // JMP X--,3 [31]
    }
#define RECOVERY_EMIT(word) do { if (n >= cap) return 0; out[n++] = (uint16_t)(word); } while (0)
    RECOVERY_EMIT(0xE05FU); // SET Y,31: at most 32 checks
    size_t poll = n;
    RECOVERY_EMIT(0xA0E8U); // MOV OSR,~PINS; ISR/RX data remain untouched
    unsigned consumed = 0;
    uint32_t retry_jumps = 0;
    for (unsigned pin = 0; pin < 30;) {
        if (!(matrix_pins & (1UL << pin))) { ++pin; continue; }
        unsigned first = pin;
        do { ++pin; } while (pin < 30 && (matrix_pins & (1UL << pin)));
        if (first > consumed) RECOVERY_EMIT(0x6060U | (first - consumed)); // OUT NULL,gap
        RECOVERY_EMIT(0x6020U | (pin - first)); // OUT X,run
        consumed = pin;
        if (!(matrix_pins >> pin)) {
            RECOVERY_EMIT(0x0020U); // JMP !X,0: all pins HIGH; next pattern
        } else {
            if (n + 2 > cap) return 0;
            out[n] = (uint16_t)(0x0020U | (n + 2)); // skip retry if group HIGH
            ++n;
            retry_jumps |= 1UL << n;
            RECOVERY_EMIT(0); // JMP retry, patched below
        }
    }
    size_t retry = n;
    RECOVERY_EMIT(ERA_RP2040_MATRIX_PIO_INSTR_NOP | (31U << 8));
    RECOVERY_EMIT(ERA_RP2040_MATRIX_PIO_INSTR_NOP | (31U << 8));
    RECOVERY_EMIT(0x0080U | poll | (31U << 8)); // JMP Y--,poll [31]
    for (unsigned i = 0; i < n; ++i)
        if (retry_jumps & (1UL << i)) out[i] = (uint16_t)retry;
#undef RECOVERY_EMIT
    /* On timeout the final JMP falls through the configured wrap to PULL.
       A held-low input cannot hang the state machine or mask a key bit. */
    return n;
}
