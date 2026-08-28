// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Hardware-free half of replacement Apply. The caller owns policy and
 * publication; this header owns only the bounded slice swap that keeps one
 * complete old image available while the raw store is becoming the candidate.
 * It is header-only so the host proof below exercises the exact operations the
 * firmware inlines without creating another storage translation unit. */
#define ERA_STORAGE_SLICE_SWAP_MAX_SLICE_BYTES 32U

typedef bool (*era_storage_slice_swap_read_fn)(uint32_t address, void *data, uint16_t length, void *context);
typedef bool (*era_storage_slice_swap_write_fn)(uint32_t address, const void *data, uint16_t length, void *context);

typedef enum {
    ERA_STORAGE_SLICE_SWAP_IDLE = 0,
    ERA_STORAGE_SLICE_SWAP_WRITE,
    /* Final successful slice has returned to the keyboard once; raw verify
     * and publication ordering run on the following task entry. */
    ERA_STORAGE_SLICE_SWAP_VERIFY,
    ERA_STORAGE_SLICE_SWAP_ROLLBACK,
    ERA_STORAGE_SLICE_SWAP_REPAIR_REQUIRED,
} era_storage_slice_swap_phase_t;

typedef enum {
    ERA_STORAGE_SLICE_SWAP_PROGRESS = 0,
    ERA_STORAGE_SLICE_SWAP_COMPLETE,
    ERA_STORAGE_SLICE_SWAP_READ_FAILED,
    ERA_STORAGE_SLICE_SWAP_WRITE_FAILED,
    ERA_STORAGE_SLICE_SWAP_VERIFY_FAILED,
    ERA_STORAGE_SLICE_SWAP_INVALID,
} era_storage_slice_swap_result_t;

typedef struct {
    uint16_t written_prefix;
    uint16_t protected_offset;
    uint8_t  protected_length;
    uint8_t  phase;
    uint8_t  scratch[ERA_STORAGE_SLICE_SWAP_MAX_SLICE_BYTES];
} era_storage_slice_swap_t;

static inline bool era_storage_slice_swap_facade_active(const era_storage_slice_swap_t *swap) {
    return swap != NULL && swap->phase != ERA_STORAGE_SLICE_SWAP_IDLE;
}

static inline void era_storage_slice_swap_begin(era_storage_slice_swap_t *swap) {
    if (swap == NULL) {
        return;
    }
    memset(swap, 0, sizeof(*swap));
    swap->phase = ERA_STORAGE_SLICE_SWAP_WRITE;
}

static inline void era_storage_slice_swap_request_rollback(era_storage_slice_swap_t *swap) {
    if (swap != NULL && swap->phase != ERA_STORAGE_SLICE_SWAP_IDLE) {
        swap->phase = ERA_STORAGE_SLICE_SWAP_ROLLBACK;
    }
}

static inline uint32_t era_storage_slice_swap_min_u32(uint32_t left, uint32_t right) {
    return left < right ? left : right;
}

static inline uint32_t era_storage_slice_swap_max_u32(uint32_t left, uint32_t right) {
    return left > right ? left : right;
}

static inline void era_storage_slice_swap_overlay(uint8_t *target, uint32_t request_address, uint16_t request_length, uint32_t source_address, const uint8_t *source, uint16_t source_length) {
    uint32_t request_end = request_address + request_length;
    uint32_t source_end  = source_address + source_length;
    uint32_t start       = era_storage_slice_swap_max_u32(request_address, source_address);
    uint32_t end         = era_storage_slice_swap_min_u32(request_end, source_end);
    if (start < end) {
        memcpy(&target[start - request_address], &source[start - source_address], end - start);
    }
}

/* Returns true only when the Apply facade handled the request. The raw read is
 * taken first, then the two places that still own old bytes are overlaid: the
 * swapped prefix in staging and, after a possibly partial failed write, the
 * current slice scratch. */
