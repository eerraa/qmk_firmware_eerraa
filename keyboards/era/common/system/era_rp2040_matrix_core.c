// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string.h>

#if defined(MCU_RP) && defined(MATRIX_SCAN_RAW_DIAGNOSTICS_ENABLE)
// Include before RP ChibiOS headers to avoid pico-sdk/RP header name clashes.
#    include "hardware/timer.h"
#endif

#include "debug.h"
#include "era_matrix_debounce_runtime.h"
#include "era_matrix_engine.h"
#include "era_rp2040_matrix.h"
#include "print.h"

#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
#    include "era_pass_phase_diagnostics.h"
#endif

#if defined(MATRIX_SCAN_RAW_DIAGNOSTICS_ENABLE)
#    if !defined(MCU_RP) && defined(PROTOCOL_CHIBIOS)
#        include "ch.h"
#    elif !defined(MCU_RP)
#        include "timer.h"
#    endif
#endif

#ifdef SPLIT_KEYBOARD
#    include "keyboard.h"
#    include "split_common/split_util.h"
#    include "keyboards/era/common/split/era_split_keyboard.h"
#    include "keyboards/era/common/split/era_split_transport_scheduler.h"
#    include "atomic_util.h"
#endif

#if !defined(ERA_RP2040_MATRIX_ENABLE)
#    error "era_rp2040_matrix_core.c must be built only when ERA_RP2040_MATRIX_ENABLE is set."
#endif

typedef struct {
    matrix_row_t raw_rows[MATRIX_ROWS_PER_HAND];
    matrix_row_t local_rows[MATRIX_ROWS_PER_HAND];
    matrix_row_t composed_rows[MATRIX_ROWS];
    bool         local_changed;
    bool         composed_changed;
#ifdef SPLIT_KEYBOARD
    /* The other half's rows as this half last accepted them. Split-only: on a
       non-split build composed_rows already is the whole matrix and there is
       no second source to hold against it. */
    matrix_row_t     peer_rows[MATRIX_ROWS_PER_HAND];
    uint8_t          this_hand;
    uint8_t          that_hand;
    volatile bool    host_peer_local_matrix_ready;
    volatile bool    host_peer_local_source_push_forced;
    matrix_row_t     host_peer_local_matrix[MATRIX_ROWS_PER_HAND];
    volatile uint8_t host_peer_local_current_seq8;
    volatile uint8_t host_peer_local_host_known_seq8;
    bool             host_peer_peer_cache_valid;
    volatile bool    host_peer_peer_cache_dirty;
    matrix_row_t     host_peer_peer_matrix[MATRIX_ROWS_PER_HAND];
    uint8_t          host_peer_peer_matrix_seq8;
    bool             host_peer_peer_projection_known;
    bool             host_peer_peer_projection_valid;
    uint8_t          host_peer_peer_projection_seq8;
    uint32_t         host_peer_peer_cache_update_count;
    uint32_t         host_peer_peer_cache_project_count;
    uint32_t         host_peer_peer_cache_flush_count;
#endif
} era_matrix_engine_state_t;

static era_matrix_engine_state_t g_era_matrix_engine;

#ifdef MATRIX_MASKED
extern const matrix_row_t matrix_mask[];
#endif

#ifdef SPLIT_KEYBOARD
static uint8_t era_matrix_engine_next_seq8(uint8_t seq) {
    seq++;
    return seq == 0 ? 1 : seq;
}

__attribute__((always_inline)) static inline bool era_matrix_engine_rows_equal(const matrix_row_t lhs[MATRIX_ROWS_PER_HAND], const matrix_row_t rhs[MATRIX_ROWS_PER_HAND]) {
    for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; row++) {
        if (lhs[row] != rhs[row]) {
            return false;
        }
    }
    return true;
}

