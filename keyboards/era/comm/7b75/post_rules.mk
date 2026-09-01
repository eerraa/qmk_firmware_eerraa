# ERA's build options run in QMK's post_rules.mk phase, not the rules.mk one.

# --- What this board builds ------------------------------------------------
ERA_RP2040_MATRIX_ENABLE = yes
ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY = 128
ERA_SOCD_ENABLE = yes
ERA_KKUK_ENABLE = yes
ERA_DEBOUNCE_ENABLE = yes
ERA_TAPPING_CONFIG_ENABLE = yes
ERA_MOUSEKEY_CONFIG_ENABLE = yes
TAP_DANCE_ENABLE = yes
ERA_BACKLIGHT_EFFECT_ENABLE = yes
ERA_BACKLIGHT_LOCK_ENABLE = yes
ERA_VIA_BOOTLOADER_ENABLE = yes
ERA_EEPROM_CLEAN_ENABLE = yes
ERA_STORAGE_QUIET_DEFER_MS = 500

include keyboards/era/common/system/era_common_qmk_rules.mk
include keyboards/era/common/system/era_sram_resident_rules.mk
include keyboards/era/common/features/era_tapdance_rules.mk

# Last, so `-e ERA_SHOW_OPTIONS=yes` sees every option this board declared.
include keyboards/era/common/system/era_show_options.mk
