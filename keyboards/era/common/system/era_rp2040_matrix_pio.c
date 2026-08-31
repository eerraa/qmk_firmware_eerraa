// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* The PIO+DMA matrix sampler: the row drive, the settle and the column read
 * run on a PIO1 state machine, two DMA channels feed it patterns and carry its
 * samples into a ring, and core0's part of the scan shrinks to reading the
 * latest complete frame out of that ring once per pass.
 *
 * Why PIO1 alone: PIO0 carries the ws2812 driver and both wire state machines,
 * and `out pins,32` writes every pin whose function select is the emitting
 * block. Giving the matrix its own block means the only pins that can take the
 * pattern are the rows this unit routed there, so no per-cycle pin priority
 * between state machines is ever in play (design brief, census 1.1).
 *
 * What is deliberately identical to the retired CPU engine it replaced, and
 * therefore what the device evidence taken on that engine still covers: rows
 * are
 * push-pull, HIGH at rest and LOW while selected, one row at a time; the
 * settle before the sample is the same ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY
 * (the fixed baseline in era_performance_gates.md); the pad register of a row
 * pin is byte-identical (input enable only, 2 mA, slow slew); columns stay SIO
 * inputs with pull-ups and are read through PIO input, which every GPIO
 * reaches regardless of function select. Only the release gap between rows
 * changes -- and it grows (64 cycles against the ~50-70 the CPU decode gave),
 * so the electrical case is a re-verification, not a new one.
 *
 * What is deliberately not done: no fold of every frame since the last pass
 * into an OR/AND (that would change the debounce input semantics
 * era_matrix_debounce_runtime.c is written against), no ring deep enough to
 * ride out a flash stall (typing through a store is not a goal -- owner
 * decision), no PIO or DMA interrupt (nothing
 * needs to be told: the sampler free-runs and the consumer reads).
 *
 * The DMA transfer counts are finite (0xFFFFFFFF words, about 93 minutes at
 * this frame rate). When one runs out the state machine stalls -- TXSTALL on an
 * empty pattern FIFO, RXSTALL on a full sample FIFO -- and stalling loses
 * nothing: READ_ADDR/WRITE_ADDR stay inside their rings and the next word
 * still lands in the next slot. The consumer notices the frozen write pointer
 * and re-triggers whichever channel is idle; nothing runs on a timer.
 *
 * Init is idempotent against a previous instance still running. A core-only
 * reset (mcu_reset(), the EEPROM CLEAN restart) leaves PIO1 and the DMA
 * channels going through crt0: the sample ring keeps being written at the same
 * .bss address, and the pattern ring reads zeros -- all rows LOW, harmless
 * against pulled-up columns -- until this init resets PIO1 (ours alone) and
 * aborts the two channels it is handed back. Every reset runs the same path
 * (era_invariants.md); nothing here branches on the cause. */

#include "quantum.h"
#include "era_rp2040_matrix.h"

#include <hal.h>
#include "hardware/pio.h"
/* The register struct only: pico-sdk's hardware/timer.h clashes with the
   ChibiOS TIMER macro once hal.h is in. */
#include "hardware/structs/timer.h"

#if !defined(ERA_RP2040_MATRIX_ENABLE)
#    error "era_rp2040_matrix_pio.c must be built only when ERA_RP2040_MATRIX_ENABLE is set."
#endif

#if !defined(RP_DMA_REQUIRED)
#    error "The PIO matrix sampler allocates through the ChibiOS DMA LLD; era_common_qmk_rules.mk emits RP_DMA_REQUIRED beside the selector."
#endif

#if !defined(IOPORT1)
#    error "ERA_RP2040_MATRIX_ENABLE requires an RP-style ChibiOS PAL port."
#endif

#if !defined(MATRIX_ROW_PINS) || !defined(MATRIX_COL_PINS)
#    error "ERA_RP2040_MATRIX_ENABLE requires MATRIX_ROW_PINS and MATRIX_COL_PINS."
#endif

#if !defined(DIODE_DIRECTION) || (DIODE_DIRECTION != COL2ROW)
#    error "ERA_RP2040_MATRIX_ENABLE currently supports only COL2ROW matrices."
#endif

