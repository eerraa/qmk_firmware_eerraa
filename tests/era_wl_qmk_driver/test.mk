# Production QMK macro -> EEPROM adapter -> wear-leveling, physical flash mock only.
EEPROM_DRIVER = custom
SRC += drivers/eeprom/eeprom_wear_leveling.c
SRC += quantum/wear_leveling/wear_leveling.c quantum/wear_leveling/tests/backing_mocks.cpp
SRC += lib/fnv/qmk_fnv_type_validation.c lib/fnv/hash_32a.c lib/fnv/hash_64a.c
SRC += quantum/nvm/eeprom/nvm_dynamic_keymap.c
OPT_DEFS += -Iquantum/wear_leveling -Ilib/fnv
OPT_DEFS += -DWEAR_LEVELING_TESTS -DBACKING_STORE_WRITE_SIZE=2 -DWEAR_LEVELING_BACKING_SIZE=8192 -DWEAR_LEVELING_LOGICAL_SIZE=4096
OPT_DEFS += -DEEPROM_SIZE=4096 -DDYNAMIC_KEYMAP_LAYER_COUNT=6 -DDYNAMIC_KEYMAP_EEPROM_ADDR=321
OPT_DEFS += -DDYNAMIC_KEYMAP_MACRO_EEPROM_ADDR=801 -DDYNAMIC_KEYMAP_MACRO_EEPROM_SIZE=3295
# This test selects a custom build adapter to supply physical stubs, but executes
# the non-custom production macro branch used by native/WL boards.
OPT_DEFS += -UEEPROM_CUSTOM

INTROSPECTION_KEYMAP_C = keymap.c
SRC += quantum/via.c quantum/dynamic_keymap.c quantum/nvm/eeprom/nvm_via.c
SRC += keyboards/era/common/system/era_state_sync.c
OPT_DEFS += -DVIA_ENABLE -DVIA_EEPROM_MAGIC_ADDR=317 -DRAW_ENABLE -DDYNAMIC_KEYMAP_ENABLE -DERA_STATE_SYNC_TEST
