// Copyright 2018-2022 QMK
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
/* Nested synchronous logical operations; only the outer end is a durability
 * result. Backends without transactions retain their physical write ordering. */
bool eeprom_begin_transaction(void);
bool eeprom_end_transaction(void);
bool eeprom_persistence_ok(void);
bool eeprom_transaction_active(void);
void eeprom_transaction_complete(bool success);

#if defined(__AVR__) && !defined(EEPROM_DRIVER)
#    include <avr/eeprom.h>
void eeprom_native_note_result(bool success);
bool eeprom_prepare_commit(uint8_t *address, uint8_t invalid);
bool eeprom_commit_byte(uint8_t *address, uint8_t value);
bool eeprom_commit_failed(void);
/* avr-libc has a void write API. Verify physical bytes after EEPE clears so
 * the common durability receipt cannot acknowledge a dropped programming. */
#    define EEPROM_NATIVE_CHECKED(suffix, type) \
        static inline void eeprom_checked_write_##suffix(type *p, type value) { \
            eeprom_write_##suffix(p, value); eeprom_busy_wait(); \
            eeprom_native_note_result(eeprom_read_##suffix(p) == value); \
        } \
        static inline void eeprom_checked_update_##suffix(type *p, type value) { \
            eeprom_update_##suffix(p, value); eeprom_busy_wait(); \
            eeprom_native_note_result(eeprom_read_##suffix(p) == value); \
        }
EEPROM_NATIVE_CHECKED(byte, uint8_t)
EEPROM_NATIVE_CHECKED(word, uint16_t)
EEPROM_NATIVE_CHECKED(dword, uint32_t)
#    undef EEPROM_NATIVE_CHECKED
static inline void eeprom_native_verify_block(const void *source, const void *address, size_t length) {
    const uint8_t *data = source, *target = address;
    bool ok = true;
    eeprom_busy_wait();
    for (size_t i = 0; i < length; ++i) ok = (eeprom_read_byte(target + i) == data[i]) && ok;
    eeprom_native_note_result(ok);
}
static inline void eeprom_checked_write_block(const void *source, void *address, size_t length) {
    eeprom_write_block(source, address, length);
    eeprom_native_verify_block(source, address, length);
}
static inline void eeprom_checked_update_block(const void *source, void *address, size_t length) {
    eeprom_update_block(source, address, length);
    eeprom_native_verify_block(source, address, length);
}
#    define eeprom_write_byte eeprom_checked_write_byte
#    define eeprom_write_word eeprom_checked_write_word
#    define eeprom_write_dword eeprom_checked_write_dword
#    define eeprom_write_block eeprom_checked_write_block
#    define eeprom_update_byte eeprom_checked_update_byte
#    define eeprom_update_word eeprom_checked_update_word
#    define eeprom_update_dword eeprom_checked_update_dword
#    define eeprom_update_block eeprom_checked_update_block
#else
#    include <stdint.h>
#    include <stdlib.h>

uint8_t  eeprom_read_byte(const uint8_t *__p);
uint16_t eeprom_read_word(const uint16_t *__p);
uint32_t eeprom_read_dword(const uint32_t *__p);
void     eeprom_read_block(void *__dst, const void *__src, size_t __n);
void     eeprom_write_byte(uint8_t *__p, uint8_t __value);
void     eeprom_write_word(uint16_t *__p, uint16_t __value);
void     eeprom_write_dword(uint32_t *__p, uint32_t __value);
void     eeprom_write_block(const void *__src, void *__dst, size_t __n);
void     eeprom_update_byte(uint8_t *__p, uint8_t __value);
void     eeprom_update_word(uint16_t *__p, uint16_t __value);
void     eeprom_update_dword(uint32_t *__p, uint32_t __value);
void     eeprom_update_block(const void *__src, void *__dst, size_t __n);
#endif

// While newer avr-libc versions may have an implementation
//   use preprocessor as to not cause conflicts
#undef eeprom_write_qword
#define eeprom_write_qword(__p, __value)                  \
    do {                                                  \
        uint64_t tmp = __value;                           \
        eeprom_update_block(&tmp, __p, sizeof(uint64_t)); \
    } while (0)

#if defined(EEPROM_CUSTOM)
#    ifndef EEPROM_SIZE
#        error EEPROM_SIZE has not been defined for custom driver.
#    endif
#    define TOTAL_EEPROM_BYTE_COUNT (EEPROM_SIZE)
#elif defined(EEPROM_WEAR_LEVELING)
#    include "wear_leveling_drivers.h"
#    define TOTAL_EEPROM_BYTE_COUNT (WEAR_LEVELING_LOGICAL_SIZE)
#elif defined(EEPROM_TRANSIENT)
#    include "eeprom_transient.h"
#    define TOTAL_EEPROM_BYTE_COUNT (TRANSIENT_EEPROM_SIZE)
#elif defined(EEPROM_I2C)
#    include "eeprom_i2c.h"
#    define TOTAL_EEPROM_BYTE_COUNT (EXTERNAL_EEPROM_BYTE_COUNT)
#elif defined(EEPROM_SPI)
#    include "eeprom_spi.h"
#    define TOTAL_EEPROM_BYTE_COUNT (EXTERNAL_EEPROM_BYTE_COUNT)
#elif defined(EEPROM_STM32_L0_L1)
#    include "eeprom_stm32_L0_L1.h"
#    define TOTAL_EEPROM_BYTE_COUNT (STM32_ONBOARD_EEPROM_SIZE)
#elif defined(EEPROM_KINETIS_FLEXRAM)
#    include "eeprom_kinetis_flexram.h"
#    define TOTAL_EEPROM_BYTE_COUNT (EEPROM_SIZE)
#elif defined(EEPROM_LEGACY_EMULATED_FLASH)
#    include "eeprom_legacy_emulated_flash_defs.h"
#    define TOTAL_EEPROM_BYTE_COUNT (FEE_DENSITY_BYTES)
#elif defined(EEPROM_SAMD)
#    include "eeprom_samd.h"
#    define TOTAL_EEPROM_BYTE_COUNT (EEPROM_SIZE)
#elif defined(__AVR_ATmega16U2__) || defined(__AVR_ATmega16U4__) || defined(__AVR_AT90USB162__) || defined(__AVR_ATtiny85__)
#    define TOTAL_EEPROM_BYTE_COUNT 512
#elif defined(__AVR_ATmega32U2__) || defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega328P__) || defined(__AVR_ATmega32A__)
#    define TOTAL_EEPROM_BYTE_COUNT 1024
#elif defined(__AVR_AT90USB646__) || defined(__AVR_AT90USB647__)
#    define TOTAL_EEPROM_BYTE_COUNT 2048
#elif defined(__AVR_AT90USB1286__) || defined(__AVR_AT90USB1287__)
#    define TOTAL_EEPROM_BYTE_COUNT 4096
#elif defined(EEPROM_TEST_HARNESS)
#    ifndef LEGACY_FLASH_OPS_MOCKED
// Normal tests
#        define TOTAL_EEPROM_BYTE_COUNT 32
#    else
// Flash wear-leveling testing
#        include "eeprom_legacy_emulated_flash_tests.h"
#        define TOTAL_EEPROM_BYTE_COUNT (EEPROM_SIZE)
#    endif
#else
#    error Unknown EEPROM driver.
#endif