__attribute__((always_inline)) static inline void era_matrix_engine_rows_copy(matrix_row_t dst[MATRIX_ROWS_PER_HAND], const matrix_row_t src[MATRIX_ROWS_PER_HAND]) {
    volatile matrix_row_t *vdst = dst;
    for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; row++) {
        vdst[row] = src[row];
    }
}

__attribute__((always_inline)) static inline bool era_matrix_engine_rows_any(const matrix_row_t rows[MATRIX_ROWS_PER_HAND]) {
    for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; row++) {
        if (rows[row] != 0) {
            return true;
        }
    }
    return false;
}

__attribute__((always_inline)) static inline void era_matrix_engine_rows_clear(matrix_row_t rows[MATRIX_ROWS_PER_HAND]) {
    volatile matrix_row_t *vrows = rows;
    for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; row++) {
        vrows[row] = 0;
    }
}
#endif

__attribute__((weak)) void matrix_init_kb(void) {
    matrix_init_user();
}

__attribute__((weak)) void matrix_scan_kb(void) {
    matrix_scan_user();
}

__attribute__((weak)) void matrix_init_user(void) {}
__attribute__((weak)) void matrix_scan_user(void) {}

#ifdef MATRIX_SCAN_RAW_DIAGNOSTICS_ENABLE
__attribute__((weak)) void matrix_scan_raw_diagnostics_kb(uint32_t raw_read_us) {
    (void)raw_read_us;
}
#endif

#ifdef MATRIX_SCAN_COUNT_DIAGNOSTICS_ENABLE
__attribute__((weak)) void matrix_scan_count_diagnostics_kb(void) {}
#endif

#if defined(MATRIX_SCAN_RAW_DIAGNOSTICS_ENABLE)
#    if defined(MCU_RP)
typedef uint32_t era_rp2040_matrix_core_diagnostics_time_t;

static inline era_rp2040_matrix_core_diagnostics_time_t era_rp2040_matrix_core_diagnostics_time_read(void) {
    return time_us_32();
}

static inline uint32_t era_rp2040_matrix_core_diagnostics_elapsed_us(era_rp2040_matrix_core_diagnostics_time_t start) {
    return time_us_32() - start;
}
#    elif defined(PROTOCOL_CHIBIOS)
typedef systime_t era_rp2040_matrix_core_diagnostics_time_t;

static inline era_rp2040_matrix_core_diagnostics_time_t era_rp2040_matrix_core_diagnostics_time_read(void) {
    return chVTGetSystemTimeX();
}

static inline uint32_t era_rp2040_matrix_core_diagnostics_elapsed_us(era_rp2040_matrix_core_diagnostics_time_t start) {
    return (uint32_t)TIME_I2US(chTimeDiffX(start, chVTGetSystemTimeX()));
}
#    else
typedef uint32_t era_rp2040_matrix_core_diagnostics_time_t;

static inline era_rp2040_matrix_core_diagnostics_time_t era_rp2040_matrix_core_diagnostics_time_read(void) {
    return timer_read32();
}

static inline uint32_t era_rp2040_matrix_core_diagnostics_elapsed_us(era_rp2040_matrix_core_diagnostics_time_t start) {
    return (timer_read32() - start) * 1000UL;
}
#    endif
#endif

uint8_t matrix_rows(void) {
    return MATRIX_ROWS;
}

uint8_t matrix_cols(void) {
    return MATRIX_COLS;
}

bool matrix_is_on(uint8_t row, uint8_t col) {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) {
        return false;
    }
    return (g_era_matrix_engine.composed_rows[row] & ((matrix_row_t)1 << col)) != 0;
}

matrix_row_t matrix_get_row(uint8_t row) {
    if (row >= MATRIX_ROWS) {
        return 0;
    }
#ifdef MATRIX_MASKED
    return g_era_matrix_engine.composed_rows[row] & matrix_mask[row];
#else
    return g_era_matrix_engine.composed_rows[row];
#endif
}

