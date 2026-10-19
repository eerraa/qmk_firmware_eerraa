// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

/* ERA NVM v1 is intentionally independent of QMK wear-leveling's physical
 * format. Offsets in this file are relative to the 128-KiB ERA NVM region. */
#define ERA_NVM_FORMAT_VERSION 1U

#define ERA_NVM_LOGICAL_SIZE_BYTES (24U * 1024U)
#define ERA_NVM_PROGRAM_PAGE_BYTES 256U
#define ERA_NVM_ERASE_SECTOR_BYTES (4U * 1024U)
#define ERA_NVM_BANK_SIZE_BYTES (64U * 1024U)
#define ERA_NVM_BANK_COUNT 2U
#define ERA_NVM_PHYSICAL_SIZE_BYTES (ERA_NVM_BANK_SIZE_BYTES * ERA_NVM_BANK_COUNT)

#define ERA_NVM_BANK_HEADER_OFFSET 0x0000U
#define ERA_NVM_BANK_ACTIVATION_OFFSET 0x0100U
#define ERA_NVM_BANK_SNAPSHOT_OFFSET 0x0200U
#define ERA_NVM_BANK_JOURNAL_OFFSET (ERA_NVM_BANK_SNAPSHOT_OFFSET + ERA_NVM_LOGICAL_SIZE_BYTES)
#define ERA_NVM_BANK_JOURNAL_BYTES (ERA_NVM_BANK_SIZE_BYTES - ERA_NVM_BANK_JOURNAL_OFFSET)

#define ERA_NVM_BANK_MAGIC 0x314E5245UL       /* "ERN1" on flash, little-endian. */
#define ERA_NVM_ACTIVATION_MAGIC 0x31564145UL /* "EAV1". */
#define ERA_NVM_RECORD_MAGIC 0x31525245UL     /* "ERR1". */
#define ERA_NVM_TRAILER_MAGIC 0x31545245UL    /* "ERT1". */
#define ERA_NVM_ACTIVATION_COMMIT 0x4B4F5641UL /* "AVOK". */
#define ERA_NVM_RECORD_COMMIT 0x4B4F4352UL     /* "RCOK". */

#define ERA_NVM_FIRST_GENERATION 1UL
#define ERA_NVM_MAX_GENERATION 0xFFFFFFFEUL
#define ERA_NVM_FIRST_SEQUENCE 1UL

typedef struct {
    uint32_t magic;
    uint16_t format_version;
    uint16_t header_size;
    uint32_t generation;
    uint32_t logical_size;
    uint32_t bank_size;
    uint32_t snapshot_offset;
    uint32_t snapshot_size;
    uint32_t snapshot_crc32;
    uint32_t header_crc32;
} era_nvm_bank_header_t;

typedef struct {
    uint32_t magic;
    uint16_t format_version;
    uint16_t activation_size;
    uint32_t generation;
    uint32_t snapshot_crc32;
    uint32_t activation_crc32;
    uint32_t commit;
} era_nvm_bank_activation_t;

typedef struct {
    uint32_t magic;
    uint16_t format_version;
    uint16_t header_size;
    uint32_t generation;
    uint32_t sequence;
    uint32_t logical_address;
    uint32_t length;
    uint32_t payload_crc32;
    uint32_t record_size;
    uint32_t header_crc32;
} era_nvm_record_header_t;

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint32_t payload_crc32;
    uint32_t commit;
} era_nvm_record_trailer_t;

#if defined(__cplusplus)
#    define ERA_NVM_STATIC_ASSERT static_assert
#else
#    define ERA_NVM_STATIC_ASSERT _Static_assert
#endif

ERA_NVM_STATIC_ASSERT(sizeof(era_nvm_bank_header_t) == 36U, "ERA NVM bank header layout changed");
ERA_NVM_STATIC_ASSERT(sizeof(era_nvm_bank_activation_t) == 24U, "ERA NVM activation layout changed");
ERA_NVM_STATIC_ASSERT(sizeof(era_nvm_record_header_t) == 36U, "ERA NVM record header layout changed");
ERA_NVM_STATIC_ASSERT(sizeof(era_nvm_record_trailer_t) == 16U, "ERA NVM record trailer layout changed");
ERA_NVM_STATIC_ASSERT(ERA_NVM_BANK_ACTIVATION_OFFSET % ERA_NVM_PROGRAM_PAGE_BYTES == 0U, "ERA NVM activation page must be page aligned");
ERA_NVM_STATIC_ASSERT(ERA_NVM_BANK_SNAPSHOT_OFFSET % ERA_NVM_PROGRAM_PAGE_BYTES == 0U, "ERA NVM snapshot must be page aligned");
ERA_NVM_STATIC_ASSERT(ERA_NVM_LOGICAL_SIZE_BYTES % ERA_NVM_PROGRAM_PAGE_BYTES == 0U, "ERA NVM snapshot must occupy complete program pages");
ERA_NVM_STATIC_ASSERT(ERA_NVM_BANK_SIZE_BYTES % ERA_NVM_ERASE_SECTOR_BYTES == 0U, "ERA NVM bank must occupy complete erase sectors");
ERA_NVM_STATIC_ASSERT(ERA_NVM_BANK_JOURNAL_OFFSET < ERA_NVM_BANK_SIZE_BYTES, "ERA NVM journal must fit in a bank");

#undef ERA_NVM_STATIC_ASSERT
