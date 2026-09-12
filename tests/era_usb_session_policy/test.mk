# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Pure verdict plus the production RP2040 sampler against controlled registers.
SRC += tests/era_usb_session_policy/sampler_under_test.c
OPT_DEFS += -Itests/era_usb_session_policy/include
