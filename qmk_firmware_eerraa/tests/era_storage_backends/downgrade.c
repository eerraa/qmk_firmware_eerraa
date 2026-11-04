// SPDX-License-Identifier: GPL-2.0-or-later
// Only the NOR device is modeled. Both storage parsers and writers are production.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "wear_leveling.h"
#include "wear_leveling_internal.h"
#include "keyboards/era/common/storage/era_nvm.h"
#include "keyboards/era/common/storage/era_eeprom_layout.h"
#include "keyboards/era/common/storage/era_via_magic.h"

static uint8_t flash[2 * 1024 * 1024];
static uint32_t physical_base;
static era_nvm_t nvm;
enum {
    WL_BASE = sizeof(flash) - WEAR_LEVELING_BACKING_SIZE,
    OLD_ERA_BASE = sizeof(flash) - ERA_NVM_PHYSICAL_SIZE_BYTES,
    LEGACY_BASE = sizeof(flash) - 136 * 1024,
};

bool backing_store_init(void) { return true; }
bool backing_store_unlock(void) { return true; }
bool backing_store_lock(void) { return true; }
bool backing_store_erase(void) {
    memset(flash + WL_BASE, 0xff, WEAR_LEVELING_BACKING_SIZE);
    return true;
}
bool backing_store_read(uint32_t address, backing_store_int_t *value) {
    assert(address + sizeof(*value) <= WEAR_LEVELING_BACKING_SIZE);
    memcpy(value, flash + WL_BASE + address, sizeof(*value));
    *value = (backing_store_int_t)~*value;
    return true;
}
bool backing_store_write(uint32_t address, backing_store_int_t value) {
    assert(address + sizeof(value) <= WEAR_LEVELING_BACKING_SIZE);
    value = (backing_store_int_t)~value;
    const uint8_t *bytes = (const uint8_t *)&value;
    for (size_t i = 0; i < sizeof(value); ++i) flash[WL_BASE + address + i] &= bytes[i];
    return memcmp(flash + WL_BASE + address, &value, sizeof(value)) == 0;
}

static bool nor_read(void *context, uint32_t offset, void *data, size_t size) {
    (void)context;
    assert(offset + size <= ERA_NVM_PHYSICAL_SIZE_BYTES);
    memcpy(data, flash + physical_base + offset, size);
    return true;
}
static bool nor_program(void *context, uint32_t offset, const void *data, size_t size) {
    (void)context;
    assert(size && offset + size <= ERA_NVM_PHYSICAL_SIZE_BYTES);
    assert(offset / 256 == (offset + size - 1) / 256);
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        assert((flash[physical_base + offset + i] & bytes[i]) == bytes[i]);
        flash[physical_base + offset + i] &= bytes[i];
    }
    return true;
}
static bool nor_erase(void *context, uint32_t offset) {
    (void)context;
    assert(offset % 4096 == 0 && offset + 4096 <= ERA_NVM_PHYSICAL_SIZE_BYTES);
    memset(flash + physical_base + offset, 0xff, 4096);
    return true;
}
static void mount(uint32_t base) {
    assert(base + ERA_NVM_PHYSICAL_SIZE_BYTES <= sizeof(flash));
    physical_base = base;
    era_nvm_flash_t device = {.read = nor_read, .program = nor_program, .erase_sector = nor_erase};
    era_nvm_config_t config = {.macro_address = 1000, .macro_size = 16384};
    era_nvm_setup(&nvm, &device, &config);
    assert(era_nvm_mount(&nvm) == ERA_NVM_RESULT_OK);
}

static void seed_old_logical(uint8_t *image, uint32_t reset_key) {
    memset(image, 0, WEAR_LEVELING_LOGICAL_SIZE);
    uint16_t magic = EECONFIG_MAGIC_NUMBER;
    memcpy(image, &magic, sizeof(magic));
    uint32_t guard[4] = {reset_key, ~reset_key, 0, 0};
    memcpy(image + ERA_EEPROM_CONFIG_ADDR + ERA_EEPROM_RESET_GUARD_CONFIG_OFFSET, guard, sizeof(guard));
    const char *date = QMK_BUILDDATE;
    image[ERA_EEPROM_CONFIG_END] = ((date[2] & 15) << 4) | (date[3] & 15);
    image[ERA_EEPROM_CONFIG_END + 1] = ((date[5] & 15) << 4) | (date[6] & 15);
    image[ERA_EEPROM_CONFIG_END + 2] = ((date[8] & 15) << 4) | (date[9] & 15);
    image[3] = 2; // A non-default layer preference, not a parser sentinel.
}

