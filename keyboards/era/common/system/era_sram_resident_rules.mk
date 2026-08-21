# The ERA copy-to-RAM image: the whole image executes from SRAM, and flash
# keeps only boot2, the vectors LMA, the startup carve-out, and the load image.
# Contract: keyboards/era/common/docs/contracts/era_sram_residency_contract.md
#
# These are one bundle rather than four switches. Taking any of them without
# the others is a defect, and the linker script is built to fail the link
# rather than the boot when that happens - which is why they belong in one
# includable file instead of being copied per board.
#
# They lived inside era_split_qmk_rules.mk until 2026-08-01, when every
# copy-to-RAM board happened to be a split one. The boundary they actually draw
# is residency, and the reason written beside the pre-copy arm in
# platforms/chibios/bootloaders/rp2040.c has always said so: "on an image that
# is copied out of flash first this point is far too late".
#
# Including this file is a per-board decision and carries a device-verification
# debt, because it changes where every instruction in the image executes from.

# This bundle cannot be taken alone, and the two ways it fails without the ERA
# common layer are why this is a refusal rather than a note. The
# RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM emitted below assigns
# double_tap_armed, which platforms/chibios/bootloaders/rp2040.c declares only
# under ..._NONBLOCKING -- so without the common layer the build does not
# compile. Add the NONBLOCKING define by hand and it does, and then the board
# bricks: the only caller of rp2040_bootloader_double_tap_reset_task() in the
# tree is era_common_features.c, and with nothing closing the window the magic
# is never cleared and every physical reset enters BOOTSEL, permanently.
#
# One refusal before any compile, naming both files, instead of an undefined
# identifier in a QMK core file that mentions neither.
ifneq ($(strip $(ERA_COMMON_QMK_RULES_INCLUDED)), yes)
    $(error the copy-to-RAM bundle requires keyboards/era/common/system/era_common_qmk_rules.mk, included first from this board post_rules.mk -- it supplies the non-blocking double-tap window this bundle arms, and the only task that closes it)
endif

# The same argument one layer up, and it became reachable on 2026-08-13. Before
# the class skeletons existed, every board wrote housekeeping_task_kb itself and
# the refusal above was the whole of what could go wrong. Now a board can turn
# the skeleton off, and the off state *builds*: QMK's own weak
# housekeeping_task_kb takes over, nothing calls era_common_features_task(), the
# window this bundle armed before crt0's copy loops is never cleared, and the
# board enters BOOTSEL on every physical reset for the rest of its life. On a
# split board the same off state also drops era_split_keyboard_post_init(), so
# core1 never launches. Both are silent.
#
# So this combination is a real non-composition in the sense era_source_map.md's
# Build Selectors section defines -- like ERA_TAPPING_CONFIG_ENABLE=no on a split
# board -- and it stops before any compile naming both selectors instead of
# shipping an image that bricks. A board that genuinely wants its own hooks
# copies the skeleton rather than deleting it.
ifneq ($(strip $(ERA_BOARD_COMMON_ENABLE)), yes)
    $(error the copy-to-RAM bundle requires ERA_BOARD_COMMON_ENABLE=yes; the class skeleton owns housekeeping_task_kb, which is the only closer of the double-tap window this bundle arms, and an image with the arm and no closer enters BOOTSEL on every reset permanently)
endif

MCU_LDSCRIPT = ERA_RP2040_SRAM_RESIDENT
OPT_DEFS += -DERA_SRAM_RESIDENT_IMAGE

# The residency contract's ELF gate requires the veneer map to be reviewed on
# the first link, and the carve-out rule it enforces (a pre-copy caller must
# grow no flash->SRAM veneer) is only visible in a map. QMK emits one solely
# under DEBUG_ENABLE, which also turns on -ggdb3, so the artifact the gate
# names did not exist on any build the gate actually runs against. Emitting it
# unconditionally changes no code; a duplicate -Map from DEBUG_ENABLE is
# last-wins in ld.
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref

# The pre-copy window: one flash-pinned early_hardware_init_pre() override with
# two jobs, because the hardware offers exactly one call before crt0 rebuilds
# SRAM and both jobs need to be on that side of it.
#
# The double-tap arm is why this belongs to residency. On an image copied out
# of flash first, __late_init() is tens of milliseconds after reset and a
# tweezer double tap lands entirely inside that, so arming there opens no
# window a hand can hit.
#
# The core1 hardware halt is the other job, and it is split-relevant only. It
# is deliberately not conditional. The sequence is idempotent, and a core1 that
# was never launched sits in the bootrom wait-for-vector loop that set-then-
# clear returns it to - the cold-boot case every split board already takes on
# every power-on. Guarding it would mean editing a flash-pinned pre-copy
# function for no behavioural difference, in the one place where a mistake runs
# with interrupts masked and no fault handler behind it.
#
# The matching .flash_startup selector and its ASSERT are in the linker script;
# if this line goes, the link fails rather than the boot.
SRC += keyboards/era/common/system/era_boot_core1_halt.c

# Says the pre-copy arm above is linked, which is what tells
# platforms/chibios/bootloaders/rp2040.c that what reaches __late_init() is a
# bootloader request rather than the armed state.
#
# This line and the SRC line above are one fact and belong together. Setting
# this without the writer would shut the window permanently, and that
# combination is what the early_hardware_init_pre ASSERT in the linker script
# catches - it fails the link, not the boot.
OPT_DEFS += -DRP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM

# Strong SRAM definitions for the vector slots ChibiOS leaves at its weak
# default, which otherwise resolve into the flash carve-out that pins vectors.o
# for Reset_Handler. This belongs to residency rather than to split: it was
# called split-only because the non-split ERA boards were XIP builds, and an
# XIP build is exactly what including this file stops a board being. If this
# line goes, the link fails on the era_unhandled_vector ASSERT in the linker
# script rather than quietly putting 35 entries back in flash.
SRC += keyboards/era/common/system/era_vector_defaults.c
