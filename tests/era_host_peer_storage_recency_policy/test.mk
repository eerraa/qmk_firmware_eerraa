# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Header-only deterministic proof of the production recency persistence policy.
# This deliberately avoids a fake host-peer runtime: the production storage
# owner consumes these exact predicates at its publish/retire boundaries.