static inline bool era_storage_slice_swap_public_read(const era_storage_slice_swap_t *swap, const uint8_t *staged, uint32_t image_address, uint16_t image_size, uint32_t address, void *data, uint16_t length, era_storage_slice_swap_read_fn raw_read, void *context) {
    if (!era_storage_slice_swap_facade_active(swap) || staged == NULL || data == NULL || length == 0 || raw_read == NULL) {
        return false;
    }
    uint32_t image_end   = image_address + image_size;
    uint32_t request_end = address + length;
    if (request_end <= image_address || image_end <= address) {
        return false;
    }
    if (!raw_read(address, data, length, context)) {
        /* The EEPROM API has no result channel. Zero is fail-closed here: it
         * cannot expose a candidate prefix as old data after a raw-read fault. */
        memset(data, 0, length);
        return true;
    }

    era_storage_slice_swap_overlay((uint8_t *)data, address, length, image_address, staged, swap->written_prefix);
    if (swap->protected_length != 0) {
        era_storage_slice_swap_overlay((uint8_t *)data, address, length, image_address + swap->protected_offset, swap->scratch, swap->protected_length);
    }
    return true;
}

/* One candidate slice: save raw old, attempt a checked raw write, and only
 * then replace the staged candidate slice with old and advance the prefix. */
static inline era_storage_slice_swap_result_t era_storage_slice_swap_write_next(era_storage_slice_swap_t *swap, uint8_t *staged, uint32_t image_address, uint16_t image_size, uint16_t slice_size, era_storage_slice_swap_read_fn raw_read, era_storage_slice_swap_write_fn raw_write, void *context) {
    if (swap == NULL || staged == NULL || raw_read == NULL || raw_write == NULL || swap->phase != ERA_STORAGE_SLICE_SWAP_WRITE || slice_size == 0 || slice_size > ERA_STORAGE_SLICE_SWAP_MAX_SLICE_BYTES || swap->written_prefix > image_size) {
        return ERA_STORAGE_SLICE_SWAP_INVALID;
    }
    if (swap->written_prefix == image_size) {
        return ERA_STORAGE_SLICE_SWAP_COMPLETE;
    }

    uint16_t offset    = swap->written_prefix;
    uint16_t remaining = (uint16_t)(image_size - offset);
    uint16_t length    = remaining < slice_size ? remaining : slice_size;
    if (!raw_read(image_address + offset, swap->scratch, length, context)) {
        swap->phase = ERA_STORAGE_SLICE_SWAP_ROLLBACK;
        return ERA_STORAGE_SLICE_SWAP_READ_FAILED;
    }
    swap->protected_offset = offset;
    swap->protected_length = (uint8_t)length;
    if (!raw_write(image_address + offset, &staged[offset], length, context)) {
        swap->phase = ERA_STORAGE_SLICE_SWAP_ROLLBACK;
        return ERA_STORAGE_SLICE_SWAP_WRITE_FAILED;
    }

    memcpy(&staged[offset], swap->scratch, length);
    swap->written_prefix   = (uint16_t)(offset + length);
    swap->protected_length = 0;
    if (swap->written_prefix == image_size) {
        swap->phase = ERA_STORAGE_SLICE_SWAP_VERIFY;
        return ERA_STORAGE_SLICE_SWAP_COMPLETE;
    }
    return ERA_STORAGE_SLICE_SWAP_PROGRESS;
}

static inline bool era_storage_slice_swap_raw_matches_bytewise(uint32_t address, const uint8_t *expected, uint16_t length, era_storage_slice_swap_read_fn raw_read, void *context) {
    for (uint16_t index = 0; index < length; index++) {
        uint8_t actual = 0;
        if (!raw_read(address + index, &actual, 1, context) || actual != expected[index]) {
            return false;
        }
    }
    return true;
}

/* One rollback slice, verified before the protected prefix retreats. A failed
 * candidate write's current slice is restored first from scratch; successfully
 * swapped slices then unwind from staging in reverse order. */
