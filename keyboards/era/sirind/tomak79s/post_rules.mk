# ERA's build options run in QMK's post_rules.mk phase, not the rules.mk one.
# The reason is at the phase guard in
# keyboards/era/common/system/era_common_qmk_rules.mk.

SPLIT_TRANSPORT = custom
ERA_RP2040_MATRIX_ENABLE = yes

# --- What this board builds. Every line is a feature you can turn off: change
#     the value, rebuild, done. They must stay ABOVE the includes below, which
#     is where they are read -- a line under an include is silently ignored.
#     What each one costs, and the diagnostics and timing axes that are not
#     features, are in keyboards/era/era_build_options.mk.
#     `-e ERA_SHOW_OPTIONS=yes` on a build prints what it actually used.

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
RGB_MATRIX_RENDER_POLICY_ENABLE = yes
RGB_MATRIX_RENDER_DOMAIN_ENABLE = yes
RGB_MATRIX_INDICATORS_INDEPENDENT_ENABLE = yes
RGB_MATRIX_INDICATORS_WHEN_DISABLED_ENABLE = yes
RGB_MATRIX_IDLE_GATE_ENABLE = yes
ERA_SPLIT_RP_USB_SLEEP_SYNC = 1
ERA_SPLIT_SERIAL_USART_SPEED = 460800

# Set inside the VIA gate rather than beside the lines above, because the
# storage engine this option selects requires VIA and era_split_qmk_rules.mk
# refuses the pair without it. VIA_ENABLE is tested rather than the keymap's
# name: a keymap that enables VIA under another name would otherwise lose
# EEPROM sync silently. Same shape as tomak79h's.
ifeq ($(strip $(VIA_ENABLE)),yes)
    ERA_SPLIT_EEPROM_SYNC_ENABLE := yes
endif

# The tomak family's whole board content, shared by all three boards: the
# indicator configuration and its persisted record, the ERA reset guard, the
# EEPROM-sync reload table, the VIA surface, the STATUS field's two producers
# and every QMK hook the family owns. **This board keeps no .c of its own** --
# what differs between the three is geometry, and geometry is declared in
# keyboard.json and config.h. The top of sirind/common/tomak_common.h is the
# authority on that boundary.
SRC += keyboards/era/sirind/common/tomak_common.c

# The EEPROM geometry the replacement-storage engine is laid out in: the
# 24 KiB logical span, the exact 16 KiB macro domain, and the VIA magic address
# that puts the layout options at 296 and the keymap at 297. Unconditional and
# above the split fragment, which refuses EEPROM sync without it -- the stored
# layout is a fact about this board, not about whether the halves synchronise
# it, so a non-VIA keymap gets the same one.
include keyboards/era/common/storage/era_storage_adoption_rules.mk
include keyboards/era/common/features/era_tapdance_rules.mk
include keyboards/era/common/system/era_rgb_matrix_rules.mk
include keyboards/era/common/split/era_split_qmk_rules.mk

# Last, so `-e ERA_SHOW_OPTIONS=yes` sees every option this board declared.
include keyboards/era/common/system/era_show_options.mk
