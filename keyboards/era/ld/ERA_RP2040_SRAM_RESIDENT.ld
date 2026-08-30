/*
 * The ERA SRAM-resident load image, for every ERA RP2040 board.
 *
 * Not one board's: `system/era_sram_resident_rules.mk` selects it as
 * MCU_LDSCRIPT for whichever board includes the copy-to-RAM bundle, which is
 * all twenty-two RP2040 boards under `keyboards/era`. `sirind/brick65` is
 * atmega32u4 and takes none of it. The header named TOMAK79H while that was
 * the only board that had adopted it.
 *
 * Whole-image copy-to-RAM layout: flash keeps only boot2, the vectors
 * LMA at the boot2-hardcoded 0x10000100, a flash-resident startup
 * carve-out (everything reachable before the crt0 copy loops finish),
 * and the .sram_image load image. The stock ChibiOS crt0 performs both
 * copies: CRT0_INIT_VECTORS moves the vector table to SRAM and retargets
 * VTOR; CRT0_INIT_DATA copies the single combined .sram_image section
 * through the __textdata_base__/__data_base__/__data_end__ triple.
 *
 * Contract: keyboards/era/common/docs/contracts/era_sram_residency_contract.md
 * Derived from ChibiOS RP2040_FLASH_TIMECRIT.ld and its included rules
 * (Apache-2.0, ChibiOS - Giovanni Di Sirio).
 */

MEMORY
{
    flash0 (rx) : org = 0x00000000, len = 16k   /* ROM                  */
    flash1 (rx) : org = 0x10000000, len = DEFINED(FLASH_LEN) ? FLASH_LEN : 2048k /* XIP */
    flash2 (rx) : org = 0x00000000, len = 0
    flash3 (rx) : org = 0x00000000, len = 0
    flash4 (rx) : org = 0x00000000, len = 0
    flash5 (rx) : org = 0x00000000, len = 0
    flash6 (rx) : org = 0x00000000, len = 0
    flash7 (rx) : org = 0x00000000, len = 0
    ram0   (wx) : org = 0x20000000, len = 256k  /* SRAM0 striped        */
    ram1   (wx) : org = 0x00000000, len = 256k  /* SRAM0 non striped    */
    ram2   (wx) : org = 0x00000000, len = 0
    ram3   (wx) : org = 0x00000000, len = 0
    ram4   (wx) : org = 0x20040000, len = 4k    /* SRAM4                */
    ram5   (wx) : org = 0x20041000, len = 4k    /* SRAM5                */
    ram6   (wx) : org = 0x00000000, len = 0
    ram7   (wx) : org = 0x20041f00, len = 256   /* SRAM5 boot           */
}

/* Session-1 reservation for the ERA-owned A/B NVM. This names the final
 * 128 KiB of the *effective linker-visible* flash range instead of assuming
 * the physical NOR capacity. The collision ASSERT below rejects an image that
 * grows into it without changing any existing section address. */
__era_nvm_region_end__ = ORIGIN(flash1) + LENGTH(flash1);
__era_nvm_region_base__ = __era_nvm_region_end__ - 128k;

/* Aliases consumed by the stock included rules files.*/
REGION_ALIAS("MAIN_STACK_RAM", ram4);
REGION_ALIAS("PROCESS_STACK_RAM", ram4);
REGION_ALIAS("C1_MAIN_STACK_RAM", ram5);
REGION_ALIAS("C1_PROCESS_STACK_RAM", ram5);
REGION_ALIAS("RAM_INIT_FLASH_LMA", flash1);
REGION_ALIAS("HEAP_RAM", ram0);

ENTRY(Reset_Handler)

