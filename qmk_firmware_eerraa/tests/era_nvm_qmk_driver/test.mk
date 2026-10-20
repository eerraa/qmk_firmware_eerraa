# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Production-boundary host proof: QMK's stock EEPROM/update helpers and stock
# nvm_dynamic_keymap.c execute against ERA's real custom EEPROM adapter and NVM
# engine. The test supplies only the physical NOR backend.
EEPROM_DRIVER = custom
INTROSPECTION_KEYMAP_C = keymap.c
# Reuse the USB-less common-router fixture's fixed RAW HID capacity.
COMMON_VPATH := tests/era_firmware_version $(filter-out tests/era_firmware_version,$(COMMON_VPATH))

SRC += keyboards/era/common/storage/era_nvm.c
SRC += keyboards/era/common/storage/era_eeprom_driver.c
SRC += keyboards/era/common/system/era_macro_replace.c
SRC += keyboards/era/common/system/era_common_via.c
SRC += keyboards/era/common/system/era_common_features.c
SRC += keyboards/era/common/system/era_firmware_version.c
OPT_DEFS += -DERA_MACRO_REPLACE_ENABLE
OPT_DEFS += -DERA_HOST_PEER_STORAGE_V1_ENABLE
SRC += quantum/nvm/eeprom/nvm_dynamic_keymap.c

OPT_DEFS += -DEEPROM_SIZE=24576
OPT_DEFS += -DVIA_ENABLE
OPT_DEFS += -DVIA_EEPROM_MAGIC_ADDR=317
OPT_DEFS += -DDYNAMIC_KEYMAP_EEPROM_ADDR=321
OPT_DEFS += -DDYNAMIC_KEYMAP_LAYER_COUNT=6
OPT_DEFS += -DDYNAMIC_KEYMAP_MACRO_EEPROM_ADDR=801
OPT_DEFS += -DDYNAMIC_KEYMAP_MACRO_EEPROM_SIZE=16384

SRC += quantum/via.c quantum/dynamic_keymap.c quantum/nvm/eeprom/nvm_via.c
OPT_DEFS += -DRAW_ENABLE -DDYNAMIC_KEYMAP_ENABLE -DVIA_EEPROM_ALLOW_RESET
