# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

SPLIT_KEYBOARD ?= yes
include keyboards/era/common/system/era_build_variant_rules.mk

.PHONY: print
print:
	@printf 'FIXTURE variant=%s tuple=%s\n' '$(ERA_BUILD_VARIANT)' '$(ERA_BUILD_VARIANT_TUPLE)'