#ifndef MATRIX_INPUT_PRESSED_STATE
#    define MATRIX_INPUT_PRESSED_STATE 0
#endif

/* The row-select drive: how long a driven row is held before the sample. The
   value is the CPU engine's, and the fixed baseline that rejected 32 and 64
   on device (era_performance_gates.md, Fixed Baselines) binds it here too. */
#ifndef ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY
#    define ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY 128
#endif

/* Cycles every row is released (all HIGH) between one row's sample and the
   next row's drive. The CPU engine released a row the instant its snapshot
   was in a register and drove the next after the decode, ~50-70 cycles later;
   64 keeps the column pull-up recovery time at least what it was. */
#ifndef ERA_RP2040_MATRIX_PIO_ROW_RELEASE_CYCLES
#    define ERA_RP2040_MATRIX_PIO_ROW_RELEASE_CYCLES 64
#endif

/* Frames the sample ring holds. Two would do -- the consumer only ever reads
   the newest complete frame -- but four puts three frames (~37 us) between a
   frame being read and the writer wrapping onto it, so a torn read needs core0
   to be held off for that long mid-decode. It costs 64 more bytes. */
#ifndef ERA_RP2040_MATRIX_PIO_SAMPLE_RING_FRAMES
#    define ERA_RP2040_MATRIX_PIO_SAMPLE_RING_FRAMES 4
#endif

/* Bound on the wait for the first complete frame at init, well above one
   frame at any board's geometry. bootmagic scans right after matrix_init(). */
#ifndef ERA_RP2040_MATRIX_PIO_FIRST_FRAME_TIMEOUT_US
#    define ERA_RP2040_MATRIX_PIO_FIRST_FRAME_TIMEOUT_US 500
#endif

/* The DMA IRQ priority handed to the ChibiOS allocator. It is applied to
   DMA_IRQ_0 only by the first channel core0 takes, and matrix_init() runs
   before rgb_matrix_init(), so this value is what the ws2812 driver's ISR now
   runs at: it is RP_DMA_PRIORITY_WS2812's default (ws2812_vendor.c) for that
   reason, not a choice of this unit's. This unit enables no channel IRQ. */
#ifndef ERA_RP2040_MATRIX_PIO_DMA_IRQ_PRIORITY
#    define ERA_RP2040_MATRIX_PIO_DMA_IRQ_PRIORITY 3
#endif

#define ERA_RP2040_MATRIX_PIO_ROW_T matrix_row_t
#include "era_rp2040_matrix_pio_frame.h"

/* Frame geometry, all compile-time. FRAME_WORDS is the smallest power of two
   holding one slot per row so the sample ring's frame boundaries sit on the
   DMA ring wrap; the wrap itself needs the ring aligned to its own byte size. */
#if MATRIX_ROWS_PER_HAND <= 1
#    define ERA_RP2040_MATRIX_PIO_FRAME_WORDS 1U
#elif MATRIX_ROWS_PER_HAND <= 2
#    define ERA_RP2040_MATRIX_PIO_FRAME_WORDS 2U
#elif MATRIX_ROWS_PER_HAND <= 4
#    define ERA_RP2040_MATRIX_PIO_FRAME_WORDS 4U
#elif MATRIX_ROWS_PER_HAND <= 8
#    define ERA_RP2040_MATRIX_PIO_FRAME_WORDS 8U
#elif MATRIX_ROWS_PER_HAND <= 16
#    define ERA_RP2040_MATRIX_PIO_FRAME_WORDS 16U
#elif MATRIX_ROWS_PER_HAND <= 32
#    define ERA_RP2040_MATRIX_PIO_FRAME_WORDS 32U
#else
#    error "The PIO matrix sampler frames at most 32 rows per hand."
#endif

#define ERA_RP2040_MATRIX_PIO_FRAME_BYTES (ERA_RP2040_MATRIX_PIO_FRAME_WORDS * 4U)

/* `#pragma GCC unroll N` does not macro-expand its operand; _Pragma with the
   usual two-step stringify does (the CPU engine carries the same note). */
