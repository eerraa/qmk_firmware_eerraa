# Brick65 remains the atmega32u4 runtime exception: this file links no ERA
# firmware, changes no QMK feature, and adopts no copy-to-RAM rule. It takes
# only the repository-wide automated-build identity and option printer so the
# same artifact naming contract is enforced for all keyboards/era targets.
include keyboards/era/common/system/era_build_variant_rules.mk
include keyboards/era/common/system/era_show_options.mk
