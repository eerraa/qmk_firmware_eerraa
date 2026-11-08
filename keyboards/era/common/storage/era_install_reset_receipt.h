// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ERA_INSTALL_RESET_RECEIPT_BYTES 256U

typedef enum {
    ERA_INSTALL_RESET_PENDING,
    ERA_INSTALL_RESET_DONE,
    ERA_INSTALL_RESET_INVALID,
} era_install_reset_receipt_state_t;

/* The installer re-arms this page; settings erasure must never re-arm it. */
static const uint8_t era_install_reset_receipt_header[] = {
    'E', 'R', 'A', 'I', 1, 0, 0, 0,
    'D', 'O', 'N', 'E', 0xBB, 0xB0, 0xB1, 0xBA,
};

static inline era_install_reset_receipt_state_t era_install_reset_receipt_state(const uint8_t *page) {
    if (page == NULL) {
        return ERA_INSTALL_RESET_INVALID;
    }
    bool pending = true;
    bool done    = true;
    for (size_t index = 0; index < ERA_INSTALL_RESET_RECEIPT_BYTES; ++index) {
        uint8_t expected = index < sizeof(era_install_reset_receipt_header) ? era_install_reset_receipt_header[index] : 0xFFU;
        pending = pending && page[index] == 0xFFU;
        done    = done && page[index] == expected;
    }
    return pending ? ERA_INSTALL_RESET_PENDING : done ? ERA_INSTALL_RESET_DONE : ERA_INSTALL_RESET_INVALID;
}

static inline void era_install_reset_receipt_make(uint8_t *page) {
    memset(page, 0xFF, ERA_INSTALL_RESET_RECEIPT_BYTES);
    memcpy(page, era_install_reset_receipt_header, sizeof(era_install_reset_receipt_header));
}