#define ERA_RP2040_MATRIX_PIO_STR_(x) #x
#define ERA_RP2040_MATRIX_PIO_STR(x) ERA_RP2040_MATRIX_PIO_STR_(x)
#define ERA_RP2040_MATRIX_PIO_UNROLL(n) _Pragma(ERA_RP2040_MATRIX_PIO_STR(GCC unroll n))
#define ERA_RP2040_MATRIX_PIO_RING_WORDS (ERA_RP2040_MATRIX_PIO_SAMPLE_RING_FRAMES * ERA_RP2040_MATRIX_PIO_FRAME_WORDS)
#define ERA_RP2040_MATRIX_PIO_RING_BYTES (ERA_RP2040_MATRIX_PIO_RING_WORDS * 4U)

_Static_assert((ERA_RP2040_MATRIX_PIO_SAMPLE_RING_FRAMES & (ERA_RP2040_MATRIX_PIO_SAMPLE_RING_FRAMES - 1)) == 0, "sample ring frames must be a power of two");
_Static_assert(ERA_RP2040_MATRIX_PIO_SAMPLE_RING_FRAMES >= 4, "the torn-copy test needs two whole frames of margin behind the writer's own");
_Static_assert(ERA_RP2040_MATRIX_PIO_RING_BYTES <= 32768U, "RP2040 DMA ring wrap is at most 2^15 bytes");
_Static_assert(ERA_RP2040_MATRIX_PIO_FRAME_WORDS >= MATRIX_ROWS_PER_HAND, "frame must hold every row");
_Static_assert(MATRIX_COLS <= 8 * sizeof(matrix_row_t), "decode tables are matrix_row_t wide");
_Static_assert(ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY + ERA_RP2040_MATRIX_PIO_ROW_RELEASE_CYCLES <= 900, "the program encoder has 32 instructions; a settle+release beyond ~900 cycles no longer fits");

/* The two rings. Static, fixed, aligned to their own size for the DMA ring
   wrap; the residency budget takes them as ordinary .bss. */
static uint32_t era_rp2040_matrix_pio_pattern_ring[ERA_RP2040_MATRIX_PIO_FRAME_WORDS] __attribute__((aligned(ERA_RP2040_MATRIX_PIO_FRAME_BYTES)));
static uint32_t era_rp2040_matrix_pio_sample_ring[ERA_RP2040_MATRIX_PIO_RING_WORDS] __attribute__((aligned(ERA_RP2040_MATRIX_PIO_RING_BYTES)));

/* Byte-indexed decode tables for the booted hand, built at init. Four table
   reads and three ORs turn one 32-bit sample into one row; a byte holding no
   column of this hand is an all-zero table. */
static matrix_row_t era_rp2040_matrix_pio_decode_tables[4][256];

static const pin_t era_rp2040_matrix_pio_row_pins_left[MATRIX_ROWS_PER_HAND] = MATRIX_ROW_PINS;
static const pin_t era_rp2040_matrix_pio_col_pins_left[MATRIX_COLS]          = MATRIX_COL_PINS;
#if defined(SPLIT_KEYBOARD) && defined(MATRIX_ROW_PINS_RIGHT) && defined(MATRIX_COL_PINS_RIGHT)
static const pin_t era_rp2040_matrix_pio_row_pins_right[MATRIX_ROWS_PER_HAND] = MATRIX_ROW_PINS_RIGHT;
static const pin_t era_rp2040_matrix_pio_col_pins_right[MATRIX_COLS]          = MATRIX_COL_PINS_RIGHT;
#endif

typedef struct {
    const rp_dma_channel_t *pattern_channel;
    const rp_dma_channel_t *sample_channel;
    int                     sm;
    int                     program_offset;
    uint32_t                all_row_mask;
    uint32_t                last_write_addr;
    bool                    ready;
    /* Instrument counters, read by the diagnostics snapshot below. */
    uint32_t torn_retry_count;
    uint32_t rearm_count;
} era_rp2040_matrix_pio_state_t;

static era_rp2040_matrix_pio_state_t era_rp2040_matrix_pio_state;

