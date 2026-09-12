# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
# Real LINK, VIA, agreement, cold scheduler transition, EEPROM adapter and NVM.
# Only physical NOR, serial/owner handshakes and clock/peer facts are replaced.
EEPROM_DRIVER = custom
INTROSPECTION_KEYMAP_C = keymap.c
SRC += tests/era_split_link_lifecycle/link_under_test.c
SRC += keyboards/era/common/storage/era_nvm.c
OPT_DEFS += -DEEPROM_SIZE=24576 -DVIA_ENABLE -DERA_VIA_SYSTEM_ENABLE
OPT_DEFS += -DSERIAL_USART_SPEED=460800
OPT_DEFS += -DVIA_EEPROM_MAGIC_ADDR=293 -DDYNAMIC_KEYMAP_EEPROM_ADDR=297
OPT_DEFS += -DDYNAMIC_KEYMAP_LAYER_COUNT=4
OPT_DEFS += -DDYNAMIC_KEYMAP_MACRO_EEPROM_ADDR=617 -DDYNAMIC_KEYMAP_MACRO_EEPROM_SIZE=16384
LDFLAGS += -Wl,--gc-sections -Wl,--wrap=soft_reset_keyboard
