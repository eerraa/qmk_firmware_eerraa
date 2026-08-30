// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_communication_core_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "hardware/structs/psm.h"
#include "hardware/structs/timer.h"
#include "era_split_communication_core_owner.h"
#include "era_split_communication_core_standing.h"
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "era_split_communication_core_storage_service.h"
#endif
#include "../era_split_transaction_backend.h"
#include "../era_split_transaction_engine.h"

#if !defined(MCU_RP)
#    error "ERA split communication core MVP-0 requires RP2040."
#endif

/* Measured, not assumed. Deepest static frame sum reachable from
 * era_split_communication_core_entry(), summed from the linked wire-profile
 * disassembly (push register counts plus `sub sp, #imm`, following `bl` edges,
 * recursion reported rather than followed):
 *
 *   53da6101a4, before Slice 11        888 B
 *   Slice 11 (3c5783bcdc)              952 B
 *
 * The deepest chain is the initiator path -- process_initiator() alone spends
 * 496 B on its local era_split_wire_frame_t, whose payload array is sized for
 * the 264-byte bulk page. Add the Cortex-M0+ 32-byte exception frame, since
 * core1 takes its deadline IRQ anywhere in that chain and the handler itself
 * is a zero-frame leaf: worst case 984 B against the old 1024 B reservation,
 * which is 40 bytes of headroom and not a margin.
 *
 * So the reservation doubles to 2 KiB. SRAM5 is 4 KiB with a 256-byte boot
 * carve-out at its top and holds only this stack and the 192-byte core1 vector
 * table, so 2 KiB still leaves about 1.6 KiB unclaimed.
 *
 * This is a static figure and it is not a watermark: it cannot see an
 * inlining change and it assumes no recursion, which the walk verified rather
 * than assumed. The device watermark is the other half and is an owner-session
 * gate item. */
#ifndef ERA_SPLIT_COMMUNICATION_CORE_STACK_WORDS
#    define ERA_SPLIT_COMMUNICATION_CORE_STACK_WORDS 512
#endif

#ifndef ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_TIMEOUT_US
#    define ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_TIMEOUT_US 10000U
#endif

/* Sequence restarts allowed inside one launch handshake before it gives up.
 * The reasoning for the value is at the loop that reads it. */
#ifndef ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_MAX_RESTARTS
#    define ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_MAX_RESTARTS 4U
#endif

/* The state-handshake bound is 30000 us because the standing heartbeat
   exchange is built: a core1 that may be inside that exchange when core0 waits
   on a state transition needs the wider window, and the 10000 us this used to
   fall back to was the no-heartbeat stage's value. That stage is retired --
   era_split_qmk_rules.mk $(error)s if ERA_SPLIT_COMMUNICATION_CORE_HEARTBEAT_ENABLE
   (or the other five legacy stage vars) is set, does not -D them, and
   hard-errors on any stage but CORE1_FULL -- so the fallback was unreachable,
   and unreachable is the dangerous half here: a sweep that took the macro
   would have cut a live timeout to a third with nothing to say so. */
#ifndef ERA_SPLIT_COMMUNICATION_CORE_STATE_TIMEOUT_US
#    define ERA_SPLIT_COMMUNICATION_CORE_STATE_TIMEOUT_US 30000U
#endif

/* R7's per-boot launch-attempt cap: consecutive failures to bring core1 into
 * service before the half gives up on it for this boot. The one genuinely
 * transient case — core1 not yet at the bootrom wait loop — settles in two or
 * three attempts; the measured pathological case retried 51,931 times at one
 * attempt per scan with ~10 ms of each pass inside a read timeout. Eight is
 * headroom over the transient case, not a computed budget, and a boot that
 * never fails never reads it. The streak counts handshake failures, entry
 * timeouts, and — since the 2026-08-06 fix — post-launch service timeouts
 * (the owner layer's ready wait expiring), and it clears only when core1 is
 * observed *in service* (ready published), not when start() returns: the
 * kill leg proved a core1 can complete its handshake, publish launched and
 * running, and be dead one loop pass later, which start()'s own success
 * cannot see. Per boot is the bound, and the give-up state is LOCAL_NO_LINK
 * via the scheduler's read of the latch. */
