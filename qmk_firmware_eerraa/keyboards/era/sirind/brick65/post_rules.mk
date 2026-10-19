# Brick65 remains the atmega32u4 runtime exception: this file links no ERA
# firmware, changes no QMK feature, and adopts no copy-to-RAM rule. It takes
# only the repository-wide automated-build identity and option printer so the
# same artifact naming contract is enforced for all keyboards/era targets.
include keyboards/era/common/system/era_build_variant_rules.mk
include keyboards/era/common/system/era_show_options.mk

# Brick65 is the ATmega runtime exception, but RGB Sleep uses the same QMK
# keymap-config bit as the RP2040 ERA boards so the core suspend gate can stay
# one contract across the repository.
OPT_DEFS += -DERA_RGB_SLEEP_MASTER_ENABLE
SRC += keyboards/era/common/features/era_rgb_sleep.c
