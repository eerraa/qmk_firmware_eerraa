# Every ERA option this file reads is declared in keyboards/era/era_build_options.mk,
# which is the one file to edit. Included here rather than by the board so this
# fragment cannot run without its declarations; a board or a variant assigning
# above the include that reads it still wins, because every line there is `?=`.
include keyboards/era/era_build_options.mk

# The ERA RGB Matrix render work: the render policy, its three sub-options, and
# the deferred config flush. RGB_MATRIX only -- an RGBLIGHT board takes none of
# it, which is why this is a per-board include and not part of the common rules.
#
# These were five hand-copied OPT_DEFS lines in three board rules.mk until
# 2026-08-11, so the whole set had no off state and no declaration anywhere. The
# C side was already switchable: every reader in quantum/rgb_matrix/rgb_matrix.c
# is guarded, and rgb_matrix_render_policy_sanitize() clears each sub-option's
# policy flag when its define is absent. Only the make side was missing.
#
# The make variable and the macro share a name deliberately, so one grep finds
# the switch and every reader of it.

# This work is QMK RGB Matrix's. An RGBLIGHT board that included this file
# would get four defines with no reader compiled -- silently inert, which is
# the outcome the refusal further down exists to prevent one level in.
# Checkable only since the option layer moved to the post_rules.mk phase.
ifneq ($(strip $(RGB_MATRIX_ENABLE)), yes)
    $(error era_rgb_matrix_rules.mk is RGB Matrix work; this board does not enable RGB_MATRIX)
endif


# The deferred config flush has no switch: eeconfig_flush_rgb_matrix_deferred_task()
# runs from rgb_task_sync() on every ERA_STORAGE_QUIET_DEFER_MS build since
# 2026-08-13, when the ERA_RGB_MATRIX_DEFERRED_FLUSH_HOUSEKEEPING_ENABLE
# relocation retired with the RAM-island premise it served (history in the
# retiring commit).

# The three sub-options are the policy's, and with the policy off each is
# sanitized away rather than rejected -- a configuration that reads as set and
# does nothing. Refused here instead, because silently inert is the one outcome
# this project ranks below a build failure. RENDER_DOMAIN is the near miss: it
# also selects an out-of-line rgb_matrix_check_finished_leds() over the header's
# static inline, so without the policy it is a linkage change with identical
# behaviour, which is not a configuration worth admitting either.
ifneq ($(strip $(RGB_MATRIX_RENDER_POLICY_ENABLE)), yes)
    ifneq ($(strip $(RGB_MATRIX_RENDER_DOMAIN_ENABLE))$(strip $(RGB_MATRIX_INDICATORS_INDEPENDENT_ENABLE))$(strip $(RGB_MATRIX_INDICATORS_WHEN_DISABLED_ENABLE)),nonono)
        $(error RGB_MATRIX_RENDER_{DOMAIN,INDICATORS_*} require RGB_MATRIX_RENDER_POLICY_ENABLE=yes; with the policy off they are sanitized away rather than applied)
    endif
endif

ifeq ($(strip $(RGB_MATRIX_RENDER_POLICY_ENABLE)), yes)
    OPT_DEFS += -DRGB_MATRIX_RENDER_POLICY_ENABLE
endif
ifeq ($(strip $(RGB_MATRIX_RENDER_DOMAIN_ENABLE)), yes)
    OPT_DEFS += -DRGB_MATRIX_RENDER_DOMAIN_ENABLE
endif
ifeq ($(strip $(RGB_MATRIX_INDICATORS_INDEPENDENT_ENABLE)), yes)
    OPT_DEFS += -DRGB_MATRIX_INDICATORS_INDEPENDENT_ENABLE
endif
ifeq ($(strip $(RGB_MATRIX_INDICATORS_WHEN_DISABLED_ENABLE)), yes)
    OPT_DEFS += -DRGB_MATRIX_INDICATORS_WHEN_DISABLED_ENABLE
endif

# The idle arm's millisecond gate. Independent of the policy and its three
# sub-options -- it gates when the task's own state machine is asked, not what
# the render does -- so it is refused by neither of the blocks above. The C side
# also asks for MCU_RP, because the gate reads the RP2040 raw microsecond
# counter and there is no portable arm; a non-RP2040 board defining this
# compiles the same instructions it did.
ifeq ($(strip $(RGB_MATRIX_IDLE_GATE_ENABLE)), yes)
    OPT_DEFS += -DRGB_MATRIX_IDLE_GATE_ENABLE
endif