#ifndef ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_ATTEMPT_CAP
#    define ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_ATTEMPT_CAP 8U
#endif

/* Bound on the PSM force-off acknowledge poll in declare_dead(). The ack is
 * a handful of clk_sys cycles; a bound this wide only exists because an
 * unbounded register wait is not a shape this project ships. */
#ifndef ERA_SPLIT_COMMUNICATION_CORE_PSM_ACK_SPINS
#    define ERA_SPLIT_COMMUNICATION_CORE_PSM_ACK_SPINS 4096U
#endif

/* The floor is the measurement above rounded up, plus margin: 984 B is 246
 * words, and 288 words (1152 B) carries 17% over it. It used to be 256 under
 * CORE1_FULL, which was also what STACK_WORDS was -- so the assert below
 * compared 256 against 256 and could only catch someone lowering the
 * reservation to exactly the value it already had. Worse, 256 words is 1024 B,
 * below the measured worst case, so the old floor would have passed a
 * reservation that overflows.
 *
 * What the assert still cannot do is catch added call depth: neither macro is
 * derived from the call graph, and no compile-time construct here is. That is
 * why the ELF gate now asks for the figure instead, and why the comment above
 * carries the method rather than only the number. */
#define ERA_SPLIT_COMMUNICATION_CORE_MIN_STACK_WORDS 288

_Static_assert(ERA_SPLIT_COMMUNICATION_CORE_STACK_WORDS >= ERA_SPLIT_COMMUNICATION_CORE_MIN_STACK_WORDS, "ERA communication core stack is below the measured core1 worst case for the selected stage.");

era_split_communication_core_state_t g_era_split_communication_core;
/* Core1-private hot data lives in the SRAM5 scratch bank (scratch banks
 * are per-core private on RP2040): core1 stack and vector fetches never
 * contend with core0 traffic in the striped main SRAM. Both stay below
 * the ram7 boot carve-out the ROM scribbles during reset. */
static uint32_t g_era_split_communication_core_stack[ERA_SPLIT_COMMUNICATION_CORE_STACK_WORDS] __attribute__((aligned(8), section(".ram5_clear.era_core1_stack")));
enum {
    ERA_SPLIT_COMMUNICATION_CORE_VECTOR_COUNT = 48,
    ERA_SPLIT_COMMUNICATION_CORE_TIMER_IRQ3_VECTOR = 16 + RP_TIMER_IRQ3_NUMBER,
};
static uint32_t g_era_split_communication_core_vector_table[ERA_SPLIT_COMMUNICATION_CORE_VECTOR_COUNT] __attribute__((aligned(256), section(".ram5_clear.era_core1_vectors")));

static uint32_t era_split_communication_core_elapsed_us(uint32_t start_us) {
    return (uint32_t)(timer_hw->timerawl - start_us);
}

/* The three core0-side bounded waits below poll their deadline instead of
 * parking on __WFE() between checks. The WFE form put every bound behind a
 * wake the waited-on core must send: with core1 dead, and the main loop
 * stalled inside the wait so core0's own interrupt sources fall silent too,
 * the deadline check was never reached again — device-observed on the R7
 * kill image at the owner layer's revoke wait (the full account is at
 * era_split_communication_core_owner_wait_word()). Same defect shape here,
 * fixed in the same commit: a bound must hold by construction. Core1's own
 * WFE parks are untouched — parking is what they are for, and core0's SEV
 * is their wake. */
static bool era_split_communication_core_fifo_write_timeout(uint32_t data, uint32_t timeout_us) {
    uint32_t start_us = timer_hw->timerawl;
    while (!fifoIsWriteNotFull()) {
        if (era_split_communication_core_elapsed_us(start_us) >= timeout_us) {
            return false;
        }
    }

    SIO->FIFO_WR = data;
    __SEV();
    return true;
}

static bool era_split_communication_core_fifo_read_timeout(uint32_t *data, uint32_t timeout_us) {
    if (data == NULL) {
        return false;
    }

    uint32_t start_us = timer_hw->timerawl;
    while (!fifoIsReadNotEmpty()) {
        if (era_split_communication_core_elapsed_us(start_us) >= timeout_us) {
            return false;
        }
    }

    *data = SIO->FIFO_RD;
    __SEV();
    return true;
}