SECTIONS
{
    .flash_begin : {
        __flash_binary_start = .;
    } > flash1

    /* boot2 stays VMA==LMA at 0x10000000: the bootrom executes it from
       flash and the wear-leveling driver reads the linked BOOT2_ROM
       symbol through XIP at runtime.*/
    .boot2 : {
        __boot2_start__ = .;
        KEEP (*(.boot2))
        __boot2_end__ = .;
    } > flash1

    /* Vector table: SRAM VMA (must stay the first ram0 allocation so the
       256-aligned table lands at 0x20000000), LMA immediately after the
       256-byte boot2, i.e. exactly the 0x10000100 address the prebuilt
       boot2 blob loads MSP/PC from at reset.*/
    .vectors : ALIGN(256)
    {
        __textvectors_base__ = LOADADDR(.vectors);
        __vectors_base__ = .;
        KEEP(*(.vectors))
        __vectors_end__ = .;
    } > ram0 AT > flash1

    /* Flash-resident startup carve-out: everything reachable from reset
       until the crt0 CRT0_INIT_DATA loop finishes must execute from
       flash VMAs. Reset_Handler (vectors.o) and _crt0_entry (crt0_v6m.o)
       stay adjacent for the Thumb short branch between them. The QMK
       strong __early_init lives in tmk_core chibios.o (init_hooks.h
       renames the board's), so its trio is pinned selectively; board.o
       is matched path-anchored because a bare *board.o suffix pattern
       also catches every class skeleton and every _keyboard.c object in
       the build. In this split build that is FIVE - quantum/keyboard.o,
       src/default_keyboard.o, era_split_keyboard.o, era_split_board.o,
       and the intended board.o; a non-split build substitutes
       era_nonsplit_board.o for the skeleton and has its own five.

       Three rules bind anything added here, none of them enforced by ld:

       1. A carve-out function calling into .sram_image does NOT fail to
          link and does NOT fault. ld silently inserts a long-branch
          veneer that bx-es to the .sram_image VMA, which before the copy
          loop holds uninitialized SRAM - garbage execution, and with
          CRT0_VTOR_INIT=1 the resulting fault has no handler because
          VTOR already points at the not-yet-copied SRAM table. Two such
          veneers legitimately exist (__main_veneer, ____late_init_veneer)
          for the post-copy calls at crt0_v6m.S:289 and :306; a third one
          for a pre-copy caller is the bug signature.
       2. A selector that pins only .text* leaves that TU's .rodata in
          .sram_image, so a pre-copy constant read returns garbage with no
          call involved. crt1.o and board.o carry .rodata arms for this
          reason; the three chibios.o selectors do not, so those three
          functions must stay rodata-free by inspection. Any new selector
          needs the .rodata arm.
       3. Order matters. Reset_Handler reaches _crt0_entry with a Thumb T2
          b (+-2 KB, currently -264 bytes used). Growth after the
          vectors.o line does not erode that margin; a new selector
          inserted between the crt0_v6m.o and vectors.o lines does. Add
          new entries at the end.*/
    .flash_startup :
    {
        __flash_startup_base__ = .;
        *crt0_v6m.o(.text .text.*)
        *vectors.o(.text .text.*)
        *crt1.o(.text .text.* .rodata .rodata.*)
        */RP_PICO_RP2040/board.o(.text .text.* .rodata .rodata.*)
        *chibios.o(.text.__early_init)
        *chibios.o(.text.early_hardware_init_pre)
        *chibios.o(.text.early_hardware_init_post)

        /* Appended last, per rule 3, and carrying the .rodata arm per rule 2.
           The bracketing symbols are what the ASSERT at the end of this file
           uses: proving early_hardware_init_pre lands between them proves both
           that this selector still matches and that the ERA definition won over
           QMK's weak stub in chibios.o. If the object stops being linked at all,
           the two collapse and the range test fails. */
        __era_boot_core1_halt_base__ = .;
        */era/common/system/era_boot_core1_halt.o(.text .text.* .rodata .rodata.*)
        __era_boot_core1_halt_end__ = .;
        __flash_startup_end__ = .;
    } > flash1

    /* The whole remaining image: one output section so the stock crt0
       word-copy loop over [__data_base__, __data_end__) initializes
       code, rodata, and data together. VMA/LMA congruence inside a
       single output section is guaranteed by ld.*/
    .sram_image : ALIGN(4)
    {
        PROVIDE(_textdata = LOADADDR(.sram_image));
        PROVIDE(_data = .);
        __textdata_base__ = LOADADDR(.sram_image);
        __data_base__ = .;

        __init_array_base__ = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end__ = .;
        __fini_array_base__ = .;
        KEEP(*(.fini_array))
        KEEP(*(SORT(.fini_array.*)))
        __fini_array_end__ = .;

        __text_base__ = .;
        *(.text)
        *(.text.*)
        *(.glue_7t)
        *(.glue_7)
        *(.gcc*)
        __text_end__ = .;

        . = ALIGN(4);
        __rodata_base__ = .;
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
        __rodata_end__ = .;

        *(.ARM.extab* .gnu.linkonce.armextab.*)
        __exidx_base__ = .;
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end__ = .;
        __exidx_end = .;
        *(.eh_frame_hdr)
        *(.eh_frame)

        . = ALIGN(4);
        *(vtable)
        *(.time_critical*)
        . = ALIGN(4);
        *(.data)
        *(.data.*)
        *(.ramtext)
        . = ALIGN(4);
        PROVIDE(_edata = .);
        __data_end__ = .;
    } > ram0 AT > flash1

    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_base__ = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
        PROVIDE(end = .);
    } > ram0

    /* The double-tap bootloader magic, claimed by name.
       `magic_location` (platforms/chibios/bootloaders/rp2040.c) is tagged
       `.ram0.bootloader_magic` and is global so the pinned pre-copy writer can
       reach it. Until this section existed no script named that input: the
       `*(.ram0.*)` sweep in rules_memory.ld collected it into the ram0 noinit
       region, which happens to follow .bss. Same address, but by coincidence
       rather than by instruction. Claiming it here reproduces that address as
       a property of this file.

       Placement is doubly constrained. It must be immediately after .bss,
       because surviving crt0's clear loop is the whole point and the loop
       stops at __bss_end__. And it must precede `INCLUDE rules_memory.ld`,
       because ld assigns an input section to the first output section that
       matches it, so the sweep only loses the word if this claim is read
       first. The ram0 noinit region is left empty as a result, and
       __ram0_clear__/__ram0_init__/__ram0_noinit__ each shift up by this one
       word - three link-time constants in crt1.c's ram_areas table, and the
       whole cost of this section. The region they describe was and stays zero
       length, so __init_ram_areas() runs the same zero iterations it did.

       The cost is worth naming because this is what buys it: the arm moved
       ahead of crt0's copy loops in the change that follows. Under a
       post-copy arm a misplaced magic costs the window across a reset. Under
       a pre-copy arm the .bss clear would erase an arm written moments
       earlier in the same boot, and the double tap would fail exactly as the
       defect being fixed failed. */
    .era_bootloader_magic (NOLOAD) : ALIGN(4)
    {
        __era_bootloader_magic_base__ = .;
        KEEP(*(.ram0.bootloader_magic))
        . = ALIGN(4);
        __era_bootloader_magic_end__ = .;
    } > ram0
}