#if (MATRIX_COLS <= 8)
#    define print_matrix_header() print("\nr/c 01234567\n")
#    define print_matrix_row(row) print_bin_reverse8(matrix_get_row(row))
#elif (MATRIX_COLS <= 16)
#    define print_matrix_header() print("\nr/c 0123456789ABCDEF\n")
#    define print_matrix_row(row) print_bin_reverse16(matrix_get_row(row))
#elif (MATRIX_COLS <= 32)
#    define print_matrix_header() print("\nr/c 0123456789ABCDEF0123456789ABCDEF\n")
#    define print_matrix_row(row) print_bin_reverse32(matrix_get_row(row))
#endif

void matrix_print(void) {
    print_matrix_header();

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        print_hex8(row);
        print(": ");
        print_matrix_row(row);
        print("\n");
    }
}

#ifdef SPLIT_KEYBOARD
bool matrix_post_scan(void) {
    /* One transport step and one scan hook, on every half. Which half
       initiates, answers, or projects is the relation's fact, consumed inside
       the scheduler by the paths that act on it - the scan path carries no
       role of its own. The is_keyboard_master() branch that stood here chose
       between two arms that had converged to the same body, and it answered a
       relation question with authority: a held HOST-PEER HOST reads false for
       the whole suspend its own remote wake serves. The split path is now the
       same shape as the non-split matrix_scan() below: scan, one hook. */
    era_split_transport_scheduler_transport_step();
#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
    era_pass_phase_mark(ERA_PASS_PHASE_XPORT);
#endif
    matrix_scan_kb();

    /* After the transport step, so this scan's composed rows are final - the
       wake it requests fires exactly while the suspended half's own action
       pipeline is closed (era_split_keyboard.c). */
    if (g_era_matrix_engine.composed_changed) {
        era_split_keyboard_note_input_edge();
    }

    return g_era_matrix_engine.composed_changed;
}
#endif

static void era_matrix_engine_init(void) {
    era_rp2040_matrix_init_pins();

    memset(&g_era_matrix_engine, 0, sizeof(g_era_matrix_engine));
#ifdef SPLIT_KEYBOARD
    g_era_matrix_engine.this_hand = is_keyboard_left() ? 0 : (MATRIX_ROWS_PER_HAND);
    g_era_matrix_engine.that_hand = MATRIX_ROWS_PER_HAND - g_era_matrix_engine.this_hand;
#endif

    era_matrix_debounce_init();
}

__attribute__((always_inline)) static inline void era_matrix_engine_publish_local_rows(void) {
#ifdef SPLIT_KEYBOARD
    matrix_row_t *composed_rows = g_era_matrix_engine.composed_rows + g_era_matrix_engine.this_hand;
#else
    matrix_row_t *composed_rows = g_era_matrix_engine.composed_rows;
#endif

    for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; row++) {
        composed_rows[row] = g_era_matrix_engine.local_rows[row];
    }
}