enum {
    ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_ERROR_PHASE_NONE  = 0,
    ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_ERROR_PHASE_WRITE = 1,
    ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_ERROR_PHASE_READ  = 2,
    ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_ERROR_PHASE_ECHO  = 3,
};

static void era_split_communication_core_note_launch_error(uint8_t stage, uint8_t phase) {
    g_era_split_communication_core.launch_error       = 1;
    g_era_split_communication_core.launch_error_stage = stage;
    g_era_split_communication_core.launch_error_phase = phase;
    g_era_split_communication_core.launch_error_count++;
}

static void era_split_communication_core_note_entry_timeout(void) {
    g_era_split_communication_core.entry_timeout = 1;
    g_era_split_communication_core.entry_timeout_count++;
}

static void era_split_communication_core_note_stop_timeout(void) {
    g_era_split_communication_core.stop_timeout = 1;
    g_era_split_communication_core.stop_timeout_count++;
}

static bool era_split_communication_core_wait_state(volatile uint8_t *value, uint8_t expected, uint32_t timeout_us) {
    uint32_t start_us = timer_hw->timerawl;
    while (*value != expected) {
        if (era_split_communication_core_elapsed_us(start_us) >= timeout_us) {
            return false;
        }
    }

    return true;
}

static void era_split_communication_core_entry(void) {
    g_era_split_communication_core.launched = 1;
    g_era_split_communication_core.running  = 1;
    __DMB();
    __SEV();

    for (;;) {
        __DMB();
        bool backend_owner_ready = era_split_communication_core_owner_core1_service();
        era_split_transaction_engine_publish_diagnostics_snapshot();
        if (g_era_split_communication_core.stop_requested) {
            (void)era_split_communication_core_owner_core1_service();
            if (g_era_split_communication_core.running) {
                g_era_split_communication_core.running = 0;
                __DMB();
                __SEV();
            }
            g_era_split_communication_core.idle_count++;
            __DMB();
            __WFE();
            continue;
        }

        if (!g_era_split_communication_core.running) {
            g_era_split_communication_core.running = 1;
            __DMB();
            __SEV();
        }
        if (g_era_split_communication_core.wake_pending) {
            g_era_split_communication_core.wake_pending = 0;
            g_era_split_communication_core.wake_observed_count++;
        }
        era_split_transaction_backend_role_t backend_role = era_split_communication_core_owner_current_role();
        bool processed_queue = backend_owner_ready && backend_role == ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR &&
                               era_split_communication_core_process_queue_once();
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
        bool processed_storage = backend_owner_ready && backend_role == ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR &&
                                 !processed_queue &&
                                 era_split_communication_core_storage_service_initiator_once(era_split_communication_core_owner_epoch());
#else
        bool processed_storage = false;
#endif
        /* The standing DUAL-HOST exchange, and its position in this loop is
           the whole of its route priority on this side. It runs only on a pass
           where the request queue and the storage lane did nothing, so a core0
           SESSION_STATUS, a storage step and a cancel all preempt it without
           any interlock of their own. Core0 owns the rest of route priority
           through the plan's enable bit. */
        bool processed_standing = backend_owner_ready && backend_role == ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR &&
                                  !processed_queue && !processed_storage &&
                                  era_split_communication_core_standing_service_once(era_split_communication_core_owner_epoch());
        bool responder_active = backend_owner_ready && backend_role == ERA_SPLIT_TRANSACTION_BACKEND_ROLE_RESPONDER;
        if (responder_active) {
            (void)era_split_communication_core_responder_service_once(era_split_communication_core_owner_epoch());
        }
        if (processed_queue || processed_storage || processed_standing || responder_active) {
            era_split_transaction_engine_publish_diagnostics_snapshot();
        }
        g_era_split_communication_core.loop_count++;
        __DMB();
        if (processed_queue || processed_storage || processed_standing || responder_active) {
            continue;
        }
        g_era_split_communication_core.idle_count++;
        __DMB();
#ifdef ERA_SPLIT_CORE1_PARK_DIAGNOSTICS_ENABLE
        /* The idle park's duration, for the sleep-share instrument. Diagnostic
           images only, and the two timer reads sit outside the pass. */
        uint32_t idle_start_us = timer_hw->timerawl;
        __WFE();
        g_era_split_communication_core.idle_us += (uint32_t)(timer_hw->timerawl - idle_start_us);
#else
        __WFE();
#endif
    }
}

