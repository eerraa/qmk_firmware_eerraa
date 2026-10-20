// SPDX-License-Identifier: GPL-2.0-or-later
#include "era_tomak_sync_admission.h"

#include <string.h>

static struct {
    uint8_t request[32];
    uint16_t session;
    uint32_t expires_ms;
    bool valid;
} admission;

bool era_tomak_sync_admission_hold(const uint8_t request[32], uint16_t session, uint32_t now_ms, uint16_t wait_ms) {
    if (admission.valid) {
        return false;
    }
    memcpy(admission.request, request, sizeof(admission.request));
    admission.session = session;
    admission.expires_ms = now_ms + wait_ms;
    admission.valid = true;
    return true;
}

bool era_tomak_sync_admission_pending(void) {
    return admission.valid;
}

void era_tomak_sync_admission_cancel(void) {
    admission.valid = false;
}

bool era_tomak_sync_admission_cancel_matching(const uint8_t *prefix, uint8_t length, uint16_t session) {
    if (!admission.valid || session != admission.session || length > sizeof(admission.request) ||
        memcmp(prefix, admission.request, length) != 0) {
        return false;
    }
    admission.valid = false;
    return true;
}

era_tomak_admission_result_t era_tomak_sync_admission_take(uint8_t request[32], uint16_t session, uint32_t now_ms, bool output_ready) {
    if (!admission.valid) {
        return ERA_TOMAK_ADMISSION_EMPTY;
    }
    if (session != admission.session) {
        /* Do not expose an old request to the replacement host session. */
        admission.valid = false;
        return ERA_TOMAK_ADMISSION_CANCELLED;
    }
    bool expired = (int32_t)(now_ms - admission.expires_ms) >= 0;
    if (!expired && !output_ready) {
        return ERA_TOMAK_ADMISSION_WAITING;
    }
    admission.valid = false;
    memcpy(request, admission.request, sizeof(admission.request));
    return expired ? ERA_TOMAK_ADMISSION_EXPIRED : ERA_TOMAK_ADMISSION_EXECUTE;
}