/* Stock stack and memory rules: core0 stacks in the SRAM4 scratch bank,
   the ramN symbol set crt1.c's ram_areas table links against, and the
   elastic .heap spanning the ram0 remainder. The ChibiOS c1 stack
   reservation is dropped: SRAM5 is the ERA core1 private bank, populated
   through the .ram5_clear input sections rules_memory.ld collects.*/
INCLUDE rules_stacks.ld
INCLUDE rules_memory.ld

SECTIONS
{
    .flash_end : {
        __flash_binary_end = .;
    } > flash1
}

/* Pre-copy placement gate.
 *
 * crt0 makes exactly two calls before the CRT0_INIT_DATA copy loop finishes:
 * `bl __cpu_init` (crt0_v6m.S:192) and `bl __early_init` (:196). Everything
 * that closure reaches must live at a flash VMA, and rule 1 at the selector
 * list is why getting it wrong is silent - ld emits a veneer instead of an
 * error, and the fault it produces has no handler.
 *
 * These check one necessary condition: the entry symbol of every function
 * named in that closure links inside the carve-out. They are NOT a closure
 * proof - closure is a call-graph property, and a pinned function that calls
 * an unpinned one still passes. The manual objdump/nm review in
 * era_performance_gates.md stays the acceptance surface. What this adds is
 * that a selector which stops matching - a renamed object, a moved file, a
 * changed BOARD - fails the link instead of the boot.
 *
 * `& ~1` masks the Thumb bit that ARM toolchains may carry in an STT_FUNC
 * symbol value.
 *
 * One known false accusation: LTO_ENABLE=yes makes GCC hand ld ltrans
 * temporaries instead of the objects these selectors name, so every
 * object-anchored selector stops matching at once and several of these fire
 * together. The selectors are not gone in that case; LTO is incompatible with
 * the carve-out as written. LTO is off and needs its own measured gate before
 * adoption (era_sram_residency_contract.md). Failing loudly there is still the
 * right outcome — before these ASSERTs existed, the same build would have
 * linked and bricked the pre-copy path silently. */
ASSERT((Reset_Handler & ~1) >= __flash_startup_base__ && (Reset_Handler & ~1) < __flash_startup_end__,
       "ERA: Reset_Handler is outside the .flash_startup carve-out")
ASSERT((_crt0_entry & ~1) >= __flash_startup_base__ && (_crt0_entry & ~1) < __flash_startup_end__,
       "ERA: _crt0_entry is outside the .flash_startup carve-out")
ASSERT((__cpu_init & ~1) >= __flash_startup_base__ && (__cpu_init & ~1) < __flash_startup_end__,
       "ERA: __cpu_init is outside the .flash_startup carve-out")
ASSERT((__early_init & ~1) >= __flash_startup_base__ && (__early_init & ~1) < __flash_startup_end__,
       "ERA: __early_init is outside the .flash_startup carve-out")
