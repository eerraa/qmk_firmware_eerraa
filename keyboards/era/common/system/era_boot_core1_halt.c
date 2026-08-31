// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* The pre-copy window: the inbound core1 hardware halt, and the double-tap
 * bootloader arm.
 *
 * Two jobs, one function, because the hardware offers exactly one call before
 * crt0 rebuilds SRAM and both jobs need to be on that side of it. They are
 * unrelated to each other; what they share is the window, and this translation
 * unit owns the window rather than either job. Splitting them into a second
 * pinned object was considered and rejected: it buys a tidier file name and
 * costs a new carve-out selector, a new ASSERT pair, and a cross-unit call in
 * the one place where a selector that quietly stops matching produces a veneer
 * into uninitialized SRAM with no fault handler behind it (carve-out rule 1).
 *
 * ── Job 1: the inbound core1 hardware halt ────────────────────────────────
 *
 * Core1 must not be executing while core0's crt0 rebuilds SRAM underneath it.
 * The destructive window is crt0_v6m.S:249-286: the .sram_image copy overwrites
 * core1's own code and data, the .bss clear zeroes g_era_split_communication_core,
 * and __init_ram_areas() zeroes .ram5, which holds core1's live stack and live
 * vector table. A core1 still running across that returns into zeroed memory and
 * faults to a zeroed vector, with unbounded writes to shared SRAM and peripherals
 * in between.
 *
 * This used to hang off the one reset cause that has a software hook -- shutdown_kb,
 * reachable only through shutdown_quantum(). That covers software-initiated resets
 * and nothing else: a debugger or SWD reset keeps power, re-runs boot2/crt0/
 * __late_init, and is indistinguishable from a warm reset. Enumerating causes is
 * also structurally fragile rather than merely incomplete -- boot-time bootmagic
 * already runs through no hook, and is safe today only because it happens to
 * precede the core1 launch.
 *
 * So the halt sits at the hazard instead of at the causes. This runs from
 * crt0_v6m.S:196 `bl __early_init`, the only ungated call before the copy loops,
 * and therefore on every reset that re-runs crt0 whatever caused it.
 *
 * ── Job 2: the double-tap bootloader arm ──────────────────────────────────
 *
 * Upstream arms the double-tap window in __late_init(), which crt0 calls after
 * the copy loops. On an XIP board that is a few milliseconds after reset. On
 * this image it is tens of milliseconds, because crt0 first copies the whole
 * .sram_image out of flash and clears .bss, on the reset ROSC. A tweezer double
 * tap is a sub-100 ms motion, so the second reset landed before the window had
 * opened and merely re-armed: device-measured at roughly one success in ten,
 * against reliable entry when the two shorts are deliberately spaced. Widening
 * RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT was tried on device and moved the
 * hit rate not at all, which is what located the loss at the window's start.
 *
 * So reset classification and tap counting run here, microseconds after reset.
 * A first eligible tap becomes armed only after the bounded stable-release
 * guard below; only the bootrom jump stays in __late_init(). The split is not
 * a preference:
 * reset_usb_boot() lives in .sram_image, and calling it from here would link
 * silently through a veneer into SRAM that has not been written yet. The
 * three-state word that carries the count across the boundary is specified in
 * platforms/bootloader.h.
 *
 * ── The two constraints on anything in this window, both load-bearing ─────
 *
 * SRAM is off limits, with exactly one exception. .sram_image has not been
 * copied, .bss has not been cleared, and .ram5 has not been zeroed, so no
 * ordinary global holds anything yet -- the outbound version of the halt wrote
 * g_era_split_communication_core.launched/.running, which is gone, and was
 * never doing work anyway since crt0 zeroes the struct a few hundred cycles
 * later. The exception is magic_location, and it is narrow by construction
 * rather than by discipline: the reason the rule exists ("not initialized yet")
 * is the reason that word exists, and the ERA linker script claims it into
 * .era_bootloader_magic immediately after .bss and asserts it is one word
 * starting at or past __bss_end__, so no copy or clear loop reaches it. The
 * clause and the size of the exception set are canonical in
 * era_invariants.md; a second such object reopens that clause.
 *
 * The wait must be bounded. Interrupts are masked (crt0_v6m.S:167 `cpsid i`), no
 * watchdog countdown is started anywhere in this image, and VTOR already points
 * at the not-yet-copied SRAM vector table, so a fault here has no handler. An
 * unbounded spin at this point has no recovery but removing power.
 *
 * The whole translation unit is pinned to a flash VMA by the .flash_startup
 * carve-out in keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld. Read the three
 * carve-out rules written at that selector list before touching either side; the
 * ASSERT there proves both that the selector still matches this object and that
 * early_hardware_init_pre resolved here rather than to QMK's weak stub.
 */

