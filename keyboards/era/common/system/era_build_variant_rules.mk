# Every ERA automated build carries one board-independent configuration name.
# The name and its mapping live here, in the tool-neutral make layer; host
# adapters only pass it through and must not reproduce this list.
include keyboards/era/era_build_identity_options.mk

ifndef ERA_BUILD_VARIANT_RULES_INCLUDED
ERA_BUILD_VARIANT_RULES_INCLUDED := yes

ERA_BUILD_VARIANTS := standard wire qwin cause stale qwin_phase

ifneq ($(strip $(ERA_TOMAK79H_BUILD_PROFILE)),)
    $(error ERA_TOMAK79H_BUILD_PROFILE has retired; use the board-independent ERA_BUILD_VARIANT)
endif

ifeq ($(filter $(strip $(ERA_BUILD_VARIANT)),$(ERA_BUILD_VARIANTS)),)
    $(error Invalid ERA_BUILD_VARIANT=$(ERA_BUILD_VARIANT); expected one of $(ERA_BUILD_VARIANTS))
endif

ifneq ($(strip $(ERA_BUILD_VARIANT)),standard)
    ifneq ($(strip $(SPLIT_KEYBOARD)),yes)
        $(error ERA_BUILD_VARIANT=$(ERA_BUILD_VARIANT) requires a split keyboard; use standard for this target)
    endif
endif

# Every target includes exactly one complete combination. Non-split targets
# admit only `standard`, but applying its five explicit `no` values there too
# keeps a stale environment from making the resolved options disagree with the
# artifact name.
include keyboards/era/common/build_variants/$(strip $(ERA_BUILD_VARIANT)).mk

# One resolved identity, owned by the same make layer that forced its values.
# `override` inside the selected fragment is intentional: command-line option
# assignments, MAKEFLAGS, exported variables, and make -e may select a variant
# name, but none may mutate one axis underneath that name.
ERA_BUILD_VARIANT_TUPLE := wire=$(strip $(ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE)),qwin=$(strip $(ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE)),phase=$(strip $(ERA_PASS_PHASE_DIAGNOSTICS_ENABLE)),cause=$(strip $(ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE)),stale=$(strip $(ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE))

ifneq ($(words $(filter yes no,$(ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE) $(ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE) $(ERA_PASS_PHASE_DIAGNOSTICS_ENABLE) $(ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE) $(ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE))),5)
    $(error ERA_BUILD_VARIANT=$(ERA_BUILD_VARIANT) did not resolve to five yes/no axes: $(ERA_BUILD_VARIANT_TUPLE))
endif

ifeq ($(strip $(ERA_BUILD_IDENTITY_REPORT)),yes)
    $(info ERA_BUILD_IDENTITY variant=$(strip $(ERA_BUILD_VARIANT)) tuple=$(ERA_BUILD_VARIANT_TUPLE))
endif

endif