ASSERT((__chibios_override___early_init & ~1) >= __flash_startup_base__ && (__chibios_override___early_init & ~1) < __flash_startup_end__,
       "ERA: the board __early_init is outside the .flash_startup carve-out - check BOARD and the path-anchored board.o selector")
/* Tighter than the carve-out bound on purpose. ERA overrides this hook to halt
   core1 before the copy loops run, and the failure that has no other symptom is
   the override quietly not being the one that linked - a dropped SRC line leaves
   QMK's empty weak stub in chibios.o, which is inside the carve-out and would
   pass the loose test while the halt does nothing. Requiring the symbol to land
   inside era_boot_core1_halt.o's own pinned extent rules that out and subsumes
   the carve-out check. */
ASSERT((early_hardware_init_pre & ~1) >= __era_boot_core1_halt_base__ && (early_hardware_init_pre & ~1) < __era_boot_core1_halt_end__,
       "ERA: early_hardware_init_pre did not resolve to the pinned era_boot_core1_halt.o - the SRC entry or the carve-out selector is gone")
ASSERT((early_hardware_init_post & ~1) >= __flash_startup_base__ && (early_hardware_init_post & ~1) < __flash_startup_end__,
       "ERA: early_hardware_init_post is outside the .flash_startup carve-out")

/* Vector-table residency gate, link-time half.
 *
 * vectors.o(.text) is pinned above for Reset_Handler, and ChibiOS's weak
 * default handlers share that translation unit, so every vector slot nobody
 * overrides resolves into .flash_startup. era_vector_defaults.c supplies strong
 * SRAM definitions for all 35 of them; its aliases collapse onto
 * era_unhandled_vector, so that one symbol landing in .sram_image is what says
 * the object linked at all.
 *
 * This is deliberately one ASSERT rather than 35. Asserting each overridden
 * name would be vacuous - a name ERA defines is in SRAM by construction - and
 * it would restate the enumeration it is supposed to police, so a name missing
 * from era_vector_defaults.c would be missing from the list here too. The
 * failure that enumeration cannot see is a driver that stops installing a
 * handler, dropping its slot back to the flash default. Only reading the linked
 * table's own bytes catches that, which is the .vectors gate in
 * common/tools/era_residency_gate.sh and runs on every automated UF2 build.
 * What this ASSERT adds is the earlier failure for a dropped SRC line: the link
 * itself fails before an artifact can reach that gate. */
ASSERT((era_unhandled_vector & ~1) >= __text_base__ && (era_unhandled_vector & ~1) < __text_end__,
       "ERA: era_unhandled_vector is not in .sram_image - the era_vector_defaults.c SRC entry is gone, and the vector table has fallen back to the flash-resident ChibiOS defaults")

/* Double-tap bootloader magic gate.
 *
 * The magic word survives a reset only because it lands outside crt0's .bss
 * clear range, and the .era_bootloader_magic section above is what puts it
 * there. These check that the claim did its job: exactly one word, and past
 * the address the clear loop stops at. A cleared magic is not a link error and
 * not a boot fault - it is a double-tap window that quietly stops opening, on
 * a board whose only firmware-independent recovery path is that window.
 *
 * The size test is the one that carries the load. It fires when the input
 * section stops matching - a renamed section attribute in rp2040.c, or that
 * translation unit leaving the link - which is the case that would otherwise
 * hand the word back to the `*(.ram0.*)` sweep silently. It also fires if a
 * second object joins the section, and that is a prompt to bracket the new
 * object separately rather than to widen this test.
 *
 * The base tests are redundant while this section sits immediately after
 * .bss. They exist so that moving it or disconnecting the pinned writer is
 * loud. */
ASSERT(__era_bootloader_magic_end__ - __era_bootloader_magic_base__ == 4,
       "ERA: .era_bootloader_magic is not exactly the one double-tap magic word - the .ram0.bootloader_magic input section stopped matching, or a second object joined it and needs its own bracketing")
ASSERT(__era_bootloader_magic_base__ >= __bss_end__,
       "ERA: .era_bootloader_magic starts inside crt0's .bss clear range, so the double-tap bootloader magic will not survive a reset")
ASSERT(magic_location == __era_bootloader_magic_base__,
       "ERA: magic_location is not the one word claimed by .era_bootloader_magic - the pre-copy writer and linker carve-out are disconnected")

/* The reservation is an assertion, not a placement rule: Session 1 must not
 * move the current load image merely to make room for an unselected backend. */
ASSERT(LENGTH(flash1) >= 128k,
       "ERA: effective flash range is too small for the reserved 128-KiB NVM")
ASSERT(__flash_binary_end <= __era_nvm_region_base__,
       "ERA: firmware load image overlaps the reserved final 128-KiB ERA NVM region")
