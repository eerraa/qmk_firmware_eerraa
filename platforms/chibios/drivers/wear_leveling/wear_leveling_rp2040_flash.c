/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 * Copyright (c) 2022 Nick Brassel (@tzarc)
 * Copyright (c) 2022 Stefan Kerkmann (@KarlK90)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdbool.h>

#include "pico/bootrom.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/structs/ssi.h"
#include "hardware/structs/ioqspi.h"

#include "compiler_support.h"
#include "timer.h"
#include "wear_leveling.h"
#include "wear_leveling_rp2040_flash_config.h"
#include "wear_leveling_internal.h"

#ifndef WEAR_LEVELING_RP2040_FLASH_BULK_COUNT
#    define WEAR_LEVELING_RP2040_FLASH_BULK_COUNT 64
#endif // WEAR_LEVELING_RP2040_FLASH_BULK_COUNT

#define FLASHCMD_PAGE_PROGRAM 0x02
#define FLASHCMD_READ_STATUS 0x05
#define FLASHCMD_WRITE_ENABLE 0x06

extern const uint8_t BOOT2_ROM[256];
static uint32_t      BOOT2_ROM_RAM[64];

static ssi_hw_t *const ssi = (ssi_hw_t *)XIP_SSI_BASE;

// Sanity check
check_hw_layout(ssi_hw_t, ssienr, SSI_SSIENR_OFFSET);
check_hw_layout(ssi_hw_t, spi_ctrlr0, SSI_SPI_CTRLR0_OFFSET);

static void __no_inline_not_in_flash_func(flash_enable_xip_via_boot2)(void) {
    ((void (*)(void))BOOT2_ROM_RAM + 1)();
}

// Bitbanging the chip select using IO overrides, in case RAM-resident IRQs
// are still running, and the FIFO bottoms out. (the bootrom does the same)
static void __no_inline_not_in_flash_func(flash_cs_force)(bool high) {
    uint32_t field_val = high ? IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_VALUE_HIGH : IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_VALUE_LOW;
    hw_write_masked(&ioqspi_hw->io[1].ctrl, field_val << IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_LSB, IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_BITS);
}

// Also allow any unbounded loops to check whether the above abort condition
// was asserted, and terminate early
static int __no_inline_not_in_flash_func(flash_was_aborted)(void) {
    return *(io_rw_32 *)(IO_QSPI_BASE + IO_QSPI_GPIO_QSPI_SD1_CTRL_OFFSET) & IO_QSPI_GPIO_QSPI_SD1_CTRL_INOVER_BITS;
}

// Put bytes from one buffer, and get bytes into another buffer.
// These can be the same buffer.
// If tx is NULL then send zeroes.
// If rx is NULL then all read data will be dropped.
//
// If rx_skip is nonzero, this many bytes will first be consumed from the FIFO,
// before reading a further count bytes into *rx.
// E.g. if you have written a command+address just before calling this function.
static void __no_inline_not_in_flash_func(flash_put_get)(const uint8_t *tx, uint8_t *rx, size_t count, size_t rx_skip) {
    // Make sure there is never more data in flight than the depth of the RX
    // FIFO. Otherwise, when we are interrupted for long periods, hardware
    // will overflow the RX FIFO.
    const uint max_in_flight = 16 - 2; // account for data internal to SSI
    size_t     tx_count      = count;
    size_t     rx_count      = count;
    while (tx_count || rx_skip || rx_count) {
        // NB order of reads, for pessimism rather than optimism
        uint32_t tx_level      = ssi_hw->txflr;
        uint32_t rx_level      = ssi_hw->rxflr;
        bool     did_something = false; // Expect this to be folded into control flow, not register
        if (tx_count && tx_level + rx_level < max_in_flight) {
            ssi->dr0 = (uint32_t)(tx ? *tx++ : 0);
            --tx_count;
            did_something = true;
        }
        if (rx_level) {
            uint8_t rxbyte = ssi->dr0;
            did_something  = true;
            if (rx_skip) {
                --rx_skip;
            } else {
                if (rx) *rx++ = rxbyte;
                --rx_count;
            }
        }
        // APB load costs 4 cycles, so only do it on idle loops (our budget is
        // 48 cyc/byte)
        if (!did_something && __builtin_expect(flash_was_aborted(), 0)) break;
    }
    flash_cs_force(1);
}

// Convenience wrapper for above
// (And it's hard for the debug host to get the tight timing between
// cmd DR0 write and the remaining data)
static void __no_inline_not_in_flash_func(_flash_do_cmd)(uint8_t cmd, const uint8_t *tx, uint8_t *rx, size_t count) {
    flash_cs_force(0);
    ssi->dr0 = cmd;
    flash_put_get(tx, rx, count, 1);
}

// Timing of this one is critical, so do not expose the symbol to debugger etc
static void __no_inline_not_in_flash_func(flash_put_cmd_addr)(uint8_t cmd, uint32_t addr) {
    flash_cs_force(0);
    addr |= cmd << 24;
    for (int i = 0; i < 4; ++i) {
        ssi->dr0 = addr >> 24;
        addr <<= 8;
    }
}

