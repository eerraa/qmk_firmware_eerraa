# Every ERA automated build carries one board-independent configuration name.
# The name and its mapping live here, in the tool-neutral make layer; host
# adapters only pass it through and must not reproduce this list.
include keyboards/era/era_build_identity_options.mk

ifndef ERA_BUILD_VARIANT_RULES_INCLUDED
ERA_BUILD_VARIANT_RULES_INCLUDED := yes

# Every ERA target reaches this file, the ATmega exception included, and the
# automated build refuses one that does not, so this is where the pinned VIA
# EEPROM magic joins every image (storage/era_via_magic.h).
CONFIG_H += keyboards/era/common/storage/era_via_magic.h

ERA_BUILD_VARIANTS := standard reset wire qwin cause stale qwin_phase

ifneq ($(strip $(ERA_TOMAK79H_BUILD_PROFILE)),)
    $(error ERA_TOMAK79H_BUILD_PROFILE has retired; use the board-independent ERA_BUILD_VARIANT)
endif

ifeq ($(filter $(strip $(ERA_BUILD_VARIANT)),$(ERA_BUILD_VARIANTS)),)
    $(error Invalid ERA_BUILD_VARIANT=$(ERA_BUILD_VARIANT); expected one of $(ERA_BUILD_VARIANTS))
endif

ifeq ($(filter $(strip $(ERA_BUILD_VARIANT)),standard reset),)
    ifneq ($(strip $(SPLIT_KEYBOARD)),yes)
        $(error ERA_BUILD_VARIANT=$(ERA_BUILD_VARIANT) requires a split keyboard; use standard for this target)
    endif
endif

# Reset is the ordinary feature set with an install policy, not a diagnostic
# or a minimal keymap. Non-split RP2040 targets admit it as well.
include keyboards/era/common/build_variants/$(strip $(ERA_BUILD_VARIANT)).mk
override ERA_SETTINGS_RESET_IMAGE := $(if $(filter reset,$(strip $(ERA_BUILD_VARIANT))),yes,no)

ifeq ($(ERA_SETTINGS_RESET_IMAGE),yes)
    ifneq ($(strip $(MCU_SERIES)),RP2040)
        $(error ERA_BUILD_VARIANT=reset requires an ERA RP2040 SRAM-resident target)
    endif
    OPT_DEFS += -DERA_SETTINGS_RESET_IMAGE
    SRC += keyboards/era/common/storage/era_settings_reset.c
    SRC += keyboards/era/common/storage/era_settings_reset_rp2040.c
endif

# One resolved identity, owned by the same make layer that forced its values.
# `override` inside the selected fragment is intentional: command-line option
# assignments, MAKEFLAGS, exported variables, and make -e may select a variant
# name, but none may mutate one axis underneath that name.
ERA_BUILD_VARIANT_TUPLE := wire=$(strip $(ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE)),qwin=$(strip $(ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE)),phase=$(strip $(ERA_PASS_PHASE_DIAGNOSTICS_ENABLE)),cause=$(strip $(ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE)),stale=$(strip $(ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE)),reset=$(ERA_SETTINGS_RESET_IMAGE)

ifneq ($(words $(filter yes no,$(ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE) $(ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE) $(ERA_PASS_PHASE_DIAGNOSTICS_ENABLE) $(ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE) $(ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE) $(ERA_SETTINGS_RESET_IMAGE))),6)
    $(error ERA_BUILD_VARIANT=$(ERA_BUILD_VARIANT) did not resolve to six yes/no axes: $(ERA_BUILD_VARIANT_TUPLE))
endif

ifeq ($(strip $(ERA_BUILD_IDENTITY_REPORT)),yes)
    $(info ERA_BUILD_IDENTITY variant=$(strip $(ERA_BUILD_VARIANT)) tuple=$(ERA_BUILD_VARIANT_TUPLE))
endif

endif
