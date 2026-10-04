# SPDX-License-Identifier: GPL-2.0-or-later
SRC += quantum/era_macro.c quantum/dynamic_keymap.c
SRC += platforms/chibios/suspend.c
VPATH += tests/era_macro/hal_stubs
SRC += keyboards/era/common/system/era_state_sync.c
TAP_DANCE_ENABLE = yes
MOUSEKEY_ENABLE = yes
EXTRAKEY_ENABLE = yes
INTROSPECTION_KEYMAP_C = tap_dance_defs.c
OPT_DEFS += -DERA_MACRO_ENABLE -DERA_TAP_DANCE_ENABLE
# The fixture supplies the matrix keymap; production dynamic macro dispatch
# and storage API still compile and execute unchanged.
$(TEST_OBJ)/$(TEST_OUTPUT)/quantum/dynamic_keymap.o: FILE_SPECIFIC_CFLAGS += -Dkeycode_at_keymap_location=macro_fixture_keycode_at_keymap_location