static inline bool era_rp2040_matrix_pio_pin_has_sio_mask(pin_t pin) {
    return pin != NO_PIN && (uint32_t)pin <= 29U;
}

static inline uint32_t era_rp2040_matrix_pio_pin_mask(pin_t pin) {
    return era_rp2040_matrix_pio_pin_has_sio_mask(pin) ? (1UL << (uint32_t)pin) : 0U;
}

static void era_rp2040_matrix_pio_select_side(const pin_t **row_pins, const pin_t **col_pins) {
    *row_pins = era_rp2040_matrix_pio_row_pins_left;
    *col_pins = era_rp2040_matrix_pio_col_pins_left;
#if defined(SPLIT_KEYBOARD) && defined(MATRIX_ROW_PINS_RIGHT) && defined(MATRIX_COL_PINS_RIGHT)
    if (!is_keyboard_left()) {
        *row_pins = era_rp2040_matrix_pio_row_pins_right;
        *col_pins = era_rp2040_matrix_pio_col_pins_right;
    }
#endif
}

/* The DMA CTRL words. Neither channel raises an interrupt: IRQ_QUIET keeps the
   raw INTR bit down at every count exhaustion, and no INTE bit is ever set. */
static uint32_t era_rp2040_matrix_pio_pattern_ctrl(uint sm) {
    return DMA_CTRL_TRIG_DATA_SIZE_WORD | DMA_CTRL_TRIG_INCR_READ | DMA_CTRL_TRIG_RING_SIZE(era_rp2040_matrix_pio_log2(ERA_RP2040_MATRIX_PIO_FRAME_BYTES)) | DMA_CTRL_TRIG_TREQ_SEL(pio_get_dreq(pio1, sm, true)) | DMA_CTRL_TRIG_IRQ_QUIET;
}

static uint32_t era_rp2040_matrix_pio_sample_ctrl(uint sm) {
    return DMA_CTRL_TRIG_DATA_SIZE_WORD | DMA_CTRL_TRIG_INCR_WRITE | DMA_CTRL_TRIG_RING_SEL | DMA_CTRL_TRIG_RING_SIZE(era_rp2040_matrix_pio_log2(ERA_RP2040_MATRIX_PIO_RING_BYTES)) | DMA_CTRL_TRIG_TREQ_SEL(pio_get_dreq(pio1, sm, false)) | DMA_CTRL_TRIG_IRQ_QUIET;
}

static void era_rp2040_matrix_pio_channel_start(const rp_dma_channel_t *channel, const void *source, void *destination, uint32_t ctrl) {
    /* Abort first: after a core-only reset the channel we are handed may still
       be running the previous instance's transfer. */
    dmaChannelDisableX(channel);
    dmaChannelSetSourceX(channel, (uint32_t)source);
    dmaChannelSetDestinationX(channel, (uint32_t)destination);
    dmaChannelSetCounterX(channel, 0xFFFFFFFFU);
    dmaChannelSetModeX(channel, ctrl);
    dmaChannelEnableX(channel);
}

static inline bool era_rp2040_matrix_pio_channel_idle(const rp_dma_channel_t *channel) {
    return (channel->channel->CTRL_TRIG & DMA_CTRL_TRIG_BUSY) == 0U;
}

static inline void era_rp2040_matrix_pio_channel_retrigger(const rp_dma_channel_t *channel) {
    /* Reloads the transfer count and restarts with READ/WRITE_ADDR where the
       exhausted run left them -- inside the ring, on the next slot. */
    channel->dma->MULTI_CHAN_TRIGGER = channel->chnmask;
}