static bool era_split_communication_core_sio_irq0_enabled(void) {
    return (NVIC->ISER[0] & (1UL << RP_SIO_IRQ_PROC0_NUMBER)) != 0U;
}

static bool era_split_communication_core_launch_sequence(void) {
    uint32_t *stack_end = &g_era_split_communication_core_stack[ERA_SPLIT_COMMUNICATION_CORE_STACK_WORDS];
    const uint32_t *source_vectors = (const uint32_t *)(uintptr_t)SCB->VTOR;
    for (uint8_t index = 0; index < ERA_SPLIT_COMMUNICATION_CORE_VECTOR_COUNT; index++) {
        g_era_split_communication_core_vector_table[index] = source_vectors[index];
    }
    g_era_split_communication_core_vector_table[ERA_SPLIT_COMMUNICATION_CORE_TIMER_IRQ3_VECTOR] =
        (uint32_t)(uintptr_t)era_split_transaction_backend_core1_deadline_irq;
    __DMB();
    uint32_t core1_vector_table = (uint32_t)(uintptr_t)g_era_split_communication_core_vector_table;
    uint32_t  commands[] = {
         0U,
         0U,
         1U,
         core1_vector_table,
         (uint32_t)(uintptr_t)stack_end,
         (uint32_t)(uintptr_t)era_split_communication_core_entry,
    };
    /* The pico-SDK six-word handshake, plus the one bound it does not have.
       Each FIFO write and read is already capped at LAUNCH_TIMEOUT_US and a
       failure of either returns from here, so a core1 that is halted, absent,
       or silent falls out in one 10 ms read timeout at seq 0. What was
       unbounded is the mismatch arm: restarting the sequence on every wrong
       echo means a core1 that answers in time and answers wrong keeps core0
       in here forever. That is reached from keyboard post-init, so "forever"
       is before the first main-loop pass - no housekeeping, no watchdog (no
       WATCHDOG_CTRL enable is written anywhere in this image), and no recovery
       but removing power.

       A correct launch never restarts. The two leading zeros already
       resynchronize the peer core from any state - fifoFlushRead() drops a
       stale word, and the bootrom's wait loop resets its own state machine on
       a zero and echoes it back - so the first restart already means the
       responder is not that wait loop. Four is headroom against that reasoning
       being wrong rather than a computed budget, and it costs nothing on
       working hardware: a sequence that never mismatches never reads it. */
    uint8_t seq      = 0;
    uint8_t restarts = 0;

    while (seq < (uint8_t)(sizeof(commands) / sizeof(commands[0]))) {
        uint32_t command = commands[seq];
        uint32_t response;

        if (command == 0U) {
            fifoFlushRead();
        }
        if (!era_split_communication_core_fifo_write_timeout(command, ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_TIMEOUT_US)) {
            era_split_communication_core_note_launch_error(seq, ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_ERROR_PHASE_WRITE);
            return false;
        }
        if (!era_split_communication_core_fifo_read_timeout(&response, ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_TIMEOUT_US)) {
            era_split_communication_core_note_launch_error(seq, ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_ERROR_PHASE_READ);
            return false;
        }
        if (response == command) {
            seq = (uint8_t)(seq + 1U);
            continue;
        }
        if (restarts >= ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_MAX_RESTARTS) {
            /* Report the step the final mismatch happened at, not the zero it
               would have reset to: that is what separates "never answered the
               first word" from "walked most of the sequence and then broke". */
            era_split_communication_core_note_launch_error(seq, ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_ERROR_PHASE_ECHO);
            return false;
        }
        restarts = (uint8_t)(restarts + 1U);
        seq      = 0U;
    }

    return true;
}

static bool era_split_communication_core_launch(void) {
    bool sio_irq0_was_enabled = era_split_communication_core_sio_irq0_enabled();
    bool launched;

    if (sio_irq0_was_enabled) {
        NVIC_DisableIRQ((IRQn_Type)RP_SIO_IRQ_PROC0_NUMBER);
    }

    launched = era_split_communication_core_launch_sequence();

    if (sio_irq0_was_enabled) {
        NVIC_EnableIRQ((IRQn_Type)RP_SIO_IRQ_PROC0_NUMBER);
    }

    return launched;
}