static void __attribute__((noinline)) era_matrix_engine_scan_local(void) {
    bool raw_changed = false;

#if defined(MATRIX_SCAN_RAW_DIAGNOSTICS_ENABLE)
    era_rp2040_matrix_core_diagnostics_time_t raw_scan_start = 0;
#endif
#ifdef MATRIX_SCAN_RAW_DIAGNOSTICS_ENABLE
    raw_scan_start = era_rp2040_matrix_core_diagnostics_time_read();
#endif

    g_era_matrix_engine.local_changed    = false;
    g_era_matrix_engine.composed_changed = false;

    /* One frame per pass: the PIO1 sampler has already driven, settled and
       read every row; this decodes the newest complete frame out of its DMA
       ring. The raw diagnostics brackets therefore time the consumer, which
       is core0's whole share of the scan. */
    raw_changed = era_rp2040_matrix_update_raw_rows(g_era_matrix_engine.raw_rows);
#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
    /* Inside the raw diagnostics bracket rather than outside it: the two answer
       the same question and this one is the arm a qwin image has, since
       MATRIX_SCAN_RAW_DIAGNOSTICS_ENABLE is off whenever the count-only
       instrument is on (era_qmk_fork_ledger.md, quantum/matrix.h). */
    era_pass_phase_mark(ERA_PASS_PHASE_RAW);
#endif

#if defined(MATRIX_SCAN_RAW_DIAGNOSTICS_ENABLE)
    uint32_t raw_read_us = 0;
#endif
#ifdef MATRIX_SCAN_RAW_DIAGNOSTICS_ENABLE
    raw_read_us = era_rp2040_matrix_core_diagnostics_elapsed_us(raw_scan_start);
#endif
#ifdef MATRIX_SCAN_COUNT_DIAGNOSTICS_ENABLE
    matrix_scan_count_diagnostics_kb();
#endif
#ifdef MATRIX_SCAN_RAW_DIAGNOSTICS_ENABLE
    matrix_scan_raw_diagnostics_kb(raw_read_us);
#endif

    g_era_matrix_engine.local_changed = era_matrix_debounce_update(g_era_matrix_engine.raw_rows, g_era_matrix_engine.local_rows, raw_changed);
    if (g_era_matrix_engine.local_changed) {
        era_matrix_engine_publish_local_rows();
        g_era_matrix_engine.composed_changed = true;
    }
#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
    era_pass_phase_mark(ERA_PASS_PHASE_DEB);
#endif
}

bool era_matrix_engine_local_changed(void) {
    return g_era_matrix_engine.local_changed;
}

/* The peer half of the engine. These three sat outside this guard while the
   four row helpers they call sat inside it, which compiled for every board
   that existed and would have failed on the first non-split one. They are
   split-only by nature, not by accident: their whole subject is a second row
   source that a non-split board does not have. */
#ifdef SPLIT_KEYBOARD
/* Declared here rather than in era_matrix_engine.h because no caller outside
   this unit names them — that header carries the split relation's view of the
   engine, and these four are the engine's own peer-row bookkeeping. They keep
   external linkage deliberately: making them `static` would hand the compiler
   a fresh inlining decision and move RAM, which is more than a declaration
   move is allowed to cost. */
bool                                        era_matrix_engine_apply_peer_rows(const matrix_row_t rows[MATRIX_ROWS_PER_HAND]);
bool                                        era_matrix_engine_clear_peer_rows(void);
bool                                        era_matrix_engine_peer_cache_dirty(void);
era_matrix_engine_peer_matrix_copy_result_t era_matrix_engine_copy_peer_matrix_if_needed(matrix_row_t rows[MATRIX_ROWS_PER_HAND], bool applied_valid, uint8_t applied_seq8, uint8_t *peer_matrix_seq8);

bool era_matrix_engine_copy_local_rows(matrix_row_t rows[MATRIX_ROWS_PER_HAND]) {
    if (rows == NULL) {
        return false;
    }

    era_matrix_engine_rows_copy(rows, g_era_matrix_engine.local_rows);
    return true;
}

bool era_matrix_engine_apply_peer_rows(const matrix_row_t rows[MATRIX_ROWS_PER_HAND]) {
    if (rows == NULL) {
        return false;
    }

    bool changed = !era_matrix_engine_rows_equal(g_era_matrix_engine.peer_rows, rows);
    if (!changed) {
        return false;
    }

    era_matrix_engine_rows_copy(g_era_matrix_engine.peer_rows, rows);
    era_matrix_engine_rows_copy(g_era_matrix_engine.composed_rows + g_era_matrix_engine.that_hand, rows);
    g_era_matrix_engine.composed_changed = true;
    return true;
}

bool era_matrix_engine_clear_peer_rows(void) {
    bool changed = era_matrix_engine_rows_any(g_era_matrix_engine.peer_rows);
    if (!changed) {
        return false;
    }

    era_matrix_engine_rows_clear(g_era_matrix_engine.peer_rows);
    era_matrix_engine_rows_clear(g_era_matrix_engine.composed_rows + g_era_matrix_engine.that_hand);
    g_era_matrix_engine.composed_changed = true;
    return true;
}
#endif /* SPLIT_KEYBOARD */

