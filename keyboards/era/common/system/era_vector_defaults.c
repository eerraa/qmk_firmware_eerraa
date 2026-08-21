// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* Strong SRAM definitions for every vector slot ChibiOS leaves at its weak
 * default.
 *
 * The SRAM-resident image claims that neither core fetches flash at runtime.
 * That claim was very nearly true and not quite: 35 of the vector table's 48
 * entries pointed into flash. The cause is the .flash_startup carve-out itself,
 * not XIP inheritance. vectors.o(.text) is pinned to a flash VMA because
 * Reset_Handler has to live there -- boot2 loads MSP/PC from 0x10000100 -- and
 * ChibiOS's weak default handlers ride along in the same translation unit. So
 * every slot nobody overrode resolved into flash, where an erase leaves the SSI
 * out of XIP mode and an instruction fetch returns whatever the bus does then.
 *
 * Overriding _unhandled_exception alone does NOT fix this, and the shape of
 * vectors.S is why. Its default handlers are not .thumb_set aliases: dozens of
 * VectorNNN: labels sit consecutively with no instruction between them, so they
 * all collapse onto one address, and that address falls through into a single
 * `bl _unhandled_exception`. A strong _unhandled_exception in SRAM moves the
 * branch target and leaves all 35 table entries pointing at the flash `bl`.
 * The table only moves if the individual vector symbols are overridden, which
 * is what this file does. Every one of them is .weak in vectors.S, so this
 * needs no ChibiOS or QMK edit and no change to the carve-out selector list.
 *
 * The enumeration below is the set that was resolving to the weak default, and
 * it is derived rather than guessed: `nm` on the linked ELF lists exactly these
 * 35 names at the collapsed address, the vectors.S table order fixes which slot
 * each occupies, and `objdump -s -j .vectors` confirms the same 35 words. The
 * list is therefore build-configuration dependent, and it is wrong in only two
 * directions, both of which fail loudly rather than silently:
 *
 *   - A driver that starts installing one of these collides with the strong
 *     definition here and the link fails with a duplicate symbol. Answer it
 *     with the driver's own condition, the way Vector50 and the two PIO slots
 *     below are written -- never by weakening these to .weak, and never by
 *     deleting the line, which drops the slot back into flash on every board
 *     that does not have that driver.
 *   - A driver that stops installing a handler drops its slot back to the flash
 *     default, and nothing here would notice. That is the direction the
 *     .vectors gate in tools/era_tomak79h_build.sh exists to catch: it reads
 *     the linked table's own bytes rather than any name list, so it cannot
 *     inherit a mistake from this enumeration.
 *
 * What this deliberately does not do is recover. Landing here is still a hang,
 * exactly as the ChibiOS default was. The whole change is that the hang now has
 * an address that can be read back and attributed, instead of a de-XIP'd flash
 * address that means nothing.
 */

#include <stdbool.h>

#include "cmparams.h"
/* For HAL_USE_PWM and the RP_PWM_USE_PWMn set, which decide whether the
 * ChibiOS PWM LLD claims Vector50 in this build. halconf.h alone is not
 * enough: the RP_PWM_USE_PWMn defaults live in the LLD header that hal.h
 * pulls in behind HAL_USE_PWM. */
#include "hal.h"

#if !defined(ERA_SRAM_RESIDENT_IMAGE)
#    error "The ERA vector defaults belong to the SRAM-resident load image; an XIP build's flash-resident handlers are legitimate."
#endif

#if !defined(MCU_RP)
#    error "The ERA vector defaults are RP2040-only."
#endif

/* The names below are positional: VectorNNN encodes a byte offset into a table
 * whose length is CORTEX_NUM_VECTORS. At 32 the last slot is VectorBC, and the
 * enumeration is complete only for that length. */
#if CORTEX_NUM_VECTORS != 32
#    error "The ERA vector default enumeration is derived from CORTEX_NUM_VECTORS == 32."
#endif

void era_unhandled_vector(void);

/* The empty statement keeps the loop from being optimized into something
 * without a stable PC to read back. */
void era_unhandled_vector(void) {
    while (true) {
        __asm__ volatile("" ::: "memory");
    }
}

#define ERA_VECTOR_DEFAULT(vector_symbol) void vector_symbol(void) __attribute__((alias("era_unhandled_vector")))

/* System slots. On ARMv6-M most of these are reserved rather than reachable --
 * the core raises HardFault instead -- but they occupy table entries either
 * way, and an entry is what this is about. HardFault and NMI are the ones that
 * can actually fire, and neither is maskable by PRIMASK, so before this change
 * a fault during the interrupts-off erase already vectored into de-XIP'd flash.
 * NMI_Handler is absent from this list because the ChibiOS RP2 port installs
 * it; SVC, PendSV, and SysTick are present because this image installs none of
 * them -- ChibiOS drives its tick from TIMER_IRQ_3, not SysTick. */
