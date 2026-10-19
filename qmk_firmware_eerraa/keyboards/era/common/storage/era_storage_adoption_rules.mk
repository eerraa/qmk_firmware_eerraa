# The ERA replacement-storage adoption bundle: the EEPROM geometry a board
# needs before cross-half EEPROM sync will build on it.
#
# Include this from a board post_rules.mk. There is nothing to configure and
# nothing declarable -- like system/era_sram_resident_rules.mk, the bundle is
# an include rather than a set of variables because its parts fail as a set,
# and because one of them (VIA_EEPROM_MAGIC_ADDR) has a macro for a value and
# cannot be a -D at all.
#
# What this file does is put the header on the force-include list. CONFIG_H is
# assembled at builddefs/build_keyboard.mk:325-379 and read at :537, and
# post_rules.mk runs at :445-449 between the two, so a += here lands after the
# board's own config.h and before the keymap's -- which is exactly the position
# the header's conflict checks need. builddefs/common_rules.mk:272 turns each
# entry into a -include flag, so every translation unit sees it the way it sees
# a config.h.
#
# The refusal that pairs with this lives in split/era_split_qmk_rules.mk: a
# board setting ERA_SPLIT_EEPROM_SYNC_ENABLE=yes without this include is
# declined by name before any compile, rather than failing at the fourth
# _Static_assert inside era_host_peer_storage.c with no statement of what the
# set is.
#
# It is NOT gated on the sync selector, and that is deliberate. The EEPROM
# geometry is a fact about the board's stored data, not about whether the halves
# synchronise it: gating it would give a board's non-VIA keymap -- which has no
# sync and no storage engine -- a different EEPROM layout from its VIA keymap.

CONFIG_H += keyboards/era/common/storage/era_storage_adoption.h

# Production persistence for an adopted ERA store is the ERA NVM engine behind
# QMK's supported custom EEPROM boundary. This is intentionally scoped to the
# storage-adoption bundle: boards that do not own the 24-KiB schema keep their
# existing QMK EEPROM geometry rather than silently acquiring a new layout.
EEPROM_DRIVER = custom
SRC += keyboards/era/common/storage/era_eeprom_driver.c

# Read by split/era_split_qmk_rules.mk. The board includes this file above the
# ERA fragment that checks the marker, the same ordering era_common_qmk_rules.mk
# and era_sram_resident_rules.mk already require of each other.
ERA_STORAGE_ADOPTION_INCLUDED := yes