#ifdef SPLIT_KEYBOARD
bool era_matrix_engine_publish_local_snapshot_if_needed(bool *first_ready) {
    if (first_ready != NULL) {
        *first_ready = false;
    }

    bool changed   = false;
    bool skip      = false;
    bool was_ready = g_era_matrix_engine.host_peer_local_matrix_ready;
    if (was_ready && !g_era_matrix_engine.local_changed) {
        skip = true;
    } else {
        if (!was_ready || !era_matrix_engine_rows_equal(g_era_matrix_engine.host_peer_local_matrix, g_era_matrix_engine.local_rows)) {
            era_matrix_engine_rows_copy(g_era_matrix_engine.host_peer_local_matrix, g_era_matrix_engine.local_rows);
            g_era_matrix_engine.host_peer_local_current_seq8       = era_matrix_engine_next_seq8(g_era_matrix_engine.host_peer_local_current_seq8);
            g_era_matrix_engine.host_peer_local_source_push_forced = true;
            g_era_matrix_engine.host_peer_local_matrix_ready       = true;
            changed                                                = true;
        }

        if (first_ready != NULL) {
            *first_ready = !was_ready && g_era_matrix_engine.host_peer_local_matrix_ready;
        }
    }
    if (skip) {
        return false;
    }
    return changed;
}

bool era_matrix_engine_local_matrix_ready(void) {
    return g_era_matrix_engine.host_peer_local_matrix_ready;
}

bool era_matrix_engine_source_push_due(void) {
    return g_era_matrix_engine.host_peer_local_matrix_ready && (g_era_matrix_engine.host_peer_local_source_push_forced || g_era_matrix_engine.host_peer_local_current_seq8 != g_era_matrix_engine.host_peer_local_host_known_seq8);
}

bool era_matrix_engine_copy_source_push_rows(matrix_row_t rows[MATRIX_ROWS_PER_HAND], uint8_t *matrix_seq) {
    if (rows == NULL || matrix_seq == NULL) {
        return false;
    }

    bool ready = false;
    ready      = g_era_matrix_engine.host_peer_local_matrix_ready;
    if (ready) {
        era_matrix_engine_rows_copy(rows, g_era_matrix_engine.host_peer_local_matrix);
        *matrix_seq = g_era_matrix_engine.host_peer_local_current_seq8;
    }
    return ready;
}

void era_matrix_engine_note_source_push_accepted(uint8_t matrix_seq) {
    g_era_matrix_engine.host_peer_local_host_known_seq8    = matrix_seq;
    g_era_matrix_engine.host_peer_local_source_push_forced = false;
}

bool era_matrix_engine_accept_peer_snapshot(const matrix_row_t rows[MATRIX_ROWS_PER_HAND]) {
    if (rows == NULL) {
        return false;
    }

    ATOMIC_BLOCK_RESTORESTATE {
        era_matrix_engine_rows_copy(g_era_matrix_engine.host_peer_peer_matrix, rows);
        g_era_matrix_engine.host_peer_peer_cache_valid = true;
        g_era_matrix_engine.host_peer_peer_matrix_seq8 = era_matrix_engine_next_seq8(g_era_matrix_engine.host_peer_peer_matrix_seq8);
        g_era_matrix_engine.host_peer_peer_cache_dirty = true;
        g_era_matrix_engine.host_peer_peer_cache_update_count++;
    }
    return true;
}

bool era_matrix_engine_peer_cache_dirty(void) {
    return g_era_matrix_engine.host_peer_peer_cache_dirty;
}