void era_split_communication_core_init(void) {
    if (g_era_split_communication_core.initialized) {
        return;
    }

    memset((void *)&g_era_split_communication_core, 0, sizeof(g_era_split_communication_core));
    g_era_split_communication_core.initialized = 1;
    __DMB();
}

/* R7: kill and reset a core1 that has been judged dead, so the next start()
 * finds launched==0 and runs the full six-word handshake against the bootrom
 * wait loop a PSM-reset core re-enters — which is the pico-SDK's own launch
 * precondition ("core 1 must previously have been reset"). Clearing the
 * lifecycle words here is what makes start() need no dead-core branch of its
 * own, and it also retires the false-success hazard: a core that died with
 * running==1 left that flag lying in RAM, and start() would have trusted it.
 *
 * The judgment does not live here. The owner layer calls this after the
 * revoke wait times out consecutively, because a live core1 answers a revoke
 * at the top of every loop pass and a wedged one, by the core's own design
 * contract (bounded waits, no flash, no ChibiOS suspends), has nothing legal
 * to be doing for that long. Resetting a genuinely live core through this
 * path is therefore a judgment error upstream, not a hazard this function
 * can add to. */
void era_split_communication_core_declare_dead(void) {
    era_split_communication_core_init();

    hw_set_bits(&psm_hw->frce_off, PSM_FRCE_OFF_PROC1_BITS);
    for (uint32_t spin = 0; spin < ERA_SPLIT_COMMUNICATION_CORE_PSM_ACK_SPINS; spin++) {
        if ((psm_hw->frce_off & PSM_FRCE_OFF_PROC1_BITS) != 0U) {
            break;
        }
    }
    hw_clear_bits(&psm_hw->frce_off, PSM_FRCE_OFF_PROC1_BITS);

    g_era_split_communication_core.launched       = 0;
    g_era_split_communication_core.running        = 0;
    g_era_split_communication_core.stop_requested = 0;
    g_era_split_communication_core.wake_pending   = 0;
    g_era_split_communication_core.dead_declared_count++;
    __DMB();
}

bool era_split_communication_core_launch_capped(void) {
    era_split_communication_core_init();
    return g_era_split_communication_core.launch_capped != 0;
}

uint32_t era_split_communication_core_progress_count(void) {
    /* Core1 publishes loop_count after the selected service returns, or just
     * before an idle WFE when no service ran. Queue, storage, standing and
     * idle progress therefore share one existing writer; a service still in
     * flight has not advanced it yet. */
    __DMB();
    return g_era_split_communication_core.loop_count;
}

static void era_split_communication_core_note_launch_failure(void) {
    if (g_era_split_communication_core.launch_failure_streak < 0xFFU) {
        g_era_split_communication_core.launch_failure_streak++;
    }
    if (g_era_split_communication_core.launch_failure_streak >= ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_ATTEMPT_CAP) {
        g_era_split_communication_core.launch_capped = 1;
    }
    __DMB();
}

/* R7 fix (2026-08-06): the owner layer reports the outcome of bringing core1
 * into service, and that report is what moves the give-up streak — not
 * start()'s own return. The kill leg's evidence: a relaunched core1 completed
 * every handshake (ss=5 sf=0 on the wedged half's breadcrumb) and died at its
 * first loop pass, so a streak reset at start() success made the cap
 * structurally unreachable and the half could never converge to
 * LOCAL_NO_LINK. Serviced means the owner observed ready published for the
 * new lease; a service timeout is the ready wait expiring, which counts into
 * the same streak the handshake failures use. */
void era_split_communication_core_note_core1_serviced(void) {
    era_split_communication_core_init();
    /* Early-out keeps the fast-path callers (the ensure and serial-reset
       live-lease observations, which can sit on scan-adjacent paths) at one
       byte read in the ordinary streak==0 case. */
    if (g_era_split_communication_core.launch_failure_streak == 0U) {
        return;
    }
    g_era_split_communication_core.launch_failure_streak = 0;
    __DMB();
}

void era_split_communication_core_note_core1_service_timeout(void) {
    era_split_communication_core_init();
    era_split_communication_core_note_launch_failure();
}

