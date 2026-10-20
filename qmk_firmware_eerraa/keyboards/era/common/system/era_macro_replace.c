// SPDX-License-Identifier: GPL-2.0-or-later
#include "era_macro_replace.h"
#include "era_wire.h"
#include "../storage/era_eeprom_driver.h"
#include "dynamic_keymap.h"
#include "via.h"
#include <string.h>

/* Reports retain the complete request prefix for VIA's echo matcher. Result
 * lives at byte 31; state/received/CRC at 30/28..29/24..27. DATA uses <=17
 * bytes (3-byte header, 8-byte client nonce, offset16, size8). A committed
 * receipt certifies LOCAL durability. Byte 23 separately records a proved
 * peer convergence of that same, still-current local publication. */
static struct {
    uint8_t token[8], state, peer_converged;
    uint16_t length, received;
    uint32_t crc, serial;
} upload;
void era_macro_replace_session_lost(void) {
    if (era_eeprom_driver_macro_replace_open() &&
        era_eeprom_driver_macro_abort() != ERA_NVM_RESULT_OK) return;
    memset(&upload, 0, sizeof(upload));
}
void era_macro_replace_note_converged(void) {
    if (upload.state == 2 && upload.serial == era_eeprom_driver_macro_serial())
        upload.peer_converged = 1;
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)p[0] << 8 | p[1]; }
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint8_t result_code(era_nvm_result_t r) {
    if (r == ERA_NVM_RESULT_OK || r == ERA_NVM_RESULT_STAGED || r == ERA_NVM_RESULT_NO_CHANGE)
        return ERA_WIRE_MACRO_REPLACE_RESULT_OK;
    if (r == ERA_NVM_RESULT_BUSY) return ERA_WIRE_MACRO_REPLACE_RESULT_BUSY;
    if (r == ERA_NVM_RESULT_PROTOCOL || r == ERA_NVM_RESULT_INVALID_ARGUMENT)
        return ERA_WIRE_MACRO_REPLACE_RESULT_INVALID;
    return ERA_WIRE_MACRO_REPLACE_RESULT_IO_ERROR;
}
bool era_macro_replace_handle_via(uint8_t *data, uint8_t length) {
    if (length != 32 || data[1] != ERA_WIRE_CHANNELS_EERRAA_MACRO_REPLACE) return false;
    uint8_t op = data[2];
    if (op == ERA_WIRE_MACRO_REPLACE_CAPABILITIES && data[0] == id_custom_get_value) {
        data[3] = ERA_WIRE_MACRO_REPLACE_VERSION_CURRENT;
        data[4] = 17;
        data[5] = (DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - 1U) >> 8;
        data[6] = (DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - 1U) & 0xFF;
        memcpy(data + 7, "EMR1", 4);
        data[31] = ERA_WIRE_MACRO_REPLACE_RESULT_OK;
        return true;
    }
    if ((op == ERA_WIRE_MACRO_REPLACE_STATUS && data[0] != id_custom_get_value) ||
        (op != ERA_WIRE_MACRO_REPLACE_STATUS && data[0] != id_custom_set_value) ||
        op < ERA_WIRE_MACRO_REPLACE_BEGIN || op > ERA_WIRE_MACRO_REPLACE_STATUS) return false;
    uint8_t status = ERA_WIRE_MACRO_REPLACE_RESULT_STALE;
    if (upload.state && upload.serial != era_eeprom_driver_macro_serial()) upload.state = 0;
    bool same = upload.state && memcmp(upload.token, data + 3, 8) == 0;
    if (op == ERA_WIRE_MACRO_REPLACE_BEGIN) {
        uint16_t size = get16(data + 11);
        uint32_t crc = get32(data + 13);
        uint8_t nonzero = 0;
        for (unsigned i = 3; i < 11; ++i) nonzero |= data[i];
        if (!nonzero || size >= DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE) {
            status = ERA_WIRE_MACRO_REPLACE_RESULT_INVALID;
        } else if (same) {
            status = upload.state != 3 && upload.length == size && upload.crc == crc ?
                ERA_WIRE_MACRO_REPLACE_RESULT_OK : ERA_WIRE_MACRO_REPLACE_RESULT_INVALID;
        } else {
            era_nvm_result_t r = ERA_NVM_RESULT_OK;
            if (era_eeprom_driver_macro_replace_open()) r = era_eeprom_driver_macro_abort();
            if (r == ERA_NVM_RESULT_OK) r = era_eeprom_driver_macro_begin(size);
            status = result_code(r);
            if (status == ERA_WIRE_MACRO_REPLACE_RESULT_OK) {
                memcpy(upload.token, data + 3, 8);
                upload.length = size; upload.crc = crc; upload.received = 0; upload.state = 1;
                upload.peer_converged = 0;
            }
        }
    } else if (same) {
        if (op == ERA_WIRE_MACRO_REPLACE_STATUS) status = ERA_WIRE_MACRO_REPLACE_RESULT_OK;
        else if ((op == ERA_WIRE_MACRO_REPLACE_COMMIT && upload.state == 2) ||
                 (op == ERA_WIRE_MACRO_REPLACE_ABORT && upload.state == 3))
            status = ERA_WIRE_MACRO_REPLACE_RESULT_OK;
        else if (upload.state == 1 && era_eeprom_driver_macro_replace_open()) {
            era_nvm_result_t r = ERA_NVM_RESULT_PROTOCOL;
            if (op == ERA_WIRE_MACRO_REPLACE_DATA) {
                uint16_t offset = get16(data + 11);
                uint8_t size = data[13];
                if (size && size <= 17) {
                    r = era_eeprom_driver_macro_data(offset, data + 14, size);
                    if (r == ERA_NVM_RESULT_STAGED && offset == upload.received) upload.received += size;
                }
            } else if (op == ERA_WIRE_MACRO_REPLACE_COMMIT) {
                r = era_eeprom_driver_macro_commit(upload.crc);
                if (r == ERA_NVM_RESULT_OK) upload.state = 2;
            } else if (op == ERA_WIRE_MACRO_REPLACE_ABORT) {
                r = era_eeprom_driver_macro_abort();
                if (r == ERA_NVM_RESULT_OK) upload.state = 3;
            }
            status = result_code(r);
        }
    }
    if (status == ERA_WIRE_MACRO_REPLACE_RESULT_OK) upload.serial = era_eeprom_driver_macro_serial();
    data[31] = status;
    /* DATA occupies bytes 14..30. Do not overwrite its echoed payload. */
    if (op == ERA_WIRE_MACRO_REPLACE_DATA) return true;
    data[23] = same && upload.state == 2 ? upload.peer_converged : 0;
    data[30] = same || op == ERA_WIRE_MACRO_REPLACE_BEGIN ? upload.state : 0;
    data[28] = upload.received >> 8; data[29] = upload.received & 0xFF;
    data[24] = upload.crc >> 24; data[25] = upload.crc >> 16;
    data[26] = upload.crc >> 8; data[27] = upload.crc;
    return true;
}
