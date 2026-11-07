// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <string.h>
#include "keyboards/era/common/storage/era_settings_reset.h"

static uint8_t storage[ERA_NVM_PHYSICAL_SIZE_BYTES + 2 * ERA_NVM_ERASE_SECTOR_BYTES];
static unsigned erases, reads;
static bool init_failure, erase_failure, read_failure, corrupt_readback;
static uint8_t receipt_page[ERA_INSTALL_RESET_RECEIPT_BYTES];
static unsigned receipt_writes;
static bool receipt_read_failure, receipt_program_failure, receipt_corrupt;

static bool initialize(void *context) {
    (void)context;
    return !init_failure;
}

static bool erase(void *context, uint32_t offset) {
    (void)context;
    assert(offset % ERA_NVM_ERASE_SECTOR_BYTES == 0);
    assert(offset <= ERA_NVM_PHYSICAL_SIZE_BYTES - ERA_NVM_ERASE_SECTOR_BYTES);
    ++erases;
    if (erase_failure) return false;
    memset(storage + ERA_NVM_ERASE_SECTOR_BYTES + offset, 0xFF, ERA_NVM_ERASE_SECTOR_BYTES);
    return true;
}

static bool read_bytes(void *context, uint32_t offset, void *data, size_t length) {
    (void)context;
    assert(length <= ERA_NVM_PHYSICAL_SIZE_BYTES);
    assert(offset <= ERA_NVM_PHYSICAL_SIZE_BYTES - length);
    ++reads;
    if (read_failure) return false;
    memcpy(data, storage + ERA_NVM_ERASE_SECTOR_BYTES + offset, length);
    if (corrupt_readback && offset + length == ERA_NVM_PHYSICAL_SIZE_BYTES) {
        ((uint8_t *)data)[length - 1] = 0;
    }
    return true;
}

static bool read_receipt(void *context, uint8_t *page) {
    (void)context;
    if (receipt_read_failure) return false;
    memcpy(page, receipt_page, sizeof receipt_page);
    return true;
}

static bool program_receipt(void *context, const uint8_t *page) {
    (void)context;
    ++receipt_writes;
    /* A torn program is not an erased page and cannot be repaired in place. */
    size_t length = receipt_program_failure ? 5U : sizeof receipt_page;
    for (size_t index = 0; index < length; ++index) receipt_page[index] &= page[index];
    if (receipt_corrupt) receipt_page[sizeof receipt_page - 1] = 0;
    return !receipt_program_failure;
}

static void fresh(void) {
    memset(storage, 0x5A, sizeof storage);
    erases = reads = 0;
    init_failure = erase_failure = read_failure = corrupt_readback = false;
    memset(receipt_page, 0xFF, sizeof receipt_page);
    receipt_writes = 0;
    receipt_read_failure = receipt_program_failure = receipt_corrupt = false;
}

static void check_preserved_neighbors(void) {
    for (size_t offset = 0; offset < ERA_NVM_ERASE_SECTOR_BYTES; ++offset) {
        assert(storage[offset] == 0x5A);
        assert(storage[ERA_NVM_ERASE_SECTOR_BYTES + ERA_NVM_PHYSICAL_SIZE_BYTES + offset] == 0x5A);
    }
}

int main(void) {
    const era_nvm_flash_t flash = {.init = initialize, .read = read_bytes, .erase_sector = erase};
    const era_settings_reset_receipt_t receipt = {.read = read_receipt, .program = program_receipt};
    fresh();
    for (unsigned execution = 0; execution < 2; ++execution) {
        storage[ERA_NVM_ERASE_SECTOR_BYTES + ERA_NVM_PHYSICAL_SIZE_BYTES / 2] = 0;
        unsigned previous_erases = erases;
        assert(era_settings_reset_flash(&flash));
        assert(erases > previous_erases);
        for (size_t offset = 0; offset < ERA_NVM_PHYSICAL_SIZE_BYTES; ++offset) {
            assert(storage[ERA_NVM_ERASE_SECTOR_BYTES + offset] == 0xFF);
        }
        check_preserved_neighbors();
    }

    fresh();
    init_failure = true;
    assert(!era_settings_reset_flash(&flash) && erases == 0 && reads == 0);
    fresh();
    erase_failure = true;
    assert(!era_settings_reset_flash(&flash) && reads == 0);
    check_preserved_neighbors();
    fresh();
    read_failure = true;
    assert(!era_settings_reset_flash(&flash));
    fresh();
    corrupt_readback = true;
    assert(!era_settings_reset_flash(&flash));
    check_preserved_neighbors();

    fresh();
    assert(era_settings_reset_boot(&flash, &receipt));
    assert(erases == 32 && receipt_writes == 1);
    storage[ERA_NVM_ERASE_SECTOR_BYTES + 100] = 0x31;
    unsigned erased_after_install = erases;
    unsigned read_after_install = reads;
    assert(era_settings_reset_boot(&flash, &receipt));
    assert(erases == erased_after_install && reads == read_after_install && receipt_writes == 1);
    assert(storage[ERA_NVM_ERASE_SECTOR_BYTES + 100] == 0x31);
    /* Reinstall the same bytes: the installer restores only the receipt. */
    memset(receipt_page, 0xFF, sizeof receipt_page);
    assert(era_settings_reset_boot(&flash, &receipt));
    assert(erases == 64 && receipt_writes == 2);
    assert(storage[ERA_NVM_ERASE_SECTOR_BYTES + 100] == 0xFF);
    check_preserved_neighbors();

    fresh();
    receipt_page[sizeof receipt_page - 1] = 0;
    assert(!era_settings_reset_boot(&flash, &receipt));
    assert(erases == 0 && receipt_writes == 0);
    fresh();
    receipt_read_failure = true;
    assert(!era_settings_reset_boot(&flash, &receipt) && erases == 0);

    fresh();
    erase_failure = true;
    assert(!era_settings_reset_boot(&flash, &receipt) && receipt_writes == 0);
    erase_failure = false;
    assert(era_settings_reset_boot(&flash, &receipt) && receipt_writes == 1);

    fresh();
    corrupt_readback = true;
    assert(!era_settings_reset_boot(&flash, &receipt) && receipt_writes == 0);
    fresh();
    receipt_program_failure = true;
    assert(!era_settings_reset_boot(&flash, &receipt));
    erased_after_install = erases;
    receipt_program_failure = false;
    assert(!era_settings_reset_boot(&flash, &receipt));
    assert(erases == erased_after_install && receipt_writes == 1);
    fresh();
    receipt_corrupt = true;
    assert(!era_settings_reset_boot(&flash, &receipt));
    check_preserved_neighbors();
    return 0;
}