bool era_split_communication_core_start(void) {
    era_split_communication_core_init();
    if (g_era_split_communication_core.launch_capped) {
        /* The per-boot cap latched: refuse in microseconds rather than spend
           another 10 ms handshake timeout. The scheduler's capped read is
           what ends the retries; this early return is what they cost until
           the next plan pass notices. */
        return false;
    }
    g_era_split_communication_core.start_count++;
    g_era_split_communication_core.stop_requested      = 0;
    g_era_split_communication_core.wake_pending        = 1;
    g_era_split_communication_core.launch_error        = 0;
    g_era_split_communication_core.launch_error_stage  = 0;
    g_era_split_communication_core.launch_error_phase  = ERA_SPLIT_COMMUNICATION_CORE_LAUNCH_ERROR_PHASE_NONE;
    g_era_split_communication_core.entry_timeout       = 0;
    g_era_split_communication_core.stop_timeout        = 0;
    __DMB();

    if (!g_era_split_communication_core.launched) {
        g_era_split_communication_core.launch_attempted = 1;
        if (!era_split_communication_core_launch()) {
            g_era_split_communication_core.launch_attempted = 0;
            era_split_communication_core_note_launch_failure();
            return false;
        }
    } else {
        __SEV();
    }

    if (!era_split_communication_core_wait_state(&g_era_split_communication_core.launched, 1, ERA_SPLIT_COMMUNICATION_CORE_STATE_TIMEOUT_US) ||
        !era_split_communication_core_wait_state(&g_era_split_communication_core.running, 1, ERA_SPLIT_COMMUNICATION_CORE_STATE_TIMEOUT_US)) {
        era_split_communication_core_note_entry_timeout();
        era_split_communication_core_note_launch_failure();
        return false;
    }

    /* Deliberately no streak reset here (R7 fix, 2026-08-06). A start() that
       returns true has only proven the handshake and the entry flags; the
       kill leg proved that is compatible with a core1 already dead one loop
       pass later. The streak clears at note_core1_serviced(), when the owner
       layer observes ready published. */
    return true;
}

/* Ask core1 to stop servicing the wire, and wait until it has. It does not
 * stop core1, which is why it is no longer called stop(): it revokes the owner
 * lease, raises stop_requested, DMB+SEV, and waits up to 30 ms for running to
 * read 0. Core1 stays in its for(;;) and parks on WFE, so a later start() wakes
 * it with no relaunch. Reversible, which is the whole point of it.
 *
 * Do not "fix" this into an actual stop. The live recovery paths depend on
 * being able to bring core1 back: the halt that really stops the processor
 * leaves the owner lease pointing at a dead core, and owner_transfer_role()'s
 * ordinary path waits on core1's release_word before start(). Since R7 that
 * wait is no longer the only exit — consecutive revoke timeouts make the
 * owner layer declare the core dead, reclaim the lease, and reset the core
 * for a full relaunch (declare_dead above) — but that is the judged-dead
 * path, not a substitute for this reversible park. The boot-time hardware
 * halt still runs only from the pre-copy path, where "core0 is about to
 * restart" is guaranteed rather than assumed - see
 * system/era_boot_core1_halt.c. */
bool era_split_communication_core_request_quiesce(void) {
    era_split_communication_core_init();
    if (!era_split_communication_core_owner_transfer_role(ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE,
                                                           ERA_SPLIT_TRANSACTION_BACKEND_ROLE_DISABLED)) {
        return false;
    }
    g_era_split_communication_core.stop_count++;
    g_era_split_communication_core.stop_requested = 1;
    g_era_split_communication_core.stop_timeout   = 0;
    __DMB();
    __SEV();

    if (g_era_split_communication_core.launched && !era_split_communication_core_wait_state(&g_era_split_communication_core.running, 0, ERA_SPLIT_COMMUNICATION_CORE_STATE_TIMEOUT_US)) {
        era_split_communication_core_note_stop_timeout();
        return false;
    }

    return true;
}

void era_split_communication_core_wake(void) {
    era_split_communication_core_init();
    g_era_split_communication_core.wake_count++;
    g_era_split_communication_core.wake_pending = 1;
    __DMB();
    __SEV();
}
