# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Deterministic host proof for the Session-1 ERA NVM format. The flash model in
# test_era_nvm.cpp enforces the same conservative NOR geometry as production:
# 0xFF erase state, 1->0 programming, 256-byte pages and 4-KiB sectors.
SRC += keyboards/era/common/storage/era_nvm.c

