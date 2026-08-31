# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Host proof of exact-ms VIA get/set, GET_KEYBOARD_VALUE 0x06, and the system
# Jump-to-BOOT terminal lifecycle against the shipped ERA tapping / tap-dance,
# State Sync and VIA system units. Production ERA NVM/QMK integration is covered
# by tests/era_nvm_qmk_driver.

SRC += keyboards/era/common/features/era_tapping.c
SRC += keyboards/era/common/features/era_tapping_via.c
SRC += keyboards/era/common/features/era_tapdance.c
SRC += keyboards/era/common/features/era_tapdance_via.c
SRC += keyboards/era/common/storage/era_eeprom_config_io.c
SRC += keyboards/era/common/system/era_state_sync.c
SRC += keyboards/era/common/system/era_via_system.c
TAP_DANCE_ENABLE = yes
RGB_MATRIX_ENABLE = yes
RGB_MATRIX_DRIVER = custom
# The TEST platform's default 32-byte EEPROM cannot hold the production NVM
# map used below (the macro region ends at byte 745).
EEPROM_DRIVER = transient
INTROSPECTION_KEYMAP_C = keymap.c
OPT_DEFS += -DERA_TAPPING_CONFIG_ENABLE -DERA_TAP_DANCE_ENABLE -DERA_TAP_DANCE_KEYCODE_BASE=0x5700 -DVIA_ENABLE
OPT_DEFS += -DERA_STATE_SYNC_TEST -DERA_VIA_SYSTEM_TEST -DERA_VIA_SYSTEM_ENABLE -DERA_VIA_BOOTLOADER_ENABLE -DERA_EEPROM_CLEAN_ENABLE
OPT_DEFS += -DERA_HOST_PEER_STORAGE_V1_ENABLE -DRGB_MATRIX_LED_COUNT=1
OPT_DEFS += -DTRANSIENT_EEPROM_SIZE=1024
OPT_DEFS += -DVIA_EEPROM_MAGIC_ADDR=293 -DDYNAMIC_KEYMAP_EEPROM_ADDR=297 -DDYNAMIC_KEYMAP_LAYER_COUNT=4
OPT_DEFS += -DDYNAMIC_KEYMAP_MACRO_EEPROM_ADDR=617 -DDYNAMIC_KEYMAP_MACRO_EEPROM_SIZE=128
LDFLAGS += -Wl,--wrap=reset_keyboard -Wl,--wrap=soft_reset_keyboard