#include <stdbool.h>
#include <stdint.h>

#include "hardware/structs/psm.h"

#if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET) && defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM)
#    include "bootloader.h"
#    include "hardware/regs/rosc.h"
#    include "hardware/regs/vreg_and_chip_reset.h"
#    include "hardware/structs/rosc.h"
#    include "hardware/structs/vreg_and_chip_reset.h"
#    include "hardware/structs/watchdog.h"
#    define ERA_BOOT_DOUBLE_TAP_PRE_COPY_ARM 1

#    define ERA_BOOT_GUARD_ROSC_CHUNK_TICKS 240U
#    define ERA_BOOT_GUARD_ROSC_CHUNK_COUNT 768U
#    define ERA_BOOT_GUARD_ROSC_POLL_LIMIT 256U
#    define ERA_BOOT_GUARD_SCRATCH_MARKER 0x45524153UL
#    define ERA_BOOT_GUARD_RESET_SOURCE_MASK (VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS | VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS | VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS)

/*
 * 184,320 ticks are about 15.4--102.4 ms over RP2040's specified 12--1.8 MHz
 * startup ROSC range. This is a reset-chatter guard, not a calibrated clock:
 * the existing post-init timeout still owns the human double-tap upper edge.
 */
_Static_assert(ERA_BOOT_GUARD_ROSC_CHUNK_TICKS <= ROSC_COUNT_BITS, "The boot guard ROSC chunk must fit the 8-bit COUNT register.");
_Static_assert(ERA_BOOT_GUARD_ROSC_CHUNK_TICKS * ERA_BOOT_GUARD_ROSC_CHUNK_COUNT == 184320U, "The boot guard fixed tick policy changed.");
#endif

#if !defined(MCU_RP)
#    error "The ERA pre-copy startup window is RP2040-only."
#endif

#if defined(ERA_BOOT_DOUBLE_TAP_PRE_COPY_ARM)
static __attribute__((always_inline)) inline bool era_boot_guard_wait_rosc_chunks(uint32_t chunk_count) {
    for (uint32_t chunk = 0U; chunk < chunk_count; chunk++) {
        rosc_hw->count = ERA_BOOT_GUARD_ROSC_CHUNK_TICKS;

        uint32_t polls = ERA_BOOT_GUARD_ROSC_POLL_LIMIT;
        while (rosc_hw->count != 0U && polls > 0U) {
            polls--;
        }
        if (rosc_hw->count != 0U) {
            rosc_hw->count = 0U;
            return false;
        }
    }
    return true;
}

static __attribute__((always_inline)) inline bool era_boot_guard_wait_for_stable_release(void) {
    return era_boot_guard_wait_rosc_chunks(ERA_BOOT_GUARD_ROSC_CHUNK_COUNT);
}
#endif

/* Iterations rather than microseconds: this runs before clocks_init() in
 * __late_init(), so clk_sys is still the reset ROSC and no timer-based
 * conversion would be trustworthy.
 *
 * What is being waited on is a fraction of a clock cycle of APB delay; the
 * pico-SDK spins on this same readback with no bound and documents it as such.
 * Taking the ROSC floor of 1.8 MHz and counting an APB read as far dearer than
 * it is, 1000 iterations is still about a millisecond against a sub-cycle
 * event -- roughly three orders of magnitude of margin, and a worst case too
 * small to see next to the double-tap window __late_init opens a few hundred
 * cycles later.
 *
 * Falling through the bound is a safe outcome, not a failure path: FRCE_OFF is
 * cleared either way, leaving the same state the old cause-side guard left on
 * every reset it did not cover, and a chip whose PSM does not answer this does
 * not boot at all. */