static bool era_rp2040_matrix_pio_start(const pin_t *row_pins, const pin_t *col_pins) {
    era_rp2040_matrix_pio_state_t *state = &era_rp2040_matrix_pio_state;

    /* Row masks and the pattern ring for this hand. */
    uint32_t row_masks[MATRIX_ROWS_PER_HAND];
    state->all_row_mask = 0;
    for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; row++) {
        row_masks[row] = era_rp2040_matrix_pio_pin_mask(row_pins[row]);
        state->all_row_mask |= row_masks[row];
    }
    era_rp2040_matrix_pio_patterns_build(era_rp2040_matrix_pio_pattern_ring, ERA_RP2040_MATRIX_PIO_FRAME_WORDS, row_masks, MATRIX_ROWS_PER_HAND);

    /* Column masks and their row bits, then the decode tables. */
    uint32_t     col_masks[MATRIX_COLS];
    matrix_row_t col_bits[MATRIX_COLS];
    matrix_row_t bit = MATRIX_ROW_SHIFTER;
    for (uint8_t col = 0; col < MATRIX_COLS; col++, bit <<= 1) {
        col_masks[col] = era_rp2040_matrix_pio_pin_mask(col_pins[col]);
        col_bits[col]  = bit;
    }
    era_rp2040_matrix_pio_decode_tables_build(era_rp2040_matrix_pio_decode_tables, col_masks, col_bits, MATRIX_COLS, MATRIX_INPUT_PRESSED_STATE);

    /* PIO1 is this unit's alone: reset it whole so a previous instance -- a
       core-only reset does not stop it -- leaves no state machine, program or
       FDEBUG bit behind, then bring it back. */
    hal_lld_peripheral_reset(RESETS_ALLREG_PIO1);
    hal_lld_peripheral_unreset(RESETS_ALLREG_PIO1);

    uint16_t program_words[ERA_RP2040_MATRIX_PIO_PROGRAM_MAX_INSTRUCTIONS];
    size_t   program_len = era_rp2040_matrix_pio_program_encode(program_words, ERA_RP2040_MATRIX_PIO_PROGRAM_MAX_INSTRUCTIONS, ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY, ERA_RP2040_MATRIX_PIO_ROW_RELEASE_CYCLES);
    if (program_len == 0) {
        return false;
    }
    const pio_program_t program = {
        .instructions = program_words,
        .length       = (uint8_t)program_len,
        .origin       = -1,
    };
    if (!pio_can_add_program(pio1, &program)) {
        return false;
    }
    state->program_offset = (int)pio_add_program(pio1, &program);
    state->sm             = pio_claim_unused_sm(pio1, false);
    if (state->sm < 0) {
        return false;
    }
    uint sm     = (uint)state->sm;
    uint offset = (uint)state->program_offset;

    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, offset, offset + (uint)program_len - 1U);
    sm_config_set_out_pins(&config, 0, ERA_RP2040_MATRIX_PIO_PIN_COUNT);
    sm_config_set_in_pins(&config, 0);
    sm_config_set_out_shift(&config, true, true, 32);
    sm_config_set_in_shift(&config, true, true, 32);
    sm_config_set_clkdiv_int_frac(&config, 1, 0);
    pio_sm_init(pio1, sm, offset, &config);

    /* Present the rows' rest state through the state machine before the pins
       are handed to it: every row HIGH, output enabled. Then the function
       select moves SIO -> PIO1 with the pad register byte-identical to the
       push-pull mode the CPU engine used (input enable only). */
    pio_sm_set_pins_with_mask(pio1, sm, state->all_row_mask, state->all_row_mask);
    pio_sm_set_pindirs_with_mask(pio1, sm, state->all_row_mask, state->all_row_mask);
    for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; row++) {
        if (era_rp2040_matrix_pio_pin_has_sio_mask(row_pins[row])) {
            palSetLineMode(row_pins[row], PAL_RP_IOCTRL_FUNCSEL_PIO1 | PAL_RP_PAD_IE);
        }
    }

    /* Two channels from the same allocator the ws2812 driver uses, so neither
       can be handed out twice. Allocated once; a re-entered init reuses them. */
    if (state->pattern_channel == NULL) {
        state->pattern_channel = dmaChannelAlloc(RP_DMA_CHANNEL_ID_ANY, ERA_RP2040_MATRIX_PIO_DMA_IRQ_PRIORITY, NULL, NULL);
    }
    if (state->sample_channel == NULL) {
        state->sample_channel = dmaChannelAlloc(RP_DMA_CHANNEL_ID_ANY, ERA_RP2040_MATRIX_PIO_DMA_IRQ_PRIORITY, NULL, NULL);
    }
    if (state->pattern_channel == NULL || state->sample_channel == NULL) {
        return false;
    }

    pio_sm_clear_fifos(pio1, sm);
    pio1->fdebug = 0xFFFFFFFFU;
    era_rp2040_matrix_pio_channel_start(state->pattern_channel, era_rp2040_matrix_pio_pattern_ring, (void *)&pio1->txf[sm], era_rp2040_matrix_pio_pattern_ctrl(sm));
    era_rp2040_matrix_pio_channel_start(state->sample_channel, (const void *)&pio1->rxf[sm], era_rp2040_matrix_pio_sample_ring, era_rp2040_matrix_pio_sample_ctrl(sm));
    pio_sm_set_enabled(pio1, sm, true);

    /* Bounded wait for the first complete frame, so the first matrix_scan()
       after init -- bootmagic's -- reads a real sample rather than zeros. The
       transfer counter counts down from its reload, so words moved is a
       monotonic read that no ring wrap can fold. */
    uint32_t start_us = timer_hw->timerawl;
    while ((uint32_t)(0xFFFFFFFFU - state->sample_channel->channel->TRANS_COUNT) < ERA_RP2040_MATRIX_PIO_FRAME_WORDS) {
        if ((uint32_t)(timer_hw->timerawl - start_us) >= ERA_RP2040_MATRIX_PIO_FIRST_FRAME_TIMEOUT_US) {
            break;
        }
    }
    state->last_write_addr = 0;
    state->ready           = true;
    return true;
}

