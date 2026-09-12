# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Deterministic proof of the production report-interval unit: the platform
# clock is milliseconds passed by the test, the sink records every post and
# can refuse room or be inactive, and completions are stamped by the test.
SRC += keyboards/era/common/system/era_hid_report_interval.c
SRC += tests/era_hid_report_interval/platform_under_test.c