#ifndef ERA_BOOT_CORE1_HALT_ACK_SPINS
#    define ERA_BOOT_CORE1_HALT_ACK_SPINS 1000U
#endif

/* QMK declares this hook in no header: chibios.c carries the weak default and
 * the linker matches an override by name alone. Declared here so this definition
 * is at least checked against a prototype. */
void early_hardware_init_pre(void);

void early_hardware_init_pre(void) {
#if defined(ERA_BOOT_DOUBLE_TAP_PRE_COPY_ARM)
    /* Classify this reset and preserve only an eligible physical tap, first
     * thing in the window.
     *
     * Ordered ahead of the halt for two reasons. It is the latency-sensitive
     * half - every cycle before it is dead zone, and the halt's acknowledge
     * spin below is the longest thing in this function. And it is the recovery
     * path: if the halt ever failed to return, a board whose window was already
     * armed can still be reached by a second tap, which is not true the other
     * way round.
     *
     * The one retained read is kept in a local on MSP in ram4, set at
     * crt0_v6m.S:171 and touched by no copy or clear loop. The reset-domain
     * tuple makes POR, recovery and software resets fail clear before any cold
     * SRAM pattern can be accepted. A first exact RUN then keeps the production
     * word clear through the bounded ROSC guard; only completion writes ARMED. */
    const uint32_t tap_state = magic_location;
    const uint32_t pre_copy_wdsel    = psm_hw->wdsel;
    const uint32_t chip_reset        = vreg_and_chip_reset_hw->chip_reset;
    const uint32_t watchdog_reason   = watchdog_hw->reason;
    const uint32_t watchdog_scratch0 = watchdog_hw->scratch[0];
    const uint32_t chip_reset_source = chip_reset & ERA_BOOT_GUARD_RESET_SOURCE_MASK;
    const bool physical_run_boot = watchdog_scratch0 == 0U &&
                                   watchdog_reason == 0U &&
                                   pre_copy_wdsel == 0U &&
                                   chip_reset_source == VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS;
    /*
     * Scratch 0--3 are not part of the bootrom watchdog-boot tuple (4--7).
     * RUN/DVDD resets clear them, while core-only and watchdog soft resets
     * retain them. Classify the inbound value, then mark this boot before any
     * later software route can reset the processor.
     */
    watchdog_hw->scratch[0] = ERA_BOOT_GUARD_SCRATCH_MARKER;

    uint32_t next_tap_state = 0U;

    if (physical_run_boot) {
        if (tap_state == RP2040_BOOTLOADER_DOUBLE_TAP_ARMED_TOKEN ||
            tap_state == RP2040_BOOTLOADER_DOUBLE_TAP_REQUEST_TOKEN) {
            next_tap_state = RP2040_BOOTLOADER_DOUBLE_TAP_REQUEST_TOKEN;
        } else {
            /*
             * A RUN edge inside the guard restarts here with CLEAR. Only a
             * release that survives every bounded ROSC chunk becomes the first
             * tap; a counter that fails to drain also leaves CLEAR.
             */
            magic_location = 0U;
            if (era_boot_guard_wait_for_stable_release()) {
                next_tap_state = RP2040_BOOTLOADER_DOUBLE_TAP_ARMED_TOKEN;
            }
        }
    }

    magic_location = next_tap_state;
#endif

    /* multicore_reset_core1()'s sequence minus the FIFO drain, which the ERA
     * launch handshake performs itself. Idempotent, so the cold-boot case costs
     * nothing: core1 sits in the bootrom wait-for-vector loop and set-then-clear
     * returns it there, which is exactly the state the launch handshake requires.
     * FRCE_OFF must be clear on return -- the launch path touches PSM nowhere. */
    hw_set_bits(&psm_hw->frce_off, PSM_FRCE_OFF_PROC1_BITS);
    for (uint32_t spins = ERA_BOOT_CORE1_HALT_ACK_SPINS; spins > 0U; spins--) {
        if ((psm_hw->frce_off & PSM_FRCE_OFF_PROC1_BITS) != 0U) {
            break;
        }
    }
    hw_clear_bits(&psm_hw->frce_off, PSM_FRCE_OFF_PROC1_BITS);
}
