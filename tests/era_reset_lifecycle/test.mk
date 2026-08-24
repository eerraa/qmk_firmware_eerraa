# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Focused host proof of ERA shutdown ordering and reset-key deferral across a
# sliced flash gap. The production gate and slice owners are linked; the TEST
# platform supplies its own strong matrix_init_kb(), so the class seam itself
# is represented in the test unit and covered by the firmware builds.

SRC += keyboards/era/common/system/era_board_hooks.c
SRC += keyboards/era/common/system/era_flash_slice.c
VPATH += $(QUANTUM_DIR)/wear_leveling

INTROSPECTION_KEYMAP_C = keymap.c
OPT_DEFS += -DERA_SRAM_RESIDENT_IMAGE -DERA_STORAGE_QUIET_DEFER_MS=500 -DVIA_ENABLE
LDFLAGS += -Wl,--wrap=bootloader_jump -Wl,--wrap=mcu_reset -Wl,--wrap=eeconfig_disable