ERA_VECTOR_DEFAULT(HardFault_Handler);   /* slot 3  */
ERA_VECTOR_DEFAULT(MemManage_Handler);   /* slot 4  */
ERA_VECTOR_DEFAULT(BusFault_Handler);    /* slot 5  */
ERA_VECTOR_DEFAULT(UsageFault_Handler);  /* slot 6  */
ERA_VECTOR_DEFAULT(SecureFault_Handler); /* slot 7  */
ERA_VECTOR_DEFAULT(Vector20);            /* slot 8, reserved  */
ERA_VECTOR_DEFAULT(Vector24);            /* slot 9, reserved  */
ERA_VECTOR_DEFAULT(Vector28);            /* slot 10, reserved */
ERA_VECTOR_DEFAULT(SVC_Handler);         /* slot 11 */
ERA_VECTOR_DEFAULT(DebugMon_Handler);    /* slot 12 */
ERA_VECTOR_DEFAULT(Vector34);            /* slot 13, reserved */
ERA_VECTOR_DEFAULT(PendSV_Handler);      /* slot 14 */
ERA_VECTOR_DEFAULT(SysTick_Handler);     /* slot 15 */

/* NVIC slots, named by table byte offset: VectorNNN is IRQ (NNN - 0x40) / 4.
 * The RP2040 IRQ each one carries is named so the installed/absent split can be
 * checked against the datasheet rather than against this file. The unconditional
 * omissions are the handlers every ERA image really installs: TIMER_IRQ_0..3
 * (the ChibiOS tick), USBCTRL, and SIO_IRQ_PROC0/1. The PWM slot, the two PIO
 * slots and the two DMA slots are conditional and are explained at their
 * entries below.
 *
 * Read the word "every" in that sentence as the standing hazard of this file.
 * Each of the three conditional groups was once an unconditional statement
 * about what an ERA image installs, each was true of every ERA board at the
 * time it was written, and each became false the first time a board with a
 * different driver set linked this image. */
/* Vector50 is one fact about which driver owns the slot, exactly like the two
 * PIO entries below, and it is written as the LLD's own condition rather than
 * as a board list. lib/chibios-contrib/os/hal/ports/RP/LLD/PWMv1/hal_pwm_lld.c
 * compiles its OSAL_IRQ_HANDLER(RP_PWM_IRQ_WRAP_HANDLER) when HAL_USE_PWM is
 * TRUE and at least one RP_PWM_USE_PWMn is TRUE, so that is what is mirrored
 * here; the two together is what the strong definition below must not collide
 * with. Every ERA board with QMK `backlight` on hardware PWM is in that case --
 * divine, era65, linx3/n8x, sirind/klein_hs and sirind/klein_sd -- and this
 * file claimed the slot unconditionally until 2026-08-11, which is what made
 * the copy-to-RAM bundle a duplicate-symbol link failure on all five. */
#if (HAL_USE_PWM == TRUE) &&                                                   \
    ((RP_PWM_USE_PWM0 == TRUE) || (RP_PWM_USE_PWM1 == TRUE) ||                 \
     (RP_PWM_USE_PWM2 == TRUE) || (RP_PWM_USE_PWM3 == TRUE) ||                 \
     (RP_PWM_USE_PWM4 == TRUE) || (RP_PWM_USE_PWM5 == TRUE) ||                 \
     (RP_PWM_USE_PWM6 == TRUE) || (RP_PWM_USE_PWM7 == TRUE))
/* The PWM LLD installs it. */
#else
ERA_VECTOR_DEFAULT(Vector50); /* IRQ 4  PWM_IRQ_WRAP  */
#endif
ERA_VECTOR_DEFAULT(Vector58); /* IRQ 6  XIP_IRQ       */

/* The two PIO slots are one fact rather than two entries. The ERA wire backend
 * claims exactly one of them -- PIO0_IRQ_0 normally, PIO1_IRQ_0 under
 * SERIAL_PIO_USE_PIO1 -- so this file fills whichever it left, and fills both
 * when there is no backend to claim either.
 *
 * The ERA backend's own PIO1 arms were deleted on 2026-08-11 as unreachable:
 * the selector belongs to the stock split serial driver and no ERA board sets
 * it. That deletion could have made the two files disagree in silence, so it
 * landed with an #error on the selector in era_split_transaction_backend_rp2040.c.
 * The condition below stays as it is, because this file compiles for boards
 * with no ERA backend at all, where the selector is still a real question.
 *
 * Both entries were wrong before, in mirror-image ways, and both were invisible
 * while every ERA board was a split board on PIO0. Vector5C was omitted
 * unconditionally, which read as "the backend installs it" when the truth was
 * "a backend installs it" -- the first non-split board to adopt the residency
 * bundle left the slot at ChibiOS's flash-resident weak default and failed the
 * .vectors gate. Vector64 was present unconditionally, which would have become
 * a duplicate symbol the moment any board set SERIAL_PIO_USE_PIO1.
 *
 * Getting the condition wrong in either direction fails the link or the gate
 * and never the boot, which is why it is written as a condition rather than
 * derived per board. */
