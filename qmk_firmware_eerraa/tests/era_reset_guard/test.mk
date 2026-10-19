# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Host proof of the boot reset guard: the production unit against QMK's own
# eeconfig on a transient EEPROM, with the ERA config block as an in-memory stub.

SRC += keyboards/era/common/storage/era_reset_guard.c
EEPROM_DRIVER = transient
OPT_DEFS += -DTRANSIENT_EEPROM_SIZE=1024