era_matrix_engine_peer_matrix_copy_result_t era_matrix_engine_copy_peer_matrix_if_needed(matrix_row_t rows[MATRIX_ROWS_PER_HAND], bool applied_valid, uint8_t applied_seq8, uint8_t *peer_matrix_seq8) {
    if (rows == NULL) {
        return ERA_MATRIX_ENGINE_PEER_MATRIX_INVALID;
    }

    era_matrix_engine_peer_matrix_copy_result_t result = ERA_MATRIX_ENGINE_PEER_MATRIX_UNCHANGED;
    ATOMIC_BLOCK_RESTORESTATE {
        bool    dirty = g_era_matrix_engine.host_peer_peer_cache_dirty;
        bool    valid = g_era_matrix_engine.host_peer_peer_cache_valid;
        uint8_t seq8  = g_era_matrix_engine.host_peer_peer_matrix_seq8;

        if (dirty) {
            g_era_matrix_engine.host_peer_peer_cache_dirty = false;
        }
        if (peer_matrix_seq8 != NULL) {
            *peer_matrix_seq8 = seq8;
        }

        if (!valid) {
            result = ERA_MATRIX_ENGINE_PEER_MATRIX_INVALID;
        } else if (!applied_valid || dirty || applied_seq8 != seq8) {
            era_matrix_engine_rows_copy(rows, g_era_matrix_engine.host_peer_peer_matrix);
            g_era_matrix_engine.host_peer_peer_cache_project_count++;
            result = ERA_MATRIX_ENGINE_PEER_MATRIX_COPIED;
        }
    }
    return result;
}

era_matrix_engine_peer_projection_result_t era_matrix_engine_sync_peer_projection(bool host_mode) {
    if (!host_mode) {
        if (!g_era_matrix_engine.host_peer_peer_projection_valid) {
            g_era_matrix_engine.host_peer_peer_projection_known = false;
            return ERA_MATRIX_ENGINE_PEER_PROJECTION_UNCHANGED;
        }

        era_matrix_engine_clear_peer_rows();
        g_era_matrix_engine.host_peer_peer_projection_known = false;
        g_era_matrix_engine.host_peer_peer_projection_valid = false;
        g_era_matrix_engine.host_peer_peer_projection_seq8  = 0;
        return ERA_MATRIX_ENGINE_PEER_PROJECTION_CLEARED;
    }

    if (g_era_matrix_engine.host_peer_peer_projection_known && !era_matrix_engine_peer_cache_dirty()) {
        return ERA_MATRIX_ENGINE_PEER_PROJECTION_UNCHANGED;
    }

    matrix_row_t peer_rows[MATRIX_ROWS_PER_HAND];
    uint8_t      peer_matrix_seq8 = 0;
    switch (era_matrix_engine_copy_peer_matrix_if_needed(peer_rows, g_era_matrix_engine.host_peer_peer_projection_valid, g_era_matrix_engine.host_peer_peer_projection_seq8, &peer_matrix_seq8)) {
        case ERA_MATRIX_ENGINE_PEER_MATRIX_COPIED:
            break;
        case ERA_MATRIX_ENGINE_PEER_MATRIX_INVALID:
            if (g_era_matrix_engine.host_peer_peer_projection_valid) {
                era_matrix_engine_clear_peer_rows();
            }
            g_era_matrix_engine.host_peer_peer_projection_known = true;
            g_era_matrix_engine.host_peer_peer_projection_valid = false;
            g_era_matrix_engine.host_peer_peer_projection_seq8  = 0;
            return ERA_MATRIX_ENGINE_PEER_PROJECTION_CLEARED;
        case ERA_MATRIX_ENGINE_PEER_MATRIX_UNCHANGED:
        default:
            return ERA_MATRIX_ENGINE_PEER_PROJECTION_UNCHANGED;
    }

    era_matrix_engine_apply_peer_rows(peer_rows);
    g_era_matrix_engine.host_peer_peer_projection_known = true;
    g_era_matrix_engine.host_peer_peer_projection_valid = true;
    g_era_matrix_engine.host_peer_peer_projection_seq8  = peer_matrix_seq8;
    return ERA_MATRIX_ENGINE_PEER_PROJECTION_APPLIED;
}

