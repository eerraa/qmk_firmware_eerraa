# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Deterministic host proof of the agreed-restart state machine. Peer wire facts
# are injected through the production AUTHORITY/RESTART_ARM entry points; the
# TEST platform supplies the clock, and only the reset primitive is wrapped.

SRC += keyboards/era/common/split/era_split_restart_agreement.c
SRC += keyboards/era/common/split/era_split_wire_payload.c
SRC += keyboards/era/common/split/era_split_matrix_frame.c
SRC += keyboards/era/common/split/communication_core/era_split_communication_core_standing.c

INTROSPECTION_KEYMAP_C = keymap.c
OPT_DEFS += -DERA_VIA_SYSTEM_ENABLE -DERA_HOST_PEER_STORAGE_V1_ENABLE
OPT_DEFS += -DERA_SPLIT_RESTART_AGREEMENT_TEST
LDFLAGS += -Wl,--wrap=soft_reset_keyboard

# The restart state machine must see the storage quarantine seam, while the
# host ABI cannot compile the RP2040-only storage publication records pulled in
# by wire_payload.c (their uintptr_t width is deliberately asserted as 32-bit).
# The payload object does not exercise storage classification here, so compile
# that one production unit with only the unrelated storage feature undefined.
$(TEST_OBJ)/$(TEST_OUTPUT)/keyboards/era/common/split/era_split_wire_payload.o: FILE_SPECIFIC_CFLAGS += -UERA_HOST_PEER_STORAGE_V1_ENABLE
$(TEST_OBJ)/$(TEST_OUTPUT)/keyboards/era/common/split/era_split_matrix_frame.o: FILE_SPECIFIC_CFLAGS += -include stddef.h

# standing.c is production code, but its two RP2040 primitives are supplied by
# a deterministic TEST-platform shim: the raw timer register and core/event
# instructions. No production test branch or release state is added.
$(TEST_OBJ)/$(TEST_OUTPUT)/keyboards/era/common/split/communication_core/era_split_communication_core_standing.o: FILE_SPECIFIC_CFLAGS += -include $(TOP_DIR)/tests/era_split_restart_agreement/standing_platform_shim.h
