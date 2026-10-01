# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Host proof of SOCD resolved on the outgoing report: the production unit behind
# QMK's report path, with KKUK beside it and the ERA config block as an
# in-memory stub in the test.

SRC += keyboards/era/common/features/era_socd.c
SRC += keyboards/era/common/features/era_kkuk.c
OPT_DEFS += -DERA_SOCD_ENABLE
NKRO_ENABLE = yes