static uint32_t upload_crc(const uint8_t *data, size_t length) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
    }
    return ~crc;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    bool tomak = strcmp(argv[1], "tomak") == 0;
    uint8_t old[WEAR_LEVELING_LOGICAL_SIZE], recovered[WEAR_LEVELING_LOGICAL_SIZE];
    uint8_t encoded[5000], macro[ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES];
    static uint8_t legacy[136 * 1024], latest[ERA_NVM_PHYSICAL_SIZE_BYTES];
    memset(flash, 0xff, sizeof(flash));
    seed_old_logical(old, ERA_EEPROM_RESET_KEY);
    assert(wear_leveling_init() != WEAR_LEVELING_FAILED);
    assert(wear_leveling_stage(0, old, sizeof(old)));
    assert(wear_leveling_write(0, old, sizeof(old)) == WEAR_LEVELING_CONSOLIDATED);
    memcpy(encoded, flash + WL_BASE, sizeof(encoded));
    if (tomak) {
        mount(OLD_ERA_BASE);
        assert(era_nvm_replace(&nvm, 0, old, sizeof(old), ERA_NVM_ORIGIN_LOCAL_QMK) == ERA_NVM_RESULT_OK);
    }
    memcpy(legacy, flash + LEGACY_BASE, sizeof(legacy));

    mount(NVM_BASE_OFFSET);
    assert(era_nvm_read(&nvm, 0, recovered, sizeof(recovered)) == ERA_NVM_RESULT_OK);
    for (size_t i = 0; i < sizeof(recovered); ++i) assert(recovered[i] == 0);
    assert(era_nvm_format(&nvm) == ERA_NVM_RESULT_OK);
    // Reach bank B's tail with normal local writes and an actual macro upload.
    // The payload deliberately includes a valid WL image, the original alias
    // counterexample; isolation must not depend on parsing or rejecting it.
    for (unsigned i = 0; i < 424; ++i) {
        uint8_t value = (i & 1) ? 2 : 1;
        assert(era_nvm_replace(&nvm, 100, &value, 1, ERA_NVM_ORIGIN_LOCAL_QMK) == ERA_NVM_RESULT_OK);
    }
    uint32_t payload = NVM_BASE_OFFSET + ERA_NVM_BANK_SIZE_BYTES + nvm.journal_cursor + sizeof(era_nvm_record_header_t);
    uint32_t bank_tail = NVM_BASE_OFFSET + ERA_NVM_PHYSICAL_SIZE_BYTES - WEAR_LEVELING_BACKING_SIZE;
    assert(payload < bank_tail && bank_tail + sizeof(encoded) <= payload + sizeof(macro));
    memset(macro, 0xff, sizeof(macro));
    memcpy(macro + bank_tail - payload, encoded, sizeof(encoded));
    assert(era_nvm_macro_replace_begin(&nvm, sizeof(macro) - 1) == ERA_NVM_RESULT_OK);
    assert(era_nvm_macro_replace_data(&nvm, 0, macro, sizeof(macro) - 1) == ERA_NVM_RESULT_STAGED);
    assert(era_nvm_macro_replace_commit(&nvm, upload_crc(macro, sizeof(macro) - 1)) == ERA_NVM_RESULT_OK);
    macro[sizeof(macro) - 1] = 0;
    mount(NVM_BASE_OFFSET);
    uint8_t replayed[sizeof(macro)];
    assert(era_nvm_read(&nvm, 1000, replayed, sizeof(replayed)) == ERA_NVM_RESULT_OK);
    assert(memcmp(replayed, macro, sizeof(macro)) == 0);
    assert(memcmp(legacy, flash + LEGACY_BASE, sizeof(legacy)) == 0);
    memcpy(latest, flash + NVM_BASE_OFFSET, sizeof(latest));

    if (tomak) {
        mount(OLD_ERA_BASE);
        assert(era_nvm_read(&nvm, 0, recovered, sizeof(recovered)) == ERA_NVM_RESULT_OK);
    } else {
        assert(wear_leveling_init() != WEAR_LEVELING_FAILED);
        assert(wear_leveling_read(0, recovered, sizeof(recovered)) != WEAR_LEVELING_FAILED);
    }
    assert(memcmp(recovered, old, sizeof(old)) == 0);
    old[3] = 4;
    if (tomak) {
        assert(era_nvm_replace(&nvm, 3, old + 3, 1, ERA_NVM_ORIGIN_LOCAL_QMK) == ERA_NVM_RESULT_OK);
    } else {
        assert(wear_leveling_stage(0, old, sizeof(old)));
        assert(wear_leveling_write(0, old, sizeof(old)) == WEAR_LEVELING_CONSOLIDATED);
    }
    assert(memcmp(latest, flash + NVM_BASE_OFFSET, sizeof(latest)) == 0);
    mount(NVM_BASE_OFFSET);
    assert(era_nvm_read(&nvm, 1000, replayed, sizeof(replayed)) == ERA_NVM_RESULT_OK);
    assert(memcmp(replayed, macro, sizeof(macro)) == 0);
    printf("PASS %s: first upgrade is empty; macro replacement preserves legacy store; downgrade edits preserve re-upgrade state\n", argv[1]);
    return 0;
}
