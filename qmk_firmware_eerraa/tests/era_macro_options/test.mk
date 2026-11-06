# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Per-macro playback options through the production executor, VIA, common
# router and State Sync units: physical key edges through the QMK pipeline,
# the MACRO_OPTIONS channel, its table in the storage-adoption geometry, and
# its lifecycle across VIA's macro RESET, content edits and an EEPROM clear.
# EEPROM is QMK's transient RAM driver; ERA NVM transactions are covered by
# tests/era_nvm_qmk_driver.

# Reuse the USB-less common-router fixture's fixed RAW HID capacity.
COMMON_VPATH := tests/era_firmware_version $(filter-out tests/era_firmware_version,$(COMMON_VPATH))

EEPROM_DRIVER = transient
SRC += quantum/era_macro.c
SRC += quantum/via.c quantum/dynamic_keymap.c quantum/nvm/eeprom/nvm_via.c quantum/nvm/eeprom/nvm_dynamic_keymap.c
SRC += keyboards/era/common/system/era_macro_options.c
SRC += keyboards/era/common/system/era_common_via.c
SRC += keyboards/era/common/system/era_firmware_version.c
SRC += keyboards/era/common/system/era_state_sync.c
OPT_DEFS += -DVIA_ENABLE -DRAW_ENABLE -DDYNAMIC_KEYMAP_ENABLE
OPT_DEFS += -DERA_MACRO_ENABLE -DERA_MACRO_OPTIONS_ENABLE -DERA_STATE_SYNC_TEST

# The production dynamic Tap Dance, whose own records run macro keys, and SOCD,
# which resolves the outgoing report Ignore modifiers is judged on; both keep
# their settings in the ERA block.
TAP_DANCE_ENABLE = yes
INTROSPECTION_KEYMAP_C = tap_dance_defs.c
SRC += keyboards/era/common/features/era_tapdance.c keyboards/era/common/features/era_tapdance_via.c
SRC += keyboards/era/common/features/era_socd.c keyboards/era/common/features/era_socd_via.c
SRC += keyboards/era/common/storage/era_eeprom_config_io.c
OPT_DEFS += -DERA_TAP_DANCE_ENABLE -DERA_TAP_DANCE_KEYCODE_BASE=0x5700 -DERA_SOCD_ENABLE

# The storage-adoption geometry (storage/era_storage_adoption.h), by value.
OPT_DEFS += -DTRANSIENT_EEPROM_SIZE=24576 -DERA_STORAGE_DYNAMIC_MACRO_EEPROM_SIZE=16384
OPT_DEFS += -DVIA_EEPROM_MAGIC_ADDR=317 -DDYNAMIC_KEYMAP_EEPROM_ADDR=321 -DDYNAMIC_KEYMAP_LAYER_COUNT=6
OPT_DEFS += -DDYNAMIC_KEYMAP_MACRO_EEPROM_ADDR=801 -DDYNAMIC_KEYMAP_MACRO_EEPROM_SIZE=16384

# Count and fail options-table writes at the driver boundary.
LDFLAGS += -Wl,--wrap=eeprom_write_block

# The fixture supplies the matrix keymap, as in tests/era_macro.
$(TEST_OBJ)/$(TEST_OUTPUT)/quantum/dynamic_keymap.o: FILE_SPECIFIC_CFLAGS += -Dkeycode_at_keymap_location=macro_fixture_keycode_at_keymap_location