void era_rp2040_matrix_init_pins(void) {
    const pin_t *row_pins;
    const pin_t *col_pins;
    era_rp2040_matrix_pio_select_side(&row_pins, &col_pins);

    /* Columns exactly as the CPU engine leaves them: SIO input, pull-up. */
    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        if (col_pins[col] != NO_PIN) {
            ATOMIC_BLOCK_FORCEON {
                gpio_set_pin_input_high(col_pins[col]);
            }
        }
    }

    era_rp2040_matrix_pio_state.ready = false;
    (void)era_rp2040_matrix_pio_start(row_pins, col_pins);
}

/* One pass: the newest complete frame, decoded into raw_rows[]. Returns
   whether any raw row changed against the previous pass, which is what the
   debounce runtime takes as its `changed` argument.

   Two steps, in this order for a reason: the frame's row words are first
   copied out of the ring -- a handful of loads, so the window in which the
   writer could lap the frame is as short as it can be, and a lapped copy is
   simply taken again -- and only the local copy is decoded into raw_rows[].
   Decoding straight out of the ring would either race the writer for the
   whole decode or need a second buffer and a second pass to know what
   changed; the copy is that buffer at its cheapest. Both loops are unrolled
   deliberately (the trip count is a constant, the build is -Os and does not
   unroll on its own): rolled, Cortex-M0+ spilled the pointers and the bound
   to the stack on every row. Check the ELF after touching this. */
