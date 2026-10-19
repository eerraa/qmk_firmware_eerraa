# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Deterministic proof of the compile-time VERSION identity and its VIA-only,
# GET-only common-router surface.

# The TEST platform has no USB descriptor, while both production units include
# usb_descriptor.h only for RAW_EPSIZE. Put this fixture's one-macro stub ahead
# of the protocol directory without changing either production source path.
# Keep VPATH's deferred COMMON_VPATH reference intact so driver paths added by
# later QMK feature rules (including quantum/nvm/eeprom) remain searchable.
COMMON_VPATH := $(TEST_PATH) $(filter-out $(TEST_PATH),$(COMMON_VPATH))

SRC += keyboards/era/common/system/era_firmware_version.c
SRC += keyboards/era/common/system/era_common_via.c
SRC += keyboards/era/common/features/era_rgb_sleep.c
OPT_DEFS += -DVIA_ENABLE -DERA_VIA_SYSTEM_ENABLE -DERA_RGB_SLEEP_MASTER_ENABLE
