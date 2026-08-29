// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_nvm.h"

#include <string.h>

#define ERA_NVM_CRC32_INITIAL 0xFFFFFFFFUL
#define ERA_NVM_CRC32_POLYNOMIAL 0xEDB88320UL
#define ERA_NVM_ERASED_BYTE 0xFFU
#define ERA_NVM_LOGICAL_ERASED_BYTE 0x00U
#define ERA_NVM_QMK_MACRO_RESET_CHUNK_BYTES 16U

#define ERA_NVM_SECTORS_PER_BANK (ERA_NVM_BANK_SIZE_BYTES / ERA_NVM_ERASE_SECTOR_BYTES)

typedef struct {
    bool     valid;
    bool     io_error;
    uint32_t generation;
    uint32_t snapshot_crc32;
} era_nvm_bank_info_t;

typedef struct {
    uint32_t cursor;
    uint32_t next_sequence;
    bool     sealed;
} era_nvm_replay_info_t;

typedef enum {
    ERA_NVM_SOURCE_BUFFER = 0,
    ERA_NVM_SOURCE_IMAGE,
    ERA_NVM_SOURCE_ZERO,
} era_nvm_source_kind_t;

typedef struct {
    era_nvm_source_kind_t kind;
    const uint8_t        *buffer;
    const era_nvm_t      *nvm;
    uint32_t              image_address;
    uint32_t              length;
    bool                  override_enabled;
    uint32_t              override_offset;
    uint8_t               override_value;
} era_nvm_source_t;

_Static_assert(ERA_NVM_BANK_JOURNAL_BYTES > (sizeof(era_nvm_record_header_t) + ERA_NVM_LOGICAL_SIZE_BYTES + sizeof(era_nvm_record_trailer_t) + 3U),
               "ERA NVM journal must fit one full logical-image replacement record");

static uint32_t era_nvm_min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static uint32_t era_nvm_align4(uint32_t value) {
    return (value + 3U) & ~3U;
}

static bool era_nvm_range_valid(uint32_t address, size_t length, uint32_t limit) {
    if (length > UINT32_MAX) {
        return false;
    }
    uint32_t length32 = (uint32_t)length;
    return address <= limit && length32 <= limit - address;
}

static bool era_nvm_bytes_are(const uint8_t *bytes, size_t length, uint8_t value) {
    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] != value) {
            return false;
        }
    }
    return true;
}

static uint32_t era_nvm_crc32_update(uint32_t crc, const void *data, size_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc           = (crc >> 1U) ^ (ERA_NVM_CRC32_POLYNOMIAL & mask);
        }
    }
    return crc;
}

static uint32_t era_nvm_crc32_finish(uint32_t crc) {
    return crc ^ ERA_NVM_CRC32_INITIAL;
}

static uint32_t era_nvm_crc32(const void *data, size_t length) {
    return era_nvm_crc32_finish(era_nvm_crc32_update(ERA_NVM_CRC32_INITIAL, data, length));
}

static uint32_t era_nvm_bank_base(uint8_t bank) {
    return (uint32_t)bank * ERA_NVM_BANK_SIZE_BYTES;
}

static uint8_t era_nvm_inactive_bank(const era_nvm_t *nvm) {
    return nvm->active_bank == 0U ? 1U : 0U;
}

static bool era_nvm_flash_read(const era_nvm_t *nvm, uint32_t offset, void *data, size_t length) {
    if (length == 0U) {
        return true;
    }
    if (nvm == NULL || nvm->flash.read == NULL || data == NULL || !era_nvm_range_valid(offset, length, ERA_NVM_PHYSICAL_SIZE_BYTES)) {
        return false;
    }
    return nvm->flash.read(nvm->flash.context, offset, data, length);
}

static bool era_nvm_flash_program_verified(era_nvm_t *nvm, uint32_t offset, const void *data, size_t length) {
    if (length == 0U) {
        return true;
    }
    if (nvm == NULL || nvm->flash.program == NULL || data == NULL || !era_nvm_range_valid(offset, length, ERA_NVM_PHYSICAL_SIZE_BYTES)) {
        return false;
    }

    const uint8_t *source = (const uint8_t *)data;
    uint8_t        verify[ERA_NVM_PROGRAM_PAGE_BYTES];
    while (length > 0U) {
        uint32_t page_room = ERA_NVM_PROGRAM_PAGE_BYTES - (offset % ERA_NVM_PROGRAM_PAGE_BYTES);
        size_t   chunk     = length < page_room ? length : page_room;
        nvm->program_count++;
        if (!nvm->flash.program(nvm->flash.context, offset, source, chunk)) {
            nvm->program_failure_count++;
            return false;
        }
        if (!era_nvm_flash_read(nvm, offset, verify, chunk) || memcmp(verify, source, chunk) != 0) {
            nvm->program_failure_count++;
            return false;
        }
        offset += (uint32_t)chunk;
        source += chunk;
        length -= chunk;
    }
    return true;
}

static bool era_nvm_flash_range_erased(const era_nvm_t *nvm, uint32_t offset, uint32_t length) {
    uint8_t scratch[ERA_NVM_PROGRAM_PAGE_BYTES];
    while (length > 0U) {
        uint32_t chunk = era_nvm_min_u32(length, sizeof(scratch));
        if (!era_nvm_flash_read(nvm, offset, scratch, chunk) || !era_nvm_bytes_are(scratch, chunk, ERA_NVM_ERASED_BYTE)) {
            return false;
        }
        offset += chunk;
        length -= chunk;
    }
    return true;
}

static bool era_nvm_flash_erase_verified(era_nvm_t *nvm, uint32_t offset) {
    if (nvm == NULL || nvm->flash.erase_sector == NULL || offset % ERA_NVM_ERASE_SECTOR_BYTES != 0U ||
        offset > ERA_NVM_PHYSICAL_SIZE_BYTES - ERA_NVM_ERASE_SECTOR_BYTES) {
        return false;
    }
    nvm->erase_count++;
    if (!nvm->flash.erase_sector(nvm->flash.context, offset)) {
        nvm->erase_failure_count++;
        return false;
    }
    if (!era_nvm_flash_range_erased(nvm, offset, ERA_NVM_ERASE_SECTOR_BYTES)) {
        nvm->erase_failure_count++;
        return false;
    }
    return true;
}

static void era_nvm_source_copy(const era_nvm_source_t *source, uint32_t offset, uint8_t *target, uint32_t length) {
    switch (source->kind) {
        case ERA_NVM_SOURCE_BUFFER:
            memcpy(target, source->buffer + offset, length);
            break;
        case ERA_NVM_SOURCE_IMAGE:
            memcpy(target, source->nvm->image + source->image_address + offset, length);
            break;
        case ERA_NVM_SOURCE_ZERO:
            memset(target, 0, length);
            break;
    }

    if (source->override_enabled && source->override_offset >= offset && source->override_offset - offset < length) {
        target[source->override_offset - offset] = source->override_value;
    }
}