bool era_rp2040_matrix_update_raw_rows(matrix_row_t raw_rows[]) {
    era_rp2040_matrix_pio_state_t *state = &era_rp2040_matrix_pio_state;
    if (!state->ready) {
        return false;
    }

    const DMA_Channel_Typedef *sample      = state->sample_channel->channel;
    const uint32_t             base        = (uint32_t)era_rp2040_matrix_pio_sample_ring;
    const uint32_t             frame_shift = era_rp2040_matrix_pio_log2(ERA_RP2040_MATRIX_PIO_FRAME_BYTES);
    const uint32_t             words_shift = era_rp2040_matrix_pio_log2(ERA_RP2040_MATRIX_PIO_FRAME_WORDS);

    uint32_t samples[MATRIX_ROWS_PER_HAND];
    uint32_t write_addr = sample->WRITE_ADDR;
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        uint32_t        count_before = sample->TRANS_COUNT;
        uint32_t        offset       = era_rp2040_matrix_pio_latest_complete_frame_offset(write_addr, base, frame_shift, ERA_RP2040_MATRIX_PIO_SAMPLE_RING_FRAMES - 1U, words_shift);
        const uint32_t *frame        = &era_rp2040_matrix_pio_sample_ring[offset];
        ERA_RP2040_MATRIX_PIO_UNROLL(MATRIX_ROWS_PER_HAND)
        for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; row++) {
            samples[row] = frame[row];
        }
        uint32_t count_after = sample->TRANS_COUNT;
        if (!era_rp2040_matrix_pio_copy_torn(count_before - count_after, ERA_RP2040_MATRIX_PIO_FRAME_WORDS, ERA_RP2040_MATRIX_PIO_SAMPLE_RING_FRAMES)) {
            break;
        }
        /* The writer moved far enough during the copy to have reached the
           copied frame; take it again from where the writer is now. A second
           tear is accepted -- the debounce absorbs one pass -- and counted
           either way. */
        state->torn_retry_count++;
        write_addr = sample->WRITE_ADDR;
    }

    /* A write pointer that has not moved since the last pass is a stalled
       sampler: a passes-per-frame ratio above one (not this board), or a
       channel whose transfer count ran out. Re-trigger whichever is idle;
       the ring positions are intact. */
    if (write_addr == state->last_write_addr) {
        if (era_rp2040_matrix_pio_channel_idle(state->sample_channel)) {
            era_rp2040_matrix_pio_channel_retrigger(state->sample_channel);
            state->rearm_count++;
        }
        if (era_rp2040_matrix_pio_channel_idle(state->pattern_channel)) {
            era_rp2040_matrix_pio_channel_retrigger(state->pattern_channel);
            state->rearm_count++;
        }
    }
    state->last_write_addr = write_addr;

    bool changed = false;
    ERA_RP2040_MATRIX_PIO_UNROLL(MATRIX_ROWS_PER_HAND)
    for (uint8_t row = 0; row < MATRIX_ROWS_PER_HAND; row++) {
        matrix_row_t value = era_rp2040_matrix_pio_decode_row(era_rp2040_matrix_pio_decode_tables, samples[row]);
        changed |= value != raw_rows[row];
        raw_rows[row] = value;
    }
    return changed;
}

void era_rp2040_matrix_pio_get_diagnostics(era_rp2040_matrix_pio_diagnostics_t *snapshot) {
    const era_rp2040_matrix_pio_state_t *state = &era_rp2040_matrix_pio_state;
    if (snapshot == NULL) {
        return;
    }
    snapshot->ready         = state->ready ? 1U : 0U;
    snapshot->frame_words   = ERA_RP2040_MATRIX_PIO_FRAME_WORDS;
    snapshot->torn_retries  = state->torn_retry_count;
    snapshot->rearms        = state->rearm_count;
    /* Words the sample channel has moved in its current run: the transfer
       count reloads to 0xFFFFFFFF on every (re)trigger and counts down. A
       reader takes deltas and treats a `rearms` change inside its window as
       a discontinuity. */
    snapshot->sample_words = state->ready ? (0xFFFFFFFFU - state->sample_channel->channel->TRANS_COUNT) : 0U;
    /* FDEBUG for our state machine only, packed as TXSTALL:TXOVER:RXUNDER:
       RXSTALL in bits 3..0. RXSTALL says the DMA fell behind the sampler,
       TXSTALL that the pattern feed did; both read zero on a healthy sampler
       outside a re-arm gap. Sticky until cleared. */
    if (state->ready) {
        uint32_t fdebug = pio1->fdebug;
        uint     sm     = (uint)state->sm;
        snapshot->fdebug = (uint8_t)((((fdebug >> (24U + sm)) & 1U) << 3) | (((fdebug >> (16U + sm)) & 1U) << 2) | (((fdebug >> (8U + sm)) & 1U) << 1) | ((fdebug >> sm) & 1U));
    } else {
        snapshot->fdebug = 0;
    }
}

void era_rp2040_matrix_pio_clear_fdebug(void) {
    if (era_rp2040_matrix_pio_state.ready) {
        uint sm      = (uint)era_rp2040_matrix_pio_state.sm;
        pio1->fdebug = (1U << (24U + sm)) | (1U << (16U + sm)) | (1U << (8U + sm)) | (1U << sm);
    }
}
