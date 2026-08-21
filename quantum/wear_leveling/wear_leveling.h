// Copyright 2022 Nick Brassel (@tzarc)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stdlib.h>

/**
 * @typedef Status returned from any wear-leveling API.
 */
typedef enum wear_leveling_status_t {
    WEAR_LEVELING_FAILED,      //< Invocation failed
    WEAR_LEVELING_SUCCESS,     //< Invocation succeeded
    WEAR_LEVELING_CONSOLIDATED //< Invocation succeeded, consolidation occurred
} wear_leveling_status_t;

/**
 * Wear-leveling initialization
 *
 * @return Status of the request
 */
wear_leveling_status_t wear_leveling_init(void);

/**
 * Wear-leveling erasure.
 *
 * Clears the wear-leveling area, with the definition that the "reset state" of all data is zero.
 *
 * @return Status of the request
 */
wear_leveling_status_t wear_leveling_erase(void);

/**
 * Writes logical data into the backing store.
 *
 * Skips writes if there are no changes to written values. The entire written block is considered when attempting to
 * determine if an overwrite should occur -- if there is any data mismatch the entire block will be written to the log,
 * not just the changed bytes.
 *
 * @param address[in] the logical address to write data
 * @param value[in] pointer to the source buffer
 * @param length[in] length of the data
 * @return Status of the request
 */
wear_leveling_status_t wear_leveling_write(uint32_t address, const void* value, size_t length);

/**
 * Reads logical data from the cache.
 *
 * @param address[in] the logical address to read data
 * @param value[out] pointer to the destination buffer
 * @param length[in] length of the data
 * @return Status of the request
 */
wear_leveling_status_t wear_leveling_read(uint32_t address, void* value, size_t length);

#if defined(ERA_SRAM_RESIDENT_IMAGE)
/**
 * ERA: hand control back from inside a backing-store erase.
 *
 * Called by a backing store that erases its area in pieces, once between each
 * pair of them. It raises the interlock that makes the cache the only writable
 * copy of logical data for the width of the call (wear_leveling.c), then runs
 * backing_store_erase_yield_kb().
 *
 * The hook is weak and does nothing by default, so a backing store may slice
 * its erase without a board supplying a pass to run in the gaps - and a build
 * in that state is the one the slice counters exist to tell apart from a fixed
 * one.
 */
void wear_leveling_backing_erase_yield(void);
void backing_store_erase_yield_kb(void);

/**
 * ERA: must this backing-store operation be refused because a gap is open?
 *
 * The gap's contract is that nothing it runs may reach the backing store, and
 * the interlock above enforces the reachable half of that by making a nested
 * `wear_leveling_write()` cache-only. This is the backstop for the other half:
 * anything that reaches a backing-store entry point without passing through
 * that interlock. Weak and false by default.
 *
 * Refusing is the safe side **here** and is not the safe side one layer up.
 * At `wear_leveling_write()` a refusal would drop a user's edit, which is why
 * the cache-only path exists instead. Past it, a caller is about to program or
 * erase a store that is half-erased, so there is no lossless option left and
 * proceeding costs the whole store at the next boot's checksum.
 *
 * By construction it never fires, which is exactly why the count is published
 * rather than the condition asserted: a check nobody can read is a rule again.
 */
bool backing_store_commit_blocked_kb(void);
#endif
