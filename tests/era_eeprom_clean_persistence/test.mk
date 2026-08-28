# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Causal host proof for the EEPROM CLEAN persistence boundary. This links the
# production wear-leveling and EEPROM-driver units and uses QMK's existing
# physical backing-store mock; no production test branch is required.

EEPROM_DRIVER = wear_leveling
WEAR_LEVELING_DRIVER = custom

SRC += $(QUANTUM_PATH)/wear_leveling/tests/backing_mocks.cpp
VPATH += $(QUANTUM_PATH)/wear_leveling/tests

INTROSPECTION_KEYMAP_C = keymap.c
OPT_DEFS += -DERA_HOST_PEER_STORAGE_V1_ENABLE
OPT_DEFS += -DERA_SRAM_RESIDENT_IMAGE -DBACKING_STORE_WRITE_SIZE=2
OPT_DEFS += -DWEAR_LEVELING_BACKING_SIZE=48 -DWEAR_LEVELING_LOGICAL_SIZE=16
LDFLAGS += -Wl,--wrap=backing_store_write

# The production public facade retains QMK's 32-bit address cast. TEST is a
# 64-bit host ABI; the raw checked seam under test already uses uintptr_t.
$(TEST_OBJ)/$(TEST_OUTPUT)/eeprom_wear_leveling.o: FILE_SPECIFIC_CFLAGS += -Wno-error=pointer-to-int-cast