#if !defined(SPLIT_KEYBOARD) || defined(SERIAL_PIO_USE_PIO1)
ERA_VECTOR_DEFAULT(Vector5C); /* IRQ 7  PIO0_IRQ_0    */
#endif
ERA_VECTOR_DEFAULT(Vector60); /* IRQ 8  PIO0_IRQ_1    */
#if !defined(SPLIT_KEYBOARD) || !defined(SERIAL_PIO_USE_PIO1)
ERA_VECTOR_DEFAULT(Vector64); /* IRQ 9  PIO1_IRQ_0    */
#endif
ERA_VECTOR_DEFAULT(Vector68); /* IRQ 10 PIO1_IRQ_1    */

/* The two DMA slots are the third conditional pair, and they arrived the same
 * way the PIO pair did: omitted unconditionally on the reading that "every ERA
 * image installs DMA_IRQ_0/1 for RGB", which was true of every ERA board while
 * every ERA board had RGB. lib/chibios/os/hal/ports/RP/LLD/DMAv1/rp_dma.c
 * compiles both handlers under RP_DMA_REQUIRED, which platforms/chibios/
 * vendors/RP/RP2040.mk defines for WS2812_DRIVER=vendor, hal_spi_lld.h defines
 * for a board using SPI, and era_common_qmk_rules.mk defines beside the PIO
 * matrix sampler's SRC line -- so the marker, reachable here through hal.h, is
 * the condition rather than any statement about RGB, about SPI, or about the
 * matrix engine.
 *
 * That last emitter is why the branch below currently reaches no ERA board.
 * divine, era65, linx3/n8x and sirind/klein_hs were the case until 2026-08-16:
 * QMK `backlight` on hardware PWM, no addressable LED anywhere, no DMA driver,
 * and both slots therefore resolving into the flash carve-out -- the .vectors
 * gate caught all four on their first copy-to-RAM link, which is what that gate
 * is for. The PIO sampler then took its two channels from the same ChibiOS
 * allocator the ws2812 driver uses, so those four link rp_dma.c and install
 * both handlers now. Do not answer that by deleting the entries: the condition
 * is a fact about which driver owns the slot, and an ERA board that turns the
 * matrix engine off with no addressable LED and no SPI returns to exactly the
 * case above, one build option away and with nothing but this branch between it
 * and a flash-resident vector. */
#if !defined(RP_DMA_REQUIRED)
ERA_VECTOR_DEFAULT(Vector6C); /* IRQ 11 DMA_IRQ_0     */
ERA_VECTOR_DEFAULT(Vector70); /* IRQ 12 DMA_IRQ_1     */
#endif

ERA_VECTOR_DEFAULT(Vector74); /* IRQ 13 IO_IRQ_BANK0  */
ERA_VECTOR_DEFAULT(Vector78); /* IRQ 14 IO_IRQ_QSPI   */
ERA_VECTOR_DEFAULT(Vector84); /* IRQ 17 CLOCKS_IRQ    */
ERA_VECTOR_DEFAULT(Vector88); /* IRQ 18 SPI0_IRQ      */
ERA_VECTOR_DEFAULT(Vector8C); /* IRQ 19 SPI1_IRQ      */
ERA_VECTOR_DEFAULT(Vector90); /* IRQ 20 UART0_IRQ     */
ERA_VECTOR_DEFAULT(Vector94); /* IRQ 21 UART1_IRQ     */
ERA_VECTOR_DEFAULT(Vector98); /* IRQ 22 ADC_IRQ_FIFO  */
ERA_VECTOR_DEFAULT(Vector9C); /* IRQ 23 I2C0_IRQ      */
ERA_VECTOR_DEFAULT(VectorA0); /* IRQ 24 I2C1_IRQ      */
ERA_VECTOR_DEFAULT(VectorA4); /* IRQ 25 RTC_IRQ       */
ERA_VECTOR_DEFAULT(VectorA8); /* IRQ 26, no source    */
ERA_VECTOR_DEFAULT(VectorAC); /* IRQ 27, no source    */
ERA_VECTOR_DEFAULT(VectorB0); /* IRQ 28, no source    */
ERA_VECTOR_DEFAULT(VectorB4); /* IRQ 29, no source    */
ERA_VECTOR_DEFAULT(VectorB8); /* IRQ 30, no source    */
ERA_VECTOR_DEFAULT(VectorBC); /* IRQ 31, no source    */
