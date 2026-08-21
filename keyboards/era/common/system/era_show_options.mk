# Every ERA option this file reads is declared in keyboards/era/era_build_options.mk,
# which is the one file to edit. Included here rather than by the board so this
# fragment cannot run without its declarations; a board or a profile assigning
# above the include that reads it still wins, because every line there is `?=`.
include keyboards/era/era_build_options.mk

# What did this build actually use?
#
#   qmk compile -kb era/sirind/tomak79h -km via -e ERA_SHOW_OPTIONS=yes
#
# prints every ERA build option with its value and where the value came from --
# `file` for a default or a board line, `command line` for a -e, `environment`
# for an exported variable. QMK answers the same question for its own switches
# with `make <kb>:<km>:show_build_options`; its BUILD_OPTION_NAMES lists are
# plain `=` assignments in builddefs/show_options.mk, which is included after
# this phase, so an ERA append there would be overwritten and this is the
# equivalent rather than an extension of it.
#
# Included last from a board's post_rules.mk, because $(.VARIABLES) is evaluated
# where it is written and the answer is only complete once every ERA fragment
# has run.
#
# The set is derived rather than listed, and that choice has a direction. A new
# option appears here automatically; a new *non-option* whose name matches the
# pattern also appears, until it is added to the exclusion list below. So the
# failure mode is a non-option shown once and noticed, never an option silently
# missing -- which is the direction that matters, because a list that can omit
# an option is how the RGB set stayed invisible for as long as it did.
#
# That promise was false for the RGB half of the pattern until 2026-08-16, and
# in exactly the way it forbids. The filter enumerated RGB_MATRIX_RENDER_% and
# RGB_MATRIX_INDICATORS_% -- the two prefixes that happened to exist when it was
# written -- so it was a list wearing a pattern's clothes, and
# RGB_MATRIX_IDLE_GATE_ENABLE was declared, emitted, read and shipped without
# ever appearing here. RGB_MATRIX_% is the pattern the prose already described;
# what it costs is the two QMK-owned names below, which is the cheap direction.


ERA_SHOW_OPTIONS_NOT_OPTIONS := \
    ERA_SHOW_OPTIONS \
    ERA_SHOW_OPTIONS_NOT_OPTIONS \
    ERA_SHOW_OPTIONS_NAMES \
    ERA_BUILD_OPTIONS_INCLUDED \
    ERA_COMMON_QMK_RULES_INCLUDED \
    ERA_COMMON_VIA_SRCS \
    ERA_SPLIT_COMMUNICATION_CORE_LEGACY_STAGE_VARS \
    ERA_TOMAK79H_BUILD_PROFILES \
    ERA_EDIT_TREE \
    ERA_BUILD_JOBS \
    RGB_MATRIX_ENABLE \
    RGB_MATRIX_DRIVER

# QMK generates an included dependency file during the build, and remaking an
# included makefile makes GNU make re-exec itself and parse everything a second
# time. MAKE_RESTARTS is make's own signal for that pass, so testing it prints
# the block once rather than once per parse.
ifeq ($(strip $(ERA_SHOW_OPTIONS))$(MAKE_RESTARTS), yes)
ERA_SHOW_OPTIONS_NAMES := $(sort $(filter-out $(ERA_SHOW_OPTIONS_NOT_OPTIONS), \
    $(filter ERA_% RGB_MATRIX_% TAP_DANCE_ENABLE,$(.VARIABLES))))

$(info )
$(info ERA build options for $(KEYBOARD):$(KEYMAP))
$(foreach o,$(ERA_SHOW_OPTIONS_NAMES),$(info $(shell printf '  %-52s = %-10s # %s' '$(o)' '$($(o))' '$(origin $(o))')))
$(info )
$(info   The complete list with defaults and where each is declared is in)
$(info   keyboards/era/common/docs/manuals/era_build_options.md, The selectors.)
$(info   Set one for a build with -e NAME=value, or)
$(info   permanently by assigning it in the board post_rules.mk above the)
$(info   include that reads it.)
$(info )
endif
