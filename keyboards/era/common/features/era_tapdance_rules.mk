# Every ERA option this file reads is declared in keyboards/era/era_build_options.mk,
# which is the one file to edit. Included here rather than by the board so this
# fragment cannot run without its declarations; a board or a profile assigning
# above the include that reads it still wins, because every line there is `?=`.
include keyboards/era/era_build_options.mk


ifeq ($(strip $(TAP_DANCE_ENABLE)), yes)
    INTROSPECTION_KEYMAP_C = keyboards/era/common/features/era_tapdance_introspection.c
    SRC += keyboards/era/common/features/era_tapdance.c
    # The runtime and the introspection table are what make the keycodes work
    # and are not VIA's; only the adapter is. A non-VIA build keeps eight
    # working tap-dance slots on their compiled-in defaults, which is exactly
    # what it had before -- the difference is that it no longer links the
    # adapter that has no command to answer.
    ifeq ($(strip $(VIA_ENABLE)), yes)
        SRC += keyboards/era/common/features/era_tapdance_via.c
    endif
    # The ERA tap-dance surface is derived from QMK's switch, not chosen beside
    # it, which is what the five board headers used to say with
    # `#if defined(TAP_DANCE_ENABLE)`. Emitting it here instead makes it visible
    # to every translation unit rather than only to one that reaches
    # QMK_KEYBOARD_H, which is the difference that compiled this feature's whole
    # VIA route away once. Every consumer tests it with #ifdef / #if defined and
    # none with `#if X`, so arriving as a -D valued 1 rather than an empty
    # header #define changes nothing -- checked before the move, because that is
    # exactly where this substitution goes wrong silently.
    OPT_DEFS += -DERA_TAP_DANCE_ENABLE
endif
