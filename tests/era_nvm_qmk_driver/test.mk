# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Production-boundary host proof: QMK's stock EEPROM/update helpers and stock
# nvm_dynamic_keymap.c execute against ERA's real custom EEPROM adapter and NVM
# engine. The test supplies only the physical NOR backend.
EEPROM_DRIVER = custom
INTROSPECTION_KEYMAP_C = keymap.c

SRC += keyboards/era/common/storage/era_nvm.c
SRC += keyboards/era/common/storage/era_eeprom_driver.c
SRC += quantum/nvm/eeprom/nvm_dynamic_keymap.c

OPT_DEFS += -DEEPROM_SIZE=24576
OPT_DEFS += -DVIA_ENABLE
OPT_DEFS += -DVIA_EEPROM_MAGIC_ADDR=293
OPT_DEFS += -DDYNAMIC_KEYMAP_EEPROM_ADDR=297
OPT_DEFS += -DDYNAMIC_KEYMAP_LAYER_COUNT=4
OPT_DEFS += -DDYNAMIC_KEYMAP_MACRO_EEPROM_ADDR=617
OPT_DEFS += -DDYNAMIC_KEYMAP_MACRO_EEPROM_SIZE=16384
