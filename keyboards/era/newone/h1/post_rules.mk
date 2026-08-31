# ERA's build options run in QMK's post_rules.mk phase, not the rules.mk one.
# The reason is at the phase guard in
# keyboards/era/common/system/era_common_qmk_rules.mk.

# --- What this board builds. Every line is a feature you can turn off: change
#     the value, rebuild, done. They must stay ABOVE the includes below, which
#     is where they are read -- a line under an include is silently ignored.
#     What each one costs, and the diagnostics and timing axes that are not
#     features, are in keyboards/era/era_build_options.mk.
#     `-e ERA_SHOW_OPTIONS=yes` on a build prints what it actually used.

ERA_RP2040_MATRIX_ENABLE = yes
ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY = 128
ERA_SOCD_ENABLE = yes
ERA_KKUK_ENABLE = yes
ERA_DEBOUNCE_ENABLE = yes
ERA_TAPPING_CONFIG_ENABLE = yes
ERA_MOUSEKEY_CONFIG_ENABLE = yes
TAP_DANCE_ENABLE = yes
ERA_VIA_BOOTLOADER_ENABLE = yes
ERA_EEPROM_CLEAN_ENABLE = yes
ERA_STORAGE_QUIET_DEFER_MS = 500

include keyboards/era/common/system/era_common_qmk_rules.mk
# Copy-to-RAM. Owner policy 2026-08-11: every ERA RP2040 board runs the image
# from SRAM and XIP is retired, because performance is hard to control on it.
# This include is the whole of the adoption -- linker script, residency marker,
# the pre-copy double-tap arm and the vector defaults are one bundle, and it
# refuses to run without the common rules above.
include keyboards/era/common/system/era_sram_resident_rules.mk
include keyboards/era/common/features/era_tapdance_rules.mk

# Last, so `-e ERA_SHOW_OPTIONS=yes` sees every option this board declared.
include keyboards/era/common/system/era_show_options.mk