static uint32_t era_nvm_source_crc32(const era_nvm_source_t *source) {
    uint8_t  scratch[ERA_NVM_PROGRAM_PAGE_BYTES];
    uint32_t crc    = ERA_NVM_CRC32_INITIAL;
    uint32_t offset = 0U;
    while (offset < source->length) {
        uint32_t chunk = era_nvm_min_u32(source->length - offset, sizeof(scratch));
        era_nvm_source_copy(source, offset, scratch, chunk);
        crc = era_nvm_crc32_update(crc, scratch, chunk);
        offset += chunk;
    }
    return era_nvm_crc32_finish(crc);
}

static bool era_nvm_source_matches_image(const era_nvm_t *nvm, uint32_t address, const era_nvm_source_t *source) {
    uint8_t  scratch[ERA_NVM_PROGRAM_PAGE_BYTES];
    uint32_t offset = 0U;
    while (offset < source->length) {
        uint32_t chunk = era_nvm_min_u32(source->length - offset, sizeof(scratch));
        era_nvm_source_copy(source, offset, scratch, chunk);
        if (memcmp(nvm->image + address + offset, scratch, chunk) != 0) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

static void era_nvm_publish_source(era_nvm_t *nvm, uint32_t address, const era_nvm_source_t *source) {
    uint8_t  scratch[ERA_NVM_PROGRAM_PAGE_BYTES];
    uint32_t offset = 0U;
    while (offset < source->length) {
        uint32_t chunk = era_nvm_min_u32(source->length - offset, sizeof(scratch));
        era_nvm_source_copy(source, offset, scratch, chunk);
        memcpy(nvm->image + address + offset, scratch, chunk);
        offset += chunk;
    }
}

static void era_nvm_notify(const era_nvm_t *nvm, uint32_t address, uint32_t length, era_nvm_origin_t origin) {
    if (nvm->config.commit_notifier != NULL) {
        nvm->config.commit_notifier(nvm->config.commit_context, address, length, origin);
    }
}

static uint32_t era_nvm_record_size(uint32_t payload_length) {
    uint32_t trailer_offset = era_nvm_align4((uint32_t)sizeof(era_nvm_record_header_t) + payload_length);
    return trailer_offset + (uint32_t)sizeof(era_nvm_record_trailer_t);
}

static uint32_t era_nvm_record_trailer_offset(uint32_t record_cursor, uint32_t payload_length) {
    return record_cursor + era_nvm_align4((uint32_t)sizeof(era_nvm_record_header_t) + payload_length);
}

static bool era_nvm_flash_crc32(const era_nvm_t *nvm, uint32_t offset, uint32_t length, uint32_t *crc_out) {
    uint8_t  scratch[ERA_NVM_PROGRAM_PAGE_BYTES];
    uint32_t crc = ERA_NVM_CRC32_INITIAL;
    while (length > 0U) {
        uint32_t chunk = era_nvm_min_u32(length, sizeof(scratch));
        if (!era_nvm_flash_read(nvm, offset, scratch, chunk)) {
            return false;
        }
        crc = era_nvm_crc32_update(crc, scratch, chunk);
        offset += chunk;
        length -= chunk;
    }
    *crc_out = era_nvm_crc32_finish(crc);
    return true;
}

static void era_nvm_inspect_bank(const era_nvm_t *nvm, uint8_t bank, era_nvm_bank_info_t *info) {
    memset(info, 0, sizeof(*info));
    if (bank >= ERA_NVM_BANK_COUNT) {
        return;
    }

    uint32_t              base = era_nvm_bank_base(bank);
    era_nvm_bank_header_t header;
    if (!era_nvm_flash_read(nvm, base + ERA_NVM_BANK_HEADER_OFFSET, &header, sizeof(header))) {
        info->io_error = true;
        return;
    }
    if (era_nvm_bytes_are((const uint8_t *)&header, sizeof(header), ERA_NVM_ERASED_BYTE)) {
        return;
    }
    if (header.magic != ERA_NVM_BANK_MAGIC || header.format_version != ERA_NVM_FORMAT_VERSION || header.header_size != sizeof(header) ||
        header.generation < ERA_NVM_FIRST_GENERATION || header.generation > ERA_NVM_MAX_GENERATION ||
        header.logical_size != ERA_NVM_LOGICAL_SIZE_BYTES || header.bank_size != ERA_NVM_BANK_SIZE_BYTES ||
        header.snapshot_offset != ERA_NVM_BANK_SNAPSHOT_OFFSET || header.snapshot_size != ERA_NVM_LOGICAL_SIZE_BYTES ||
        header.header_crc32 != era_nvm_crc32(&header, offsetof(era_nvm_bank_header_t, header_crc32))) {
        return;
    }

    era_nvm_bank_activation_t activation;
    if (!era_nvm_flash_read(nvm, base + ERA_NVM_BANK_ACTIVATION_OFFSET, &activation, sizeof(activation))) {
        info->io_error = true;
        return;
    }
    if (activation.magic != ERA_NVM_ACTIVATION_MAGIC || activation.format_version != ERA_NVM_FORMAT_VERSION || activation.activation_size != sizeof(activation) ||
        activation.generation != header.generation || activation.snapshot_crc32 != header.snapshot_crc32 ||
        activation.activation_crc32 != era_nvm_crc32(&activation, offsetof(era_nvm_bank_activation_t, activation_crc32)) ||
        activation.commit != ERA_NVM_ACTIVATION_COMMIT) {
        return;
    }

    uint32_t physical_crc = 0U;
    if (!era_nvm_flash_crc32(nvm, base + ERA_NVM_BANK_SNAPSHOT_OFFSET, ERA_NVM_LOGICAL_SIZE_BYTES, &physical_crc)) {
        info->io_error = true;
        return;
    }
    if (physical_crc != header.snapshot_crc32) {
        return;
    }

    info->valid          = true;
    info->generation     = header.generation;
    info->snapshot_crc32 = header.snapshot_crc32;
}

static era_nvm_result_t era_nvm_select_physical_bank(era_nvm_t *nvm, uint8_t *bank_out, era_nvm_bank_info_t *info_out) {
    era_nvm_bank_info_t banks[ERA_NVM_BANK_COUNT];
    for (uint8_t bank = 0U; bank < ERA_NVM_BANK_COUNT; ++bank) {
        era_nvm_inspect_bank(nvm, bank, &banks[bank]);
        if (banks[bank].io_error) {
            return ERA_NVM_RESULT_IO_ERROR;
        }
    }

    if (!banks[0].valid && !banks[1].valid) {
        return ERA_NVM_RESULT_NOT_READY;
    }

    uint8_t selected = 0U;
    if (!banks[0].valid) {
        selected = 1U;
    } else if (banks[1].valid && banks[1].generation > banks[0].generation) {
        selected = 1U;
    }
    *bank_out = selected;
    *info_out = banks[selected];
    return ERA_NVM_RESULT_OK;
}

static era_nvm_result_t era_nvm_replay_bank_range(era_nvm_t *nvm, uint8_t bank, uint32_t generation, uint32_t address, uint8_t *target, uint32_t length, era_nvm_replay_info_t *replay) {
    uint32_t base = era_nvm_bank_base(bank);
    if (!era_nvm_flash_read(nvm, base + ERA_NVM_BANK_SNAPSHOT_OFFSET + address, target, length)) {
        return ERA_NVM_RESULT_IO_ERROR;
    }

    uint32_t cursor   = ERA_NVM_BANK_JOURNAL_OFFSET;
    uint32_t sequence = ERA_NVM_FIRST_SEQUENCE;
    bool     sealed   = false;

    while (cursor < ERA_NVM_BANK_SIZE_BYTES) {
        uint32_t remaining = ERA_NVM_BANK_SIZE_BYTES - cursor;
        if (remaining < sizeof(era_nvm_record_header_t)) {
            uint8_t tail[sizeof(era_nvm_record_header_t)];
            if (!era_nvm_flash_read(nvm, base + cursor, tail, remaining)) {
                return ERA_NVM_RESULT_IO_ERROR;
            }
            sealed = !era_nvm_bytes_are(tail, remaining, ERA_NVM_ERASED_BYTE);
            break;
        }

        era_nvm_record_header_t header;
        if (!era_nvm_flash_read(nvm, base + cursor, &header, sizeof(header))) {
            return ERA_NVM_RESULT_IO_ERROR;
        }
        if (era_nvm_bytes_are((const uint8_t *)&header, sizeof(header), ERA_NVM_ERASED_BYTE)) {
            break;
        }

        bool header_valid = header.magic == ERA_NVM_RECORD_MAGIC && header.format_version == ERA_NVM_FORMAT_VERSION && header.header_size == sizeof(header) &&
                            header.generation == generation && header.sequence == sequence && header.length > 0U &&
                            header.logical_address < ERA_NVM_LOGICAL_SIZE_BYTES && header.length <= ERA_NVM_LOGICAL_SIZE_BYTES - header.logical_address &&
                            header.record_size == era_nvm_record_size(header.length) && header.record_size <= remaining &&
                            header.header_crc32 == era_nvm_crc32(&header, offsetof(era_nvm_record_header_t, header_crc32));
        if (!header_valid) {
            sealed = true;
            break;
        }

        uint32_t payload_offset = base + cursor + (uint32_t)sizeof(header);
        uint32_t physical_crc   = 0U;
        if (!era_nvm_flash_crc32(nvm, payload_offset, header.length, &physical_crc)) {
            return ERA_NVM_RESULT_IO_ERROR;
        }
        if (physical_crc != header.payload_crc32) {
            sealed = true;
            break;
        }

        uint32_t trailer_cursor = era_nvm_record_trailer_offset(cursor, header.length);
        uint32_t padding_start  = cursor + (uint32_t)sizeof(header) + header.length;
        if (trailer_cursor > padding_start) {
            uint8_t padding[3];
            uint32_t padding_length = trailer_cursor - padding_start;
            if (!era_nvm_flash_read(nvm, base + padding_start, padding, padding_length)) {
                return ERA_NVM_RESULT_IO_ERROR;
            }
            if (!era_nvm_bytes_are(padding, padding_length, ERA_NVM_ERASED_BYTE)) {
                sealed = true;
                break;
            }
        }

        era_nvm_record_trailer_t trailer;
        if (!era_nvm_flash_read(nvm, base + trailer_cursor, &trailer, sizeof(trailer))) {
            return ERA_NVM_RESULT_IO_ERROR;
        }
        if (trailer.magic != ERA_NVM_TRAILER_MAGIC || trailer.sequence != header.sequence || trailer.payload_crc32 != header.payload_crc32 ||
            trailer.commit != ERA_NVM_RECORD_COMMIT) {
            sealed = true;
            break;
        }

        uint32_t record_begin = header.logical_address;
        uint32_t record_end   = record_begin + header.length;
        uint32_t wanted_end   = address + length;
        if (record_begin < wanted_end && address < record_end) {
            uint32_t overlap_begin = record_begin > address ? record_begin : address;
            uint32_t overlap_end   = record_end < wanted_end ? record_end : wanted_end;
            uint32_t overlap_len   = overlap_end - overlap_begin;
            uint32_t payload_index = overlap_begin - record_begin;
            uint32_t target_index  = overlap_begin - address;
            if (!era_nvm_flash_read(nvm, payload_offset + payload_index, target + target_index, overlap_len)) {
                return ERA_NVM_RESULT_IO_ERROR;
            }
        }

        cursor += header.record_size;
        sequence++;
    }

    if (replay != NULL) {
        replay->cursor        = cursor;
        replay->next_sequence = sequence;
        replay->sealed        = sealed;
    }
    return ERA_NVM_RESULT_OK;
}

static era_nvm_result_t era_nvm_scan_inactive_erase_prefix(era_nvm_t *nvm) {
    uint8_t  bank = era_nvm_inactive_bank(nvm);
    uint32_t base = era_nvm_bank_base(bank);
    nvm->inactive_erase_sector = 0U;
    while (nvm->inactive_erase_sector < ERA_NVM_SECTORS_PER_BANK) {
        uint32_t offset = base + (uint32_t)nvm->inactive_erase_sector * ERA_NVM_ERASE_SECTOR_BYTES;
        if (!era_nvm_flash_range_erased(nvm, offset, ERA_NVM_ERASE_SECTOR_BYTES)) {
            break;
        }
        nvm->inactive_erase_sector++;
    }
    return ERA_NVM_RESULT_OK;
}

static era_nvm_result_t era_nvm_finish_inactive_erase(era_nvm_t *nvm) {
    while (nvm->inactive_erase_sector < ERA_NVM_SECTORS_PER_BANK) {
        bool             did_work = false;
        era_nvm_result_t result   = era_nvm_maintenance_erase_one_sector(nvm, &did_work);
        if (result != ERA_NVM_RESULT_OK) {
            return result;
        }
        if (!did_work) {
            break;
        }
    }
    return ERA_NVM_RESULT_OK;
}

static void era_nvm_snapshot_chunk(const era_nvm_t *nvm, uint32_t logical_offset, uint8_t *target, uint32_t length, uint32_t replacement_address, const era_nvm_source_t *replacement) {
    memcpy(target, nvm->image + logical_offset, length);
    if (replacement == NULL || replacement->length == 0U) {
        return;
    }

    uint32_t chunk_end       = logical_offset + length;
    uint32_t replacement_end = replacement_address + replacement->length;
    if (replacement_address >= chunk_end || logical_offset >= replacement_end) {
        return;
    }

    uint32_t overlap_begin = replacement_address > logical_offset ? replacement_address : logical_offset;
    uint32_t overlap_end   = replacement_end < chunk_end ? replacement_end : chunk_end;
    era_nvm_source_copy(replacement, overlap_begin - replacement_address, target + (overlap_begin - logical_offset), overlap_end - overlap_begin);
}

static uint32_t era_nvm_snapshot_crc32(const era_nvm_t *nvm, uint32_t replacement_address, const era_nvm_source_t *replacement) {
    uint8_t  scratch[ERA_NVM_PROGRAM_PAGE_BYTES];
    uint32_t crc = ERA_NVM_CRC32_INITIAL;
    for (uint32_t offset = 0U; offset < ERA_NVM_LOGICAL_SIZE_BYTES; offset += sizeof(scratch)) {
        era_nvm_snapshot_chunk(nvm, offset, scratch, sizeof(scratch), replacement_address, replacement);
        crc = era_nvm_crc32_update(crc, scratch, sizeof(scratch));
    }
    return era_nvm_crc32_finish(crc);
}

static era_nvm_result_t era_nvm_construct_bank(era_nvm_t *nvm, uint8_t bank, uint32_t generation, uint32_t replacement_address, const era_nvm_source_t *replacement) {
    uint32_t base         = era_nvm_bank_base(bank);
    uint32_t snapshot_crc = era_nvm_snapshot_crc32(nvm, replacement_address, replacement);

    era_nvm_bank_header_t header = {
        .magic           = ERA_NVM_BANK_MAGIC,
        .format_version  = ERA_NVM_FORMAT_VERSION,
        .header_size     = sizeof(era_nvm_bank_header_t),
        .generation      = generation,
        .logical_size    = ERA_NVM_LOGICAL_SIZE_BYTES,
        .bank_size       = ERA_NVM_BANK_SIZE_BYTES,
        .snapshot_offset = ERA_NVM_BANK_SNAPSHOT_OFFSET,
        .snapshot_size   = ERA_NVM_LOGICAL_SIZE_BYTES,
        .snapshot_crc32  = snapshot_crc,
        .header_crc32    = 0U,
    };
    header.header_crc32 = era_nvm_crc32(&header, offsetof(era_nvm_bank_header_t, header_crc32));
    if (!era_nvm_flash_program_verified(nvm, base + ERA_NVM_BANK_HEADER_OFFSET, &header, sizeof(header))) {
        return ERA_NVM_RESULT_IO_ERROR;
    }

    uint8_t scratch[ERA_NVM_PROGRAM_PAGE_BYTES];
    for (uint32_t offset = 0U; offset < ERA_NVM_LOGICAL_SIZE_BYTES; offset += sizeof(scratch)) {
        era_nvm_snapshot_chunk(nvm, offset, scratch, sizeof(scratch), replacement_address, replacement);
        if (!era_nvm_flash_program_verified(nvm, base + ERA_NVM_BANK_SNAPSHOT_OFFSET + offset, scratch, sizeof(scratch))) {
            return ERA_NVM_RESULT_IO_ERROR;
        }
    }

    uint32_t physical_snapshot_crc = 0U;
    if (!era_nvm_flash_crc32(nvm, base + ERA_NVM_BANK_SNAPSHOT_OFFSET, ERA_NVM_LOGICAL_SIZE_BYTES, &physical_snapshot_crc) || physical_snapshot_crc != snapshot_crc) {
        return ERA_NVM_RESULT_IO_ERROR;
    }

    era_nvm_bank_activation_t activation = {
        .magic            = ERA_NVM_ACTIVATION_MAGIC,
        .format_version   = ERA_NVM_FORMAT_VERSION,
        .activation_size  = sizeof(era_nvm_bank_activation_t),
        .generation       = generation,
        .snapshot_crc32   = snapshot_crc,
        .activation_crc32 = 0U,
        .commit           = ERA_NVM_ACTIVATION_COMMIT,
    };
    activation.activation_crc32 = era_nvm_crc32(&activation, offsetof(era_nvm_bank_activation_t, activation_crc32));
    if (!era_nvm_flash_program_verified(nvm, base + ERA_NVM_BANK_ACTIVATION_OFFSET, &activation, offsetof(era_nvm_bank_activation_t, commit))) {
        return ERA_NVM_RESULT_IO_ERROR;
    }
    if (!era_nvm_flash_program_verified(nvm, base + ERA_NVM_BANK_ACTIVATION_OFFSET + offsetof(era_nvm_bank_activation_t, commit), &activation.commit, sizeof(activation.commit))) {
        return ERA_NVM_RESULT_IO_ERROR;
    }
    return ERA_NVM_RESULT_OK;
}

static era_nvm_result_t era_nvm_rotate(era_nvm_t *nvm, uint32_t replacement_address, const era_nvm_source_t *replacement) {
    if (nvm->generation >= ERA_NVM_MAX_GENERATION) {
        return ERA_NVM_RESULT_GENERATION_EXHAUSTED;
    }
    era_nvm_result_t result = era_nvm_finish_inactive_erase(nvm);
    if (result != ERA_NVM_RESULT_OK) {
        return result;
    }

    uint8_t  old_active = nvm->active_bank;
    uint8_t  new_active = era_nvm_inactive_bank(nvm);
    uint32_t generation = nvm->generation + 1U;
    result              = era_nvm_construct_bank(nvm, new_active, generation, replacement_address, replacement);
    if (result != ERA_NVM_RESULT_OK) {
        /* The construction target is now ambiguous. The next attempt starts by
         * erasing it from sector zero; the old active bank was never touched. */
        nvm->inactive_erase_sector = 0U;
        return result;
    }

    nvm->active_bank           = new_active;
    nvm->generation            = generation;
    nvm->journal_cursor        = ERA_NVM_BANK_JOURNAL_OFFSET;
    nvm->next_sequence         = ERA_NVM_FIRST_SEQUENCE;
    nvm->tail_sealed           = false;
    nvm->inactive_erase_sector = 0U;
    (void)old_active;
    return ERA_NVM_RESULT_OK;
}

static bool era_nvm_append_record(era_nvm_t *nvm, uint32_t address, const era_nvm_source_t *source) {
    uint32_t record_size = era_nvm_record_size(source->length);
    if (nvm->tail_sealed || record_size > ERA_NVM_BANK_SIZE_BYTES - nvm->journal_cursor) {
        return false;
    }

    uint32_t payload_crc = era_nvm_source_crc32(source);
    era_nvm_record_header_t header = {
        .magic           = ERA_NVM_RECORD_MAGIC,
        .format_version  = ERA_NVM_FORMAT_VERSION,
        .header_size     = sizeof(era_nvm_record_header_t),
        .generation      = nvm->generation,
        .sequence        = nvm->next_sequence,
        .logical_address = address,
        .length          = source->length,
        .payload_crc32   = payload_crc,
        .record_size     = record_size,
        .header_crc32    = 0U,
    };
    header.header_crc32 = era_nvm_crc32(&header, offsetof(era_nvm_record_header_t, header_crc32));

    uint32_t base   = era_nvm_bank_base(nvm->active_bank);
    uint32_t cursor = nvm->journal_cursor;
    if (!era_nvm_flash_program_verified(nvm, base + cursor, &header, sizeof(header))) {
        return false;
    }

    uint8_t  scratch[ERA_NVM_PROGRAM_PAGE_BYTES];
    uint32_t source_offset  = 0U;
    uint32_t payload_cursor = base + cursor + (uint32_t)sizeof(header);
    while (source_offset < source->length) {
        uint32_t page_room = ERA_NVM_PROGRAM_PAGE_BYTES - (payload_cursor % ERA_NVM_PROGRAM_PAGE_BYTES);
        uint32_t chunk     = era_nvm_min_u32(source->length - source_offset, page_room);
        era_nvm_source_copy(source, source_offset, scratch, chunk);
        if (!era_nvm_flash_program_verified(nvm, payload_cursor, scratch, chunk)) {
            return false;
        }
        payload_cursor += chunk;
        source_offset += chunk;
    }

    uint32_t trailer_cursor = era_nvm_record_trailer_offset(cursor, source->length);
    era_nvm_record_trailer_t trailer = {
        .magic         = ERA_NVM_TRAILER_MAGIC,
        .sequence      = nvm->next_sequence,
        .payload_crc32 = payload_crc,
        .commit        = ERA_NVM_RECORD_COMMIT,
    };
    if (!era_nvm_flash_program_verified(nvm, base + trailer_cursor, &trailer, offsetof(era_nvm_record_trailer_t, commit))) {
        return false;
    }
    if (!era_nvm_flash_program_verified(nvm, base + trailer_cursor + offsetof(era_nvm_record_trailer_t, commit), &trailer.commit, sizeof(trailer.commit))) {
        return false;
    }

    nvm->journal_cursor += record_size;
    nvm->next_sequence++;
    return true;
}

static era_nvm_result_t era_nvm_commit_source(era_nvm_t *nvm, uint32_t address, const era_nvm_source_t *source, era_nvm_origin_t origin) {
    uint32_t record_size = era_nvm_record_size(source->length);
    era_nvm_result_t result;
    if (nvm->tail_sealed || record_size > ERA_NVM_BANK_SIZE_BYTES - nvm->journal_cursor) {
        result = era_nvm_rotate(nvm, address, source);
        if (result != ERA_NVM_RESULT_OK) {
            return result;
        }
    } else if (!era_nvm_append_record(nvm, address, source)) {
        nvm->tail_sealed = true;
        return ERA_NVM_RESULT_IO_ERROR;
    }

    era_nvm_publish_source(nvm, address, source);
    era_nvm_notify(nvm, address, source->length, origin);
    return ERA_NVM_RESULT_OK;
}

static era_nvm_result_t era_nvm_ensure_macro_headroom(era_nvm_t *nvm) {
    uint32_t required = era_nvm_record_size(nvm->config.macro_size);
    if (!nvm->tail_sealed && required <= ERA_NVM_BANK_SIZE_BYTES - nvm->journal_cursor) {
        return ERA_NVM_RESULT_OK;
    }
    return era_nvm_rotate(nvm, 0U, NULL);
}

static bool era_nvm_macro_range_overlaps(const era_nvm_t *nvm, uint32_t address, uint32_t length) {
    if (nvm->config.macro_size == 0U || length == 0U) {
        return false;
    }
    uint32_t end       = address + length;
    uint32_t macro_end = nvm->config.macro_address + nvm->config.macro_size;
    return address < macro_end && nvm->config.macro_address < end;
}

static bool era_nvm_macro_range_contains(const era_nvm_t *nvm, uint32_t address, uint32_t length) {
    uint32_t macro_end = nvm->config.macro_address + nvm->config.macro_size;
    return address >= nvm->config.macro_address && address <= macro_end && length <= macro_end - address;
}

static void era_nvm_macro_stage_write(era_nvm_t *nvm, uint32_t address, const uint8_t *data, uint32_t length, bool leave_zero_marker_invalid) {
    uint32_t marker = nvm->config.macro_address + nvm->config.macro_size - 1U;
    for (uint32_t i = 0U; i < length; ++i) {
        uint32_t logical = address + i;
        if (logical == marker && leave_zero_marker_invalid && data[i] == 0U) {
            continue;
        }
        nvm->image[logical] = data[i];
    }
}

static void era_nvm_macro_reset_tracking_clear(era_nvm_t *nvm) {
    nvm->macro_reset_scan_cursor     = 0U;
    nvm->macro_reset_zero_write_seen = false;
    nvm->macro_reset_scan_complete   = false;
    nvm->macro_reset_prior_mode      = ERA_NVM_MACRO_IDLE;
    nvm->macro_reset_saved_marker    = 0U;
}

static void era_nvm_macro_reset_abort_scan(era_nvm_t *nvm) {
    if (nvm->macro_reset_zero_write_seen) {
        uint32_t marker    = nvm->config.macro_address + nvm->config.macro_size - 1U;
        nvm->image[marker] = nvm->macro_reset_saved_marker;
        nvm->macro_mode    = nvm->macro_reset_prior_mode;
    }
    era_nvm_macro_reset_tracking_clear(nvm);
}

static era_nvm_result_t era_nvm_macro_reset_begin(era_nvm_t *nvm) {
    if (nvm->macro_reset_zero_write_seen) {
        return ERA_NVM_RESULT_OK;
    }

    /* Invalidate the public marker only after a mandatory future whole-domain
     * write is guaranteed to fit. If an upload is already open, its opener has
     * already made this same headroom guarantee and has issued no durable
     * writes since. */
    era_nvm_result_t result = era_nvm_ensure_macro_headroom(nvm);
    if (result != ERA_NVM_RESULT_OK) {
        return result;
    }

    uint32_t marker                    = nvm->config.macro_address + nvm->config.macro_size - 1U;
    nvm->macro_reset_prior_mode        = nvm->macro_mode;
    nvm->macro_reset_saved_marker      = nvm->image[marker];
    nvm->macro_reset_zero_write_seen   = true;
    nvm->macro_mode                    = ERA_NVM_MACRO_RESET_OPEN;
    nvm->image[marker]                 = ERA_NVM_ERASED_BYTE;
    return ERA_NVM_RESULT_OK;
}

static era_nvm_result_t era_nvm_macro_finish_write(era_nvm_t *nvm) {
    era_nvm_source_t source = {
        .kind             = ERA_NVM_SOURCE_IMAGE,
        .nvm              = nvm,
        .image_address    = nvm->config.macro_address,
        .length           = nvm->config.macro_size,
        .override_enabled = true,
        .override_offset  = nvm->config.macro_size - 1U,
        .override_value   = 0U,
    };
    era_nvm_result_t result = era_nvm_commit_source(nvm, nvm->config.macro_address, &source, ERA_NVM_ORIGIN_MACRO_TRANSACTION);
    if (result == ERA_NVM_RESULT_OK) {
        nvm->macro_mode         = ERA_NVM_MACRO_IDLE;
        nvm->macro_payload_seen = false;
        era_nvm_macro_reset_tracking_clear(nvm);
    }
    return result;
}

static era_nvm_result_t era_nvm_macro_finish_reset(era_nvm_t *nvm) {
    era_nvm_source_t source = {
        .kind   = ERA_NVM_SOURCE_ZERO,
        .length = nvm->config.macro_size,
    };
    if (era_nvm_source_matches_image(nvm, nvm->config.macro_address, &source)) {
        nvm->macro_mode         = ERA_NVM_MACRO_IDLE;
        nvm->macro_payload_seen = false;
        era_nvm_macro_reset_tracking_clear(nvm);
        return ERA_NVM_RESULT_NO_CHANGE;
    }
    era_nvm_result_t result = era_nvm_commit_source(nvm, nvm->config.macro_address, &source, ERA_NVM_ORIGIN_MACRO_TRANSACTION);
    if (result == ERA_NVM_RESULT_OK) {
        nvm->macro_mode         = ERA_NVM_MACRO_IDLE;
        nvm->macro_payload_seen = false;
        era_nvm_macro_reset_tracking_clear(nvm);
    } else {
        uint32_t marker    = nvm->config.macro_address + nvm->config.macro_size - 1U;
        nvm->image[marker] = ERA_NVM_ERASED_BYTE;
        nvm->macro_mode    = ERA_NVM_MACRO_RESET_OPEN;
        era_nvm_macro_reset_tracking_clear(nvm);
    }
    return result;
}

static era_nvm_result_t era_nvm_macro_qmk_write(era_nvm_t *nvm, uint32_t address, const uint8_t *data, uint32_t length) {
    uint32_t macro_base = nvm->config.macro_address;
    uint32_t macro_size = nvm->config.macro_size;
    uint32_t marker     = macro_base + macro_size - 1U;
    bool     marker_written = marker >= address && marker - address < length;
    uint8_t  marker_value   = marker_written ? data[marker - address] : 0U;
    bool     payload_written = address < marker && length > 0U;

    bool reset_chunk = length == ERA_NVM_QMK_MACRO_RESET_CHUNK_BYTES && era_nvm_bytes_are(data, length, 0U);
    bool reset_write_matches_scan = nvm->macro_reset_scan_cursor >= length &&
                                    address == macro_base + nvm->macro_reset_scan_cursor - length && reset_chunk;
    if (nvm->macro_reset_scan_complete) {
        if (!reset_write_matches_scan || address + length != macro_base + macro_size) {
            era_nvm_macro_reset_abort_scan(nvm);
        } else {
            era_nvm_result_t result = era_nvm_macro_reset_begin(nvm);
            if (result != ERA_NVM_RESULT_OK) {
                return result;
            }
            return era_nvm_macro_finish_reset(nvm);
        }
    } else if (nvm->macro_reset_scan_cursor != 0U) {
        if (!reset_write_matches_scan) {
            era_nvm_macro_reset_abort_scan(nvm);
        } else {
            era_nvm_result_t result = era_nvm_macro_reset_begin(nvm);
            if (result != ERA_NVM_RESULT_OK) {
                era_nvm_macro_reset_abort_scan(nvm);
                return result;
            }
            return ERA_NVM_RESULT_STAGED;
        }
    }

    /* A failed RESET intentionally leaves the marker invalid. A later stock
     * upload may have its 0xFF opener optimized away by eeprom_update_byte();
     * let the first actual payload write resume the ordinary open transcript. */
    if (nvm->macro_mode == ERA_NVM_MACRO_RESET_OPEN) {
        nvm->macro_mode         = ERA_NVM_MACRO_WRITE_OPEN;
        nvm->macro_payload_seen = false;
    }

    if (nvm->macro_mode == ERA_NVM_MACRO_IDLE) {
        if (!marker_written || marker_value == 0U) {
            return ERA_NVM_RESULT_PROTOCOL;
        }
        era_nvm_result_t result = era_nvm_ensure_macro_headroom(nvm);
        if (result != ERA_NVM_RESULT_OK) {
            return result;
        }
        nvm->macro_mode         = ERA_NVM_MACRO_WRITE_OPEN;
        nvm->macro_payload_seen = false;
    }

    era_nvm_macro_stage_write(nvm, address, data, length, marker_written && marker_value == 0U);
    if (payload_written) {
        nvm->macro_payload_seen = true;
    }
    if (marker_written && marker_value == 0U) {
        if (!nvm->macro_payload_seen) {
            nvm->image[marker] = ERA_NVM_ERASED_BYTE;
            return ERA_NVM_RESULT_PROTOCOL;
        }
        return era_nvm_macro_finish_write(nvm);
    }
    return ERA_NVM_RESULT_STAGED;
}

void era_nvm_setup(era_nvm_t *nvm, const era_nvm_flash_t *flash, const era_nvm_config_t *config) {
    if (nvm == NULL) {
        return;
    }
    memset(nvm, 0, sizeof(*nvm));
    nvm->active_bank = ERA_NVM_NO_ACTIVE_BANK;
    nvm->state       = ERA_NVM_STATE_UNINITIALIZED;
    if (flash != NULL) {
        nvm->flash = *flash;
    }
    if (config != NULL) {
        nvm->config = *config;
    }
}

era_nvm_result_t era_nvm_mount(era_nvm_t *nvm) {
    if (nvm == NULL || nvm->flash.read == NULL || nvm->flash.program == NULL || nvm->flash.erase_sector == NULL ||
        (nvm->config.macro_size != 0U && nvm->config.macro_size != ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES) ||
        (nvm->config.macro_size != 0U && !era_nvm_range_valid(nvm->config.macro_address, nvm->config.macro_size, ERA_NVM_LOGICAL_SIZE_BYTES))) {
        if (nvm != NULL) {
            nvm->state = ERA_NVM_STATE_FAULTED;
        }
        return ERA_NVM_RESULT_INVALID_ARGUMENT;
    }
    nvm->state       = ERA_NVM_STATE_UNINITIALIZED;
    nvm->active_bank = ERA_NVM_NO_ACTIVE_BANK;
    if (nvm->flash.init != NULL && !nvm->flash.init(nvm->flash.context)) {
        nvm->state = ERA_NVM_STATE_FAULTED;
        return ERA_NVM_RESULT_IO_ERROR;
    }

    uint8_t             bank = 0U;
    era_nvm_bank_info_t bank_info;
    era_nvm_result_t    result = era_nvm_select_physical_bank(nvm, &bank, &bank_info);
    if (result == ERA_NVM_RESULT_NOT_READY) {
        memset(nvm->image, ERA_NVM_LOGICAL_ERASED_BYTE, sizeof(nvm->image));
        nvm->inactive_erase_sector = 0U;

        /* Fresh format always constructs bank A. Verify/erase every sector
         * that is not already physically erased; old QMK bytes are never
         * parsed or migrated. */
        for (uint8_t sector = 0U; sector < ERA_NVM_SECTORS_PER_BANK; ++sector) {
            uint32_t offset = era_nvm_bank_base(0U) + (uint32_t)sector * ERA_NVM_ERASE_SECTOR_BYTES;
            if (!era_nvm_flash_range_erased(nvm, offset, ERA_NVM_ERASE_SECTOR_BYTES) && !era_nvm_flash_erase_verified(nvm, offset)) {
                nvm->state = ERA_NVM_STATE_FAULTED;
                return ERA_NVM_RESULT_IO_ERROR;
            }
        }
        result = era_nvm_construct_bank(nvm, 0U, ERA_NVM_FIRST_GENERATION, 0U, NULL);
        if (result != ERA_NVM_RESULT_OK) {
            nvm->state = ERA_NVM_STATE_FAULTED;
            return result;
        }
        nvm->active_bank    = 0U;
        nvm->generation     = ERA_NVM_FIRST_GENERATION;
        nvm->journal_cursor = ERA_NVM_BANK_JOURNAL_OFFSET;
        nvm->next_sequence  = ERA_NVM_FIRST_SEQUENCE;
        nvm->tail_sealed    = false;
        nvm->macro_mode         = ERA_NVM_MACRO_IDLE;
        nvm->macro_payload_seen = false;
        era_nvm_macro_reset_tracking_clear(nvm);
        nvm->state = ERA_NVM_STATE_READY;
        (void)era_nvm_scan_inactive_erase_prefix(nvm);
        return ERA_NVM_RESULT_OK;
    }
    if (result != ERA_NVM_RESULT_OK) {
        nvm->state = ERA_NVM_STATE_FAULTED;
        return result;
    }

    era_nvm_replay_info_t replay;
    result = era_nvm_replay_bank_range(nvm, bank, bank_info.generation, 0U, nvm->image, ERA_NVM_LOGICAL_SIZE_BYTES, &replay);
    if (result != ERA_NVM_RESULT_OK) {
        nvm->state = ERA_NVM_STATE_FAULTED;
        return result;
    }

    nvm->active_bank    = bank;
    nvm->generation     = bank_info.generation;
    nvm->journal_cursor = replay.cursor;
    nvm->next_sequence  = replay.next_sequence;
    nvm->tail_sealed    = replay.sealed;
    nvm->macro_mode         = ERA_NVM_MACRO_IDLE;
    nvm->macro_payload_seen = false;
    era_nvm_macro_reset_tracking_clear(nvm);
    nvm->state          = ERA_NVM_STATE_READY;
    (void)era_nvm_scan_inactive_erase_prefix(nvm);
    return ERA_NVM_RESULT_OK;
}

era_nvm_state_t era_nvm_state(const era_nvm_t *nvm) {
    return nvm == NULL ? ERA_NVM_STATE_FAULTED : nvm->state;
}

uint8_t era_nvm_active_bank(const era_nvm_t *nvm) {
    return nvm == NULL ? ERA_NVM_NO_ACTIVE_BANK : nvm->active_bank;
}

uint32_t era_nvm_generation(const era_nvm_t *nvm) {
    return nvm == NULL ? 0U : nvm->generation;
}

uint32_t era_nvm_journal_free_bytes(const era_nvm_t *nvm) {
    if (nvm == NULL || nvm->state != ERA_NVM_STATE_READY || nvm->journal_cursor > ERA_NVM_BANK_SIZE_BYTES) {
        return 0U;
    }
    return ERA_NVM_BANK_SIZE_BYTES - nvm->journal_cursor;
}

bool era_nvm_tail_is_sealed(const era_nvm_t *nvm) {
    return nvm != NULL && nvm->tail_sealed;
}

void era_nvm_get_diagnostics(const era_nvm_t *nvm, era_nvm_diagnostics_t *diagnostics) {
    if (diagnostics == NULL) {
        return;
    }
    memset(diagnostics, 0, sizeof(*diagnostics));
    if (nvm == NULL) {
        return;
    }
    diagnostics->program_count         = nvm->program_count;
    diagnostics->program_failure_count = nvm->program_failure_count;
    diagnostics->erase_count           = nvm->erase_count;
    diagnostics->erase_failure_count   = nvm->erase_failure_count;
}

era_nvm_result_t era_nvm_read(const era_nvm_t *nvm, uint32_t address, void *data, size_t length) {
    if (nvm == NULL || nvm->state != ERA_NVM_STATE_READY) {
        return ERA_NVM_RESULT_NOT_READY;
    }
    if (!era_nvm_range_valid(address, length, ERA_NVM_LOGICAL_SIZE_BYTES) || (length > 0U && data == NULL)) {
        return ERA_NVM_RESULT_INVALID_ARGUMENT;
    }
    if (length > 0U) {
        memcpy(data, nvm->image + address, length);
    }
    return ERA_NVM_RESULT_OK;
}

era_nvm_result_t era_nvm_qmk_read(era_nvm_t *nvm, uint32_t address, void *data, size_t length) {
    era_nvm_result_t result = era_nvm_read(nvm, address, data, length);
    if (result != ERA_NVM_RESULT_OK || nvm->config.macro_size == 0U) {
        return result;
    }

    uint32_t macro_base = nvm->config.macro_address;
    uint32_t macro_size = nvm->config.macro_size;
    uint32_t length32   = (uint32_t)length;
    bool reset_read_chunk = length32 == ERA_NVM_QMK_MACRO_RESET_CHUNK_BYTES &&
                            address >= macro_base && address <= macro_base + macro_size - length32 &&
                            (address - macro_base) % ERA_NVM_QMK_MACRO_RESET_CHUNK_BYTES == 0U;
    if (!reset_read_chunk) {
        era_nvm_macro_reset_abort_scan(nvm);
        return ERA_NVM_RESULT_OK;
    }

    uint32_t chunk_offset = address - macro_base;
    if (chunk_offset == 0U) {
        era_nvm_macro_reset_abort_scan(nvm);
        nvm->macro_reset_scan_cursor = length32;
    } else if (chunk_offset == nvm->macro_reset_scan_cursor) {
        nvm->macro_reset_scan_cursor += length32;
    } else {
        era_nvm_macro_reset_abort_scan(nvm);
        return ERA_NVM_RESULT_OK;
    }

    if (nvm->macro_reset_scan_cursor == macro_size) {
        if (!nvm->macro_reset_zero_write_seen && era_nvm_bytes_are((const uint8_t *)data, length, 0U)) {
            /* A complete stock reset scan with no write callbacks means every
             * 16-byte chunk was already zero, including the final marker. */
            era_nvm_macro_reset_tracking_clear(nvm);
        } else {
            /* The generic update helper now has to issue a final zero write:
             * either this chunk differs itself or an earlier reset write made
             * the public final-byte marker invalid/nonzero. That exact write is
             * the synchronous whole-domain RESET commit point. */
            nvm->macro_reset_scan_complete = true;
        }
    }
    return ERA_NVM_RESULT_OK;
}

era_nvm_result_t era_nvm_replace(era_nvm_t *nvm, uint32_t address, const void *data, size_t length, era_nvm_origin_t origin) {
    if (nvm == NULL || nvm->state != ERA_NVM_STATE_READY) {
        return ERA_NVM_RESULT_NOT_READY;
    }
    if (!era_nvm_range_valid(address, length, ERA_NVM_LOGICAL_SIZE_BYTES) || (length > 0U && data == NULL)) {
        return ERA_NVM_RESULT_INVALID_ARGUMENT;
    }
    /* The exclusivity is scoped to the macro domain, and the scope is the whole
     * rule. An open upload cannot be published by a rotation that snapshots it,
     * because the staged marker stays nonzero - so a durable write to any other
     * range is safe while one is open, and refusing it is not.
     *
     * VIA runs no other save during an upload, but the *keyboard* does: an
     * RGB Toggle pressed by accident reaches eeconfig_update_rgb_matrix(),
     * which this fork defers by ERA_STORAGE_QUIET_DEFER_MS (quantum/eeconfig.h)
     * and then flushes from housekeeping, unattended, inside a multi-second
     * transfer. That flush consumes its dirty flag before the update call and
     * the update path reports nothing, so a refusal here is a silently lost
     * setting with no retry - which is the shape
     * quantum/wear_leveling/wear_leveling.c's reentrancy interlock already
     * examined and rejected in as many words. */
    if (nvm->macro_mode != ERA_NVM_MACRO_IDLE && era_nvm_macro_range_overlaps(nvm, address, (uint32_t)length)) {
        return ERA_NVM_RESULT_BUSY;
    }
    if (length == 0U) {
        return ERA_NVM_RESULT_NO_CHANGE;
    }

    era_nvm_source_t source = {
        .kind   = ERA_NVM_SOURCE_BUFFER,
        .buffer = (const uint8_t *)data,
        .length = (uint32_t)length,
    };
    if (era_nvm_source_matches_image(nvm, address, &source)) {
        return ERA_NVM_RESULT_NO_CHANGE;
    }
    return era_nvm_commit_source(nvm, address, &source, origin);
}

era_nvm_result_t era_nvm_format(era_nvm_t *nvm) {
    if (nvm == NULL || nvm->state != ERA_NVM_STATE_READY) {
        return ERA_NVM_RESULT_NOT_READY;
    }

    /* A format is a rotation whose snapshot is the erased logical image, not a
     * sweep of journal records. That is what makes it power-safe for free: the
     * current bank stays authoritative until the new bank's activation commit,
     * and the whole operation costs one bank construction instead of the
     * ninety-six range records a caller would need to reach the same state
     * through era_nvm_replace() without owning a 24-KiB zero buffer.
     *
     * It is deliberately not refused while a macro transaction is open. A
     * format is the destructive gesture (eeprom_driver_erase(), CLEAN); an
     * upload that was still staging has nothing left to publish afterwards, so
     * the transcript state is cleared rather than preserved. */
    era_nvm_source_t source = {
        .kind   = ERA_NVM_SOURCE_ZERO,
        .length = ERA_NVM_LOGICAL_SIZE_BYTES,
    };
    era_nvm_result_t result = era_nvm_rotate(nvm, 0U, &source);
    if (result != ERA_NVM_RESULT_OK) {
        return result;
    }

    memset(nvm->image, ERA_NVM_LOGICAL_ERASED_BYTE, sizeof(nvm->image));
    nvm->macro_mode         = ERA_NVM_MACRO_IDLE;
    nvm->macro_payload_seen = false;
    era_nvm_macro_reset_tracking_clear(nvm);
    era_nvm_notify(nvm, 0U, ERA_NVM_LOGICAL_SIZE_BYTES, ERA_NVM_ORIGIN_FORMAT);
    return ERA_NVM_RESULT_OK;
}

era_nvm_result_t era_nvm_qmk_write(era_nvm_t *nvm, uint32_t address, const void *data, size_t length) {
    if (nvm == NULL || nvm->state != ERA_NVM_STATE_READY) {
        return ERA_NVM_RESULT_NOT_READY;
    }
    if (!era_nvm_range_valid(address, length, ERA_NVM_LOGICAL_SIZE_BYTES) || (length > 0U && data == NULL)) {
        return ERA_NVM_RESULT_INVALID_ARGUMENT;
    }
    if (length == 0U) {
        return ERA_NVM_RESULT_NO_CHANGE;
    }

    uint32_t length32 = (uint32_t)length;
    bool overlaps_macro = era_nvm_macro_range_overlaps(nvm, address, length32);
    if (!overlaps_macro) {
        era_nvm_macro_reset_abort_scan(nvm);
        /* era_nvm_replace() owns the one exclusivity test; a range outside the
         * macro domain never trips it. */
        return era_nvm_replace(nvm, address, data, length, ERA_NVM_ORIGIN_LOCAL_QMK);
    }
    if (!era_nvm_macro_range_contains(nvm, address, length32)) {
        return ERA_NVM_RESULT_INVALID_ARGUMENT;
    }
    return era_nvm_macro_qmk_write(nvm, address, (const uint8_t *)data, length32);
}

era_nvm_result_t era_nvm_replay_read(era_nvm_t *nvm, uint32_t address, void *data, size_t length) {
    if (nvm == NULL || nvm->state != ERA_NVM_STATE_READY) {
        return ERA_NVM_RESULT_NOT_READY;
    }
    if (!era_nvm_range_valid(address, length, ERA_NVM_LOGICAL_SIZE_BYTES) || (length > 0U && data == NULL)) {
        return ERA_NVM_RESULT_INVALID_ARGUMENT;
    }
    if (length == 0U) {
        return ERA_NVM_RESULT_OK;
    }

    uint8_t             bank = 0U;
    era_nvm_bank_info_t info;
    era_nvm_result_t    result = era_nvm_select_physical_bank(nvm, &bank, &info);
    if (result != ERA_NVM_RESULT_OK) {
        return ERA_NVM_RESULT_IO_ERROR;
    }
    return era_nvm_replay_bank_range(nvm, bank, info.generation, address, (uint8_t *)data, (uint32_t)length, NULL);
}

era_nvm_result_t era_nvm_maintenance_erase_one_sector(era_nvm_t *nvm, bool *did_work) {
    if (did_work != NULL) {
        *did_work = false;
    }
    if (nvm == NULL || nvm->state != ERA_NVM_STATE_READY || nvm->active_bank >= ERA_NVM_BANK_COUNT) {
        return ERA_NVM_RESULT_NOT_READY;
    }
    if (nvm->inactive_erase_sector >= ERA_NVM_SECTORS_PER_BANK) {
        return ERA_NVM_RESULT_OK;
    }

    uint8_t  bank   = era_nvm_inactive_bank(nvm);
    uint32_t offset = era_nvm_bank_base(bank) + (uint32_t)nvm->inactive_erase_sector * ERA_NVM_ERASE_SECTOR_BYTES;
    if (!era_nvm_flash_erase_verified(nvm, offset)) {
        return ERA_NVM_RESULT_IO_ERROR;
    }
    nvm->inactive_erase_sector++;
    if (did_work != NULL) {
        *did_work = true;
    }
    return ERA_NVM_RESULT_OK;
}