static inline era_storage_slice_swap_result_t era_storage_slice_swap_rollback_next(era_storage_slice_swap_t *swap, const uint8_t *staged, uint32_t image_address, uint16_t slice_size, era_storage_slice_swap_read_fn raw_read, era_storage_slice_swap_write_fn raw_write, void *context) {
    if (swap == NULL || staged == NULL || raw_read == NULL || raw_write == NULL || slice_size == 0 || slice_size > ERA_STORAGE_SLICE_SWAP_MAX_SLICE_BYTES || (swap->phase != ERA_STORAGE_SLICE_SWAP_ROLLBACK && swap->phase != ERA_STORAGE_SLICE_SWAP_REPAIR_REQUIRED)) {
        return ERA_STORAGE_SLICE_SWAP_INVALID;
    }
    swap->phase = ERA_STORAGE_SLICE_SWAP_ROLLBACK;

    if (swap->protected_length != 0) {
        uint32_t address = image_address + swap->protected_offset;
        uint8_t  length  = swap->protected_length;
        if (!raw_write(address, swap->scratch, length, context)) {
            swap->phase = ERA_STORAGE_SLICE_SWAP_REPAIR_REQUIRED;
            return ERA_STORAGE_SLICE_SWAP_WRITE_FAILED;
        }
        if (!era_storage_slice_swap_raw_matches_bytewise(address, swap->scratch, length, raw_read, context)) {
            swap->phase = ERA_STORAGE_SLICE_SWAP_REPAIR_REQUIRED;
            return ERA_STORAGE_SLICE_SWAP_VERIFY_FAILED;
        }
        swap->protected_length = 0;
        if (swap->written_prefix == 0) {
            swap->phase = ERA_STORAGE_SLICE_SWAP_IDLE;
            return ERA_STORAGE_SLICE_SWAP_COMPLETE;
        }
        return ERA_STORAGE_SLICE_SWAP_PROGRESS;
    }

    if (swap->written_prefix == 0) {
        swap->phase = ERA_STORAGE_SLICE_SWAP_IDLE;
        return ERA_STORAGE_SLICE_SWAP_COMPLETE;
    }

    uint16_t length = swap->written_prefix < slice_size ? swap->written_prefix : slice_size;
    uint16_t offset = (uint16_t)(swap->written_prefix - length);
    if (!raw_write(image_address + offset, &staged[offset], length, context)) {
        swap->phase = ERA_STORAGE_SLICE_SWAP_REPAIR_REQUIRED;
        return ERA_STORAGE_SLICE_SWAP_WRITE_FAILED;
    }
    if (!raw_read(image_address + offset, swap->scratch, length, context) || memcmp(swap->scratch, &staged[offset], length) != 0) {
        swap->phase = ERA_STORAGE_SLICE_SWAP_REPAIR_REQUIRED;
        return ERA_STORAGE_SLICE_SWAP_VERIFY_FAILED;
    }
    swap->written_prefix = offset;
    if (offset == 0) {
        swap->phase = ERA_STORAGE_SLICE_SWAP_IDLE;
        return ERA_STORAGE_SLICE_SWAP_COMPLETE;
    }
    return ERA_STORAGE_SLICE_SWAP_PROGRESS;
}

/* Preserve a local mutation that landed while Apply was open. Only bytes
 * whose old copy has moved out of raw need absorbing; untouched suffix bytes
 * already remain raw and therefore already form part of the public old view. */
static inline bool era_storage_slice_swap_absorb_raw_write(era_storage_slice_swap_t *swap, uint8_t *staged, uint32_t image_address, uint16_t image_size, uint32_t address, uint16_t length, era_storage_slice_swap_read_fn raw_read, void *context) {
    if (!era_storage_slice_swap_facade_active(swap) || staged == NULL || raw_read == NULL || length == 0) {
        return true;
    }
    uint32_t request_end = address + length;
    uint32_t prefix_end  = image_address + swap->written_prefix;
    uint32_t start       = era_storage_slice_swap_max_u32(address, image_address);
    uint32_t end         = era_storage_slice_swap_min_u32(request_end, prefix_end);
    if (start < end && !raw_read(start, &staged[start - image_address], (uint16_t)(end - start), context)) {
        return false;
    }

    if (swap->protected_length != 0) {
        uint32_t protected_address = image_address + swap->protected_offset;
        uint32_t protected_end     = protected_address + swap->protected_length;
        start                      = era_storage_slice_swap_max_u32(address, protected_address);
        end                        = era_storage_slice_swap_min_u32(request_end, protected_end);
        if (start < end && !raw_read(start, &swap->scratch[start - protected_address], (uint16_t)(end - start), context)) {
            return false;
        }
    }
    (void)image_size;
    return true;
}

/* The caller performs raw verification, runtime reload, and any fallible
 * publication preflight first. This is the single public old -> new flip. */
static inline bool era_storage_slice_swap_publish(era_storage_slice_swap_t *swap, uint16_t image_size) {
    if (swap == NULL || swap->phase != ERA_STORAGE_SLICE_SWAP_VERIFY || swap->protected_length != 0 || swap->written_prefix != image_size) {
        return false;
    }
    swap->phase = ERA_STORAGE_SLICE_SWAP_IDLE;
    return true;
}