bool era_matrix_engine_peer_projection_scan_idle(bool host_mode) {
    if (host_mode) {
        return g_era_matrix_engine.host_peer_peer_projection_known && !era_matrix_engine_peer_cache_dirty();
    }

    return !g_era_matrix_engine.host_peer_peer_projection_valid;
}

void era_matrix_engine_flush_host_peer_relation(void) {
    ATOMIC_BLOCK_RESTORESTATE {
        g_era_matrix_engine.host_peer_peer_cache_valid         = false;
        g_era_matrix_engine.host_peer_peer_cache_dirty         = true;
        g_era_matrix_engine.host_peer_peer_matrix_seq8         = 0;
        g_era_matrix_engine.host_peer_local_host_known_seq8    = 0;
        g_era_matrix_engine.host_peer_local_source_push_forced = g_era_matrix_engine.host_peer_local_matrix_ready;
        g_era_matrix_engine.host_peer_peer_cache_flush_count++;
    }
}

void era_matrix_engine_get_host_peer_diagnostics(era_matrix_engine_host_peer_diagnostics_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    ATOMIC_BLOCK_RESTORESTATE {
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->local_matrix_ready       = g_era_matrix_engine.host_peer_local_matrix_ready ? 1 : 0;
        snapshot->local_source_push_forced = g_era_matrix_engine.host_peer_local_source_push_forced ? 1 : 0;
        snapshot->peer_cache_valid         = g_era_matrix_engine.host_peer_peer_cache_valid ? 1 : 0;
        snapshot->local_current_seq8       = g_era_matrix_engine.host_peer_local_current_seq8;
        snapshot->local_host_known_seq8    = g_era_matrix_engine.host_peer_local_host_known_seq8;
        snapshot->peer_matrix_seq8         = g_era_matrix_engine.host_peer_peer_matrix_seq8;
        snapshot->peer_cache_update_count  = g_era_matrix_engine.host_peer_peer_cache_update_count;
        snapshot->peer_cache_project_count = g_era_matrix_engine.host_peer_peer_cache_project_count;
        snapshot->peer_cache_flush_count   = g_era_matrix_engine.host_peer_peer_cache_flush_count;
    }
}
#endif

void matrix_init(void) {
    era_matrix_engine_init();

    matrix_init_kb();
}

uint8_t matrix_scan(void) {
#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
    /* The wrap point of the twelve-segment tiling, and therefore the definition
       of a pass for this instrument: everything since the last housekeeping
       return is charged to ERA_PASS_PHASE_REST here. A pass that matrix_task()
       skipped on !matrix_can_read() is not counted and its time falls into
       ERA_PASS_PHASE_ACT -- true of no ERA board, where the hook is QMK's weak
       `true`, and stated because the segments would otherwise stop tiling. */
    era_pass_phase_wrap();
#endif
    era_matrix_engine_scan_local();

#ifdef SPLIT_KEYBOARD
    matrix_post_scan();
#else
    matrix_scan_kb();
#endif

#ifdef ERA_PASS_PHASE_DIAGNOSTICS_ENABLE
    /* At the end of matrix_scan(), after the transport step and scan hook. */
    era_pass_phase_mark(ERA_PASS_PHASE_SCANHK);
#endif
    return (uint8_t)g_era_matrix_engine.composed_changed;
}

bool peek_matrix(uint8_t row_index, uint8_t col_index, bool raw) {
    if (col_index >= MATRIX_COLS) {
        return false;
    }
    if (raw) {
        if (row_index >= MATRIX_ROWS_PER_HAND) {
            return false;
        }
        return 0 != (g_era_matrix_engine.raw_rows[row_index] & (MATRIX_ROW_SHIFTER << col_index));
    }

    return matrix_is_on(row_index, col_index);
}