// Poll the flash status register until the busy bit (LSB) clears
static void __no_inline_not_in_flash_func(flash_wait_ready)(void) {
    uint8_t stat;
    do {
        _flash_do_cmd(FLASHCMD_READ_STATUS, NULL, &stat, 1);
    } while (stat & 0x1 && !flash_was_aborted());
}

// Set the WEL bit (needed before any program/erase operation)
static void __no_inline_not_in_flash_func(flash_enable_write)(void) {
    _flash_do_cmd(FLASHCMD_WRITE_ENABLE, NULL, NULL, 0);
}

static void __no_inline_not_in_flash_func(pico_program_bulk)(uint32_t flash_address, backing_store_int_t *values, size_t item_count) {
    rom_connect_internal_flash_fn connect_internal_flash = (rom_connect_internal_flash_fn)rom_func_lookup_inline(ROM_FUNC_CONNECT_INTERNAL_FLASH);
    rom_flash_exit_xip_fn         flash_exit_xip         = (rom_flash_exit_xip_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_EXIT_XIP);
    rom_flash_flush_cache_fn      flash_flush_cache      = (rom_flash_flush_cache_fn)rom_func_lookup_inline(ROM_FUNC_FLASH_FLUSH_CACHE);
    assert(connect_internal_flash && flash_exit_xip && flash_flush_cache);

    static backing_store_int_t bulk_write_buffer[WEAR_LEVELING_RP2040_FLASH_BULK_COUNT];

    while (item_count) {
        size_t batch_size = MIN(item_count, WEAR_LEVELING_RP2040_FLASH_BULK_COUNT);
        for (size_t i = 0; i < batch_size; i++, values++, item_count--) {
            bulk_write_buffer[i] = ~(*values);
        }
        __compiler_memory_barrier();

        connect_internal_flash();
        flash_exit_xip();
        flash_enable_write();

        flash_put_cmd_addr(FLASHCMD_PAGE_PROGRAM, flash_address);
        flash_put_get((uint8_t *)bulk_write_buffer, NULL, batch_size * sizeof(backing_store_int_t), 4);
        flash_wait_ready();
        flash_address += batch_size * sizeof(backing_store_int_t);

        flash_flush_cache();
        flash_enable_xip_via_boot2();
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// QMK Wear-Leveling Backing Store implementation

// The interrupt mask around a program/erase exists for exactly one reason: the
// SSI leaves memory-mapped XIP mode to issue the command sequence, so no code
// fetched from flash may run for the duration. On an ordinary XIP build the
// ISRs are in flash, and the mask is what makes the window survivable.
//
// ERA's split boards run the whole image from SRAM. .text, .rodata, .data, both
// cores' stacks, and - as of the vector-table change this gate arrived with -
// every vector slot but the reset one hold SRAM addresses, so there is nothing
// left for an interrupt to fetch from flash. BOOT2_ROM is already copied to RAM
// in backing_store_init() for precisely this situation. The whole-store erase
// masked core0 for ~381 ms, which since the boot-master poll was deleted lands
// inside the USB enumeration window on an EEPROM CLEAN boot.
//
// This is a gate and not a deletion. Every other keyboard in this fork is an
// XIP build where the mask is still load-bearing, and so is every ERA board
// that has not included keyboards/era/common/system/era_sram_resident_rules.mk
// -- which today is every non-split one. That set is expected to shrink to
// none, and the gate is what makes shrinking it safe: a board arrives here
// masked and stops being masked by the same include that stops it being XIP,
// with no third place to keep in step.
//
// The residency claim is checked rather than argued. era_vector_defaults.c
// supplies the strong SRAM handlers, and the .vectors gate in
// keyboards/era/common/tools/era_tomak79h_build.sh reads the linked table's
// bytes on every profile, so a slot that falls back to the flash-resident
// ChibiOS default fails the build instead of quietly reintroducing the hazard
// this mask was covering.
//
// Both windows are gated, not just the erase. Slice 8.3 already stopped halting
// core1 across ordinary commits, so core1 runs through both of these today at a
// device-measured hbmiss=0/0 - unmasking core0 adds SRAM-resident ISRs to a
// window a second core already executes through. The write path is also the one
// the driver was written to tolerate: the max_in_flight limit in
// flash_put_get() exists so the RX FIFO cannot overflow "when we are
// interrupted for long periods".
#if defined(ERA_SRAM_RESIDENT_IMAGE)
#    define ERA_FLASH_COMMIT_MASK_ENTER() ((void)0)
#    define ERA_FLASH_COMMIT_MASK_EXIT() ((void)0)
#else
static int interrupts;
#    define ERA_FLASH_COMMIT_MASK_ENTER() (interrupts = save_and_disable_interrupts())
#    define ERA_FLASH_COMMIT_MASK_EXIT() restore_interrupts(interrupts)
#endif

bool backing_store_init(void) {
    bs_dprintf("Init\n");
    memcpy(BOOT2_ROM_RAM, BOOT2_ROM, sizeof(BOOT2_ROM));
    __compiler_memory_barrier();
    return true;
}

bool backing_store_unlock(void) {
    bs_dprintf("Unlock\n");
    return true;
}

bool backing_store_erase(void) {
#if defined(ERA_SRAM_RESIDENT_IMAGE)
    // Nothing the gap runs may reach the backing store, and this is where that
    // stops being a rule and starts being a check. The reachable half is held
    // by the wear-leveling interlock; this catches an entry point reached
    // without passing through it - which today is only `wear_leveling_init()`'s
    // recovery consolidation, unguarded upstream and not reachable from a gap,
    // but the guarantee is structural rather than an enumeration.
    if (backing_store_commit_blocked_kb()) {
        return false;
    }
#endif

#ifdef WEAR_LEVELING_DEBUG_OUTPUT
    uint32_t start = timer_read32();
#endif

    // Ensure the backing size can be cleanly subtracted from the flash size without alignment issues.
    STATIC_ASSERT((WEAR_LEVELING_BACKING_SIZE) % (FLASH_SECTOR_SIZE) == 0, "Backing size must be a multiple of FLASH_SECTOR_SIZE");

#if defined(ERA_SRAM_RESIDENT_IMAGE)
    // One call per sector, with control handed back between them.
    //
    // The bootrom already erases this range a sector at a time: the backing
    // store sits at the top of flash and is not 64 KB-aligned, so the block
    // command flash_range_erase() prefers never applies and all 48 KB go as
    // twelve 4 KB sector erases either way. The flash work is therefore
    // identical, and what the loop buys is twelve places core0 can be somewhere
    // else. The cost is eleven extra XIP exit/re-enter cycles, which is what
    // makes this safe rather than expensive - flash_range_erase() restores
    // memory-mapped XIP before it returns, so the gap runs ordinary code.
    //
    // Slicing without a gap that does something is not this fix. The whole
    // reason to pay for the loop is the yield, and whether the yield ran is a
    // separate reading from how long the window was; both are in
    // era_flash_slice.c, and the slice/scan pair is what distinguishes this
    // build from one that only looks like it.
    //
    // Residency is the gate because the mask above is: an XIP build holds
    // interrupts off across this window, and handing control to the keyboard
    // with interrupts masked is not handing it anywhere. Every board outside
    // that marker keeps the single masked call it has today, unchanged.
    // After every sector including the last, and the last one is not spare.
    // The caller that matters is wear_leveling_consolidate_force(), which
    // programs 24 KB of cache the instant this returns. Yielding only *between*
    // sectors welds the twelfth erase to that program into one unbroken span -
    // ~32 ms plus ~70 ms - and lands the remaining worst case back on the
    // 100 ms limit this slice exists to get under. With the final yield the
    // longest span is the undecomposed program alone, which is the ~80 ms the
    // plan's arithmetic for this rung always assumed and is what rung 2
    // decomposes next.
    for (uint32_t offset = 0; offset < (WEAR_LEVELING_BACKING_SIZE); offset += (FLASH_SECTOR_SIZE)) {
        flash_range_erase((WEAR_LEVELING_RP2040_FLASH_BASE) + offset, (FLASH_SECTOR_SIZE));
        wear_leveling_backing_erase_yield();
    }
#else
    ERA_FLASH_COMMIT_MASK_ENTER();
    flash_range_erase((WEAR_LEVELING_RP2040_FLASH_BASE), (WEAR_LEVELING_BACKING_SIZE));
    ERA_FLASH_COMMIT_MASK_EXIT();
#endif

    bs_dprintf("Backing store erase took %ldms to complete\n", ((long)(timer_read32() - start)));
    return true;
}

bool backing_store_write(uint32_t address, backing_store_int_t value) {
    return backing_store_write_bulk(address, &value, 1);
}

bool backing_store_write_bulk(uint32_t address, backing_store_int_t *values, size_t item_count) {
#if defined(ERA_SRAM_RESIDENT_IMAGE)
    // Same check, same reason. backing_store_write() routes through here, so
    // one site covers both write entry points.
    if (backing_store_commit_blocked_kb()) {
        return false;
    }
#endif

    uint32_t offset = (WEAR_LEVELING_RP2040_FLASH_BASE) + address;
    bs_dprintf("Write ");
    wl_dump(offset, values, sizeof(backing_store_int_t) * item_count);
    ERA_FLASH_COMMIT_MASK_ENTER();
    pico_program_bulk(offset, values, item_count);
    ERA_FLASH_COMMIT_MASK_EXIT();
    return true;
}

bool backing_store_lock(void) {
    return true;
}

bool backing_store_read(uint32_t address, backing_store_int_t *value) {
    return backing_store_read_bulk(address, value, 1);
}

bool backing_store_read_bulk(uint32_t address, backing_store_int_t *values, size_t item_count) {
    uint32_t             offset = (WEAR_LEVELING_RP2040_FLASH_BASE) + address;
    backing_store_int_t *loc    = (backing_store_int_t *)((XIP_BASE) + offset);
    for (size_t i = 0; i < item_count; ++i) {
        values[i] = ~loc[i];
    }
    bs_dprintf("Read  ");
    wl_dump(offset, values, item_count * sizeof(backing_store_int_t));
    return true;
}
