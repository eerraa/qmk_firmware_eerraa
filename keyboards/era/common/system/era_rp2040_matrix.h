// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "matrix.h"

/* The raw scan backend the ERA matrix engine (era_rp2040_matrix_core.c) reads
 * its rows through, implemented by era_rp2040_matrix_pio.c: row drive, settle
 * and column read on a PIO1 state machine, samples DMA'd into a ring, one
 * frame per call. It prepares the pins in era_rp2040_matrix_init_pins().
 *
 * A second unit implemented the same contract on core0 once -- a CPU engine,
 * one row per call under a selector -- and retired: no ERA image linked it, and
 * tuning inside a CPU bit-bang scan is duplicated investment by owner decision.
 * That is why this header looks like an abstraction over two backends and is
 * not one. The unit's name and its text are deliberately not given: they are in
 * neither the tree nor the history a reader of this file holds. */
void era_rp2040_matrix_init_pins(void);

/* Decode the newest complete frame into raw_rows[] and report whether any raw
   row differs from the previous call -- the debounce runtime's `changed`. */
bool era_rp2040_matrix_update_raw_rows(matrix_row_t raw_rows[]);

typedef struct {
    uint8_t  ready;
    uint8_t  fdebug;       /* PIO1 FDEBUG for the sampler's SM: TXSTALL:TXOVER:RXUNDER:RXSTALL in bits 3..0, sticky */
    uint8_t  frame_words;  /* slots per frame (power of two, >= rows per hand) */
    uint32_t sample_words; /* words the sample DMA has moved in its current run (counts down from 0xFFFFFFFF; deltas only) */
    uint32_t torn_retries; /* frames re-read because the writer lapped them mid-decode */
    uint32_t rearms;       /* DMA re-triggers after a transfer count ran out */
} era_rp2040_matrix_pio_diagnostics_t;

void era_rp2040_matrix_pio_get_diagnostics(era_rp2040_matrix_pio_diagnostics_t *snapshot);
void era_rp2040_matrix_pio_clear_fdebug(void);
