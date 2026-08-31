# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Deterministic proof of the compile-time VERSION identity and its VIA-only,
# GET-only common-router surface.

# The TEST platform has no USB descriptor, while both production units include
# usb_descriptor.h only for RAW_EPSIZE. Put this fixture's one-macro stub ahead
# of the protocol directory without changing either production source path.
VPATH := $(TEST_PATH) $(filter-out $(TEST_PATH),$(VPATH))

SRC += keyboards/era/common/system/era_firmware_version.c
SRC += keyboards/era/common/system/era_common_via.c
OPT_DEFS += -DVIA_ENABLE -DERA_VIA_SYSTEM_ENABLE
