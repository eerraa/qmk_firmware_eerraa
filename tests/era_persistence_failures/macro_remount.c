// SPDX-License-Identifier: GPL-2.0-or-later
// Physical EEPROM/NOR and MCU cold RAM are modeled; callers/adapters are real.
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <setjmp.h>
#include <stdlib.h>
#include "quantum.h"
#include "via.h"
#include "dynamic_keymap.h"
#include "matrix.h"
#include "nvm_dynamic_keymap.h"
#include "nvm_via.h"
#include "eeprom.h"

static uint8_t response[32];
static unsigned program_calls, erase_calls;
static bool fail_program;
static uintptr_t failed_address = UINTPTR_MAX;
static unsigned cut_after;
static jmp_buf power_cut;
#define MACRO_CHECK(condition, label) do { if (!(condition)) { fprintf(stderr, "MACRO_ASSERT_%s\n", label); exit(91); } } while (0)

#ifdef __AVR__
uint8_t native_storage[1024];
void macro_test_native_write(void *address, const void *data, size_t n, bool update) {
    uintptr_t a = (uintptr_t)address;
    const uint8_t *bytes = data;
    assert(a + n <= sizeof(native_storage));
    for (size_t i = 0; i < n; i++) {
        if (update && native_storage[a + i] == bytes[i]) continue;
        program_calls++;
        if (!fail_program && a + i != failed_address) native_storage[a + i] = bytes[i];
        if (cut_after && program_calls == cut_after) longjmp(power_cut, 1);
    }
}
// Native receipt declarations/functions are extracted intact from eeconfig.c.
#include "native-receipt.inc"
#else
#include "eeprom_driver.h"
#include "wear_leveling.h"
#include "wear_leveling_internal.h"
#include "keyboards/era/common/system/era_state_sync.h"
static backing_store_int_t physical[WEAR_LEVELING_BACKING_SIZE / BACKING_STORE_WRITE_SIZE];
bool backing_store_init(void) { return true; }
bool backing_store_unlock(void) { return true; }
bool backing_store_lock(void) { return true; }
bool backing_store_erase(void) {
    erase_calls++;
    if (fail_program) return false;
    memset(physical, 0, sizeof(physical));
    return true;
}
bool backing_store_read(uint32_t a, backing_store_int_t *v) {
    assert(a % BACKING_STORE_WRITE_SIZE == 0 && a + sizeof(*v) <= sizeof(physical));
    *v = physical[a / BACKING_STORE_WRITE_SIZE];
    return true;
}
bool backing_store_write(uint32_t a, backing_store_int_t v) {
    assert(a % BACKING_STORE_WRITE_SIZE == 0 && a + sizeof(v) <= sizeof(physical));
    program_calls++;
    if (fail_program) return false;
    physical[a / BACKING_STORE_WRITE_SIZE] |= v;
    if (cut_after && program_calls == cut_after) longjmp(power_cut, 1);
    return physical[a / BACKING_STORE_WRITE_SIZE] == v;
}
#endif

#ifdef __AVR__
void eeprom_transaction_complete(bool ok) { (void)ok; }
#endif
void raw_hid_send(uint8_t *d, uint8_t n) { assert(n == 32); memcpy(response, d, n); }
uint16_t timer_read(void) { return 0; }
uint16_t timer_elapsed(uint16_t t) { return (uint16_t)-t; }
uint32_t timer_read32(void) { return 0; }
void clear_keyboard(void) {}
void eeconfig_init(void) {}
void via_set_device_indication(uint8_t v) { (void)v; }
void via_custom_value_command_kb(uint8_t *d, uint8_t n) { (void)n; d[0] = id_unhandled; }
#ifdef __AVR__
bool via_command_kb(uint8_t *d, uint8_t n) { (void)d; (void)n; return false; }
#endif
uint16_t keycode_at_keymap_location_raw(uint8_t l, uint8_t r, uint8_t c) { (void)l; (void)r; (void)c; return 4; }

// Includes the entire production storage module, including its new static state.
#include "macro-owner.c"
#include "dynamic-owner.inc"
#include "via-owner.inc"

static void packet(uint8_t command, uint16_t offset, uint8_t count, const uint8_t *data) {
    uint8_t p[32] = {command, offset >> 8, offset & 255, count};
    if (data && count) memcpy(p + 4, data, count);
    raw_hid_receive(p, sizeof(p));
}
static void opener(void) { uint8_t v = 255; packet(15, EXPECTED_SIZE - 1, 1, &v); }
static void close_upload(void) { uint8_t v = 0; packet(15, EXPECTED_SIZE - 1, 1, &v); }
static void reset_upload(void) {
    uint8_t p[32] = {16};
    raw_hid_receive(p, sizeof(p));
}
static uint8_t marker(void) {
    uint8_t v;
    dynamic_keymap_macro_get_buffer(EXPECTED_SIZE - 1, 1, &v);
    return v;
}
static uint8_t physical_marker(void) {
#ifdef __AVR__
    return native_storage[DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + EXPECTED_SIZE - 1];
#else
    // A read from a fresh replay, restoring the current desired live RAM after it.
    uint8_t v;
    extern uint8_t macro_test_cold_byte(uint32_t address);
    v = macro_test_cold_byte(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + EXPECTED_SIZE - 1);
    return v;
#endif
}
static void cold_mount(void) {
    cut_after = 0;
    fail_program = false;
    failed_address = UINTPTR_MAX;
#ifdef __AVR__
    eeprom_native_depth = 0;
    eeprom_native_failed = false;
    eeprom_native_ok = true;
    eeprom_native_commit_rejected = false;
    nvm_dynamic_keymap_macro_reset_session();
#else
    // The actual adapter init calls the production QMK upload reset hook.
    eeprom_driver_init();
#endif
    assert(!dynamic_keymap_macro_upload_open && !dynamic_keymap_macro_payload_seen && !dynamic_keymap_macro_empty_reset);
#ifdef __AVR__
    assert(!dynamic_keymap_macro_upload_failed);
#endif
    assert(eeprom_persistence_ok());
    assert(eeprom_read_word((const uint16_t *)0) == 0xFEE7);
    assert(via_eeprom_is_valid());
}
static void fresh(void) {
    cut_after = 0;
    fail_program = false;
    failed_address = UINTPTR_MAX;
#ifdef __AVR__
    memset(native_storage, 0, sizeof(native_storage));
    eeprom_native_depth = 0;
    eeprom_native_failed = false;
    eeprom_native_ok = true;
    eeprom_native_commit_rejected = false;
    nvm_dynamic_keymap_macro_reset_session();
#else
    memset(physical, 0, sizeof(physical));
    eeprom_driver_init();
#endif
    eeprom_update_word((uint16_t *)0, 0xFEE7);
    via_eeprom_set_valid(true);
    assert(eeprom_persistence_ok());
    program_calls = erase_calls = 0;
}
static void payload(const uint8_t *data, size_t n) {
    for (size_t offset = 0; offset < n; offset += 28) {
        size_t count = n - offset < 28 ? n - offset : 28;
        packet(15, offset, count, data + offset);
        assert(response[0] == 15);
    }
}
static void verify_bytes(const uint8_t *data, size_t n) {
    uint8_t readback[28];
    for (size_t offset = 0; offset < n; offset += sizeof(readback)) {
        size_t count = n - offset < sizeof(readback) ? n - offset : sizeof(readback);
        dynamic_keymap_macro_get_buffer(offset, count, readback);
        assert(memcmp(readback, data + offset, count) == 0);
    }
}
static void stock_cases(void) {
    static uint8_t bytes[EXPECTED_SIZE - 1];
    for (unsigned kind = 0; kind < 6; kind++) {
        fresh(); reset_upload(); assert(response[0] == 16);
        opener(); assert(response[0] == 15);
        size_t n = kind == 0 ? 0 : kind == 1 ? 16 : kind == 2 ? 18 : EXPECTED_SIZE - 1;
        memset(bytes, 0, sizeof(bytes));
        if (kind == 2) { bytes[1] = 'A'; bytes[2] = 0; bytes[4] = 'B'; }
        if (kind >= 3) { memset(bytes, 'X', n); bytes[n - 1] = 0; }
        payload(bytes, n);
        packet(15, 0, 0, NULL); assert(response[0] == 15); // explicit zero-byte no-op
        close_upload(); assert(response[0] == 15 && eeprom_persistence_ok() && marker() == 0);
        cold_mount(); verify_bytes(bytes, n); assert(marker() == 0);
    }
    puts("PASS Stock RESET/opener/full/16-empty/zero-slots/zero-byte/maximum/equal payload cold readback");
}
static void cold_failure(void) {
    fresh(); reset_upload(); opener();
    const uint8_t desired[2] = {88, 89};
#ifdef __AVR__
    failed_address = DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + 1;
#else
    fail_program = true;
#endif
    packet(15, 0, 2, desired); assert(response[0] == id_unhandled && !eeprom_persistence_ok());
    cold_mount(); assert(marker() == 255);
    unsigned before = program_calls, erased = erase_calls;
    close_upload();
    assert(response[0] == id_unhandled); // cold session cannot activate lost/torn intent
    assert(program_calls == before && erase_calls == erased && physical_marker() == 255);
    opener(); assert(response[0] == 15);
    close_upload(); assert(response[0] == id_unhandled); // opener alone is not complete upload
    assert(physical_marker() == 255);
    uint8_t complete[18] = {88, 89};
    opener(); payload(complete, sizeof(complete)); close_upload();
    assert(response[0] == 15 && eeprom_persistence_ok());
    cold_mount(); verify_bytes(complete, sizeof(complete)); assert(marker() == 0);
    puts("PASS failed no-effect/torn upload cold RAM reset refuses raw CLOSE and opener-only; complete retry durable");
}
static void reset_proof(void) {
    fresh(); reset_upload(); assert(response[0] == 16);
    opener(); close_upload(); assert(response[0] == 15 && eeprom_persistence_ok());
    opener(); close_upload(); assert(response[0] == id_unhandled); // empty proof is one upload
    reset_upload(); assert(response[0] == 16);
    cold_mount(); opener(); close_upload(); assert(response[0] == id_unhandled); // new RAM generation
    reset_upload(); opener(); const uint8_t nonempty[2] = {65, 0}; payload(nonempty, 2); close_upload();
    fail_program = true; reset_upload(); assert(response[0] == id_unhandled);
    fail_program = false; opener(); assert(response[0] == 15);
    close_upload(); assert(response[0] == id_unhandled); // failed RESET never grants empty proof
    reset_upload(); opener(); close_upload(); assert(response[0] == 15);
#ifndef __AVR__
    reset_upload(); eeprom_driver_erase(); opener(); close_upload(); assert(response[0] == id_unhandled);
#endif
    puts("PASS successful RESET empty proof consumed; cold/failed RESET/erase cannot reuse it");
}
static void live_recovery(void) {
    uint8_t desired[28]; memset(desired, 90, sizeof(desired));
    for (unsigned other_owner = 0; other_owner < 2; other_owner++) {
        fresh(); reset_upload(); opener(); fail_program = true;
        packet(15, 0, sizeof(desired), desired); assert(response[0] == id_unhandled);
        fail_program = false;
#ifdef __AVR__
        // Another owner's opener clears the generic native fence, not macro proof.
        assert(eeprom_begin_transaction());
        assert(eeprom_prepare_commit((uint8_t *)30, 255));
        assert(eeprom_commit_byte((uint8_t *)30, 1));
        assert(eeprom_end_transaction()); assert(eeprom_persistence_ok());
        close_upload(); assert(response[0] == id_unhandled);
        opener(); close_upload(); assert(response[0] == id_unhandled);
        opener(); payload(desired, sizeof(desired)); close_upload(); assert(response[0] == 15);
#else
        if (other_owner) eeprom_update_byte((uint8_t *)30, 1);
        close_upload(); assert(response[0] == 15 && eeprom_persistence_ok());
#endif
        cold_mount(); verify_bytes(desired, sizeof(desired)); assert(marker() == 0);
    }
    puts("PASS live WL full retained repair; native owner failure survives unrelated receipt until complete fresh upload");
}
static void repaired_close(void) {
#ifndef __AVR__
    fresh(); reset_upload(); opener();
    const uint8_t accepted[2] = {65, 0}, replacement[2] = {66, 0};
    payload(accepted, sizeof(accepted));
    fail_program = true; close_upload(); assert(response[0] == id_unhandled);
    fail_program = false; eeprom_update_byte((uint8_t *)30, 1);
    assert(eeprom_persistence_ok() && marker() == 0);
    uint32_t revision = era_state_sync_macro_revision();
    unsigned before = program_calls, erased = erase_calls;
    packet(15, 0, sizeof(replacement), replacement);
    MACRO_CHECK(response[0] == id_unhandled, "REPAIRED_CLOSE_REJECTS_PAYLOAD");
    assert(program_calls == before && erase_calls == erased);
    assert(era_state_sync_macro_revision() == revision + 1);
    cold_mount(); verify_bytes(accepted, sizeof(accepted)); assert(marker() == 0);
    puts("PASS other-owner repaired CLOSE seals payload until a new opener");
#endif
}
static void combined_opener(void) {
    const uint8_t open_packet[3] = {65, 66, 255}, old_tail[2] = {0, 0};
    fresh(); reset_upload();
    unsigned before = program_calls;
    packet(15, EXPECTED_SIZE - 3, sizeof(open_packet), open_packet);
    assert(response[0] == 15 && marker() == 255);
    unsigned programs = program_calls - before;
    assert(programs != 0);
    for (volatile unsigned cut = 1; cut <= programs; ++cut) {
        fresh(); reset_upload(); cut_after = program_calls + cut;
        if (!setjmp(power_cut)) packet(15, EXPECTED_SIZE - 3, sizeof(open_packet), open_packet);
        cold_mount();
        uint8_t tail[2]; dynamic_keymap_macro_get_buffer(EXPECTED_SIZE - 3, sizeof(tail), tail);
        MACRO_CHECK(marker() != 0 || memcmp(tail, old_tail, sizeof(tail)) == 0, "OPENER_INVALIDATES_BEFORE_PAYLOAD");
    }
    printf("PASS combined opener marker first at every modeled physical program cut=%u\n", programs);
}
static void duplicate_close(void) {
    fresh(); reset_upload(); opener();
    const uint8_t accepted[2] = {65, 0}; payload(accepted, sizeof(accepted));
    close_upload(); assert(response[0] == 15 && marker() == 0);
#ifndef __AVR__
    uint32_t revision = era_state_sync_macro_revision();
#endif
    unsigned before = program_calls, erased = erase_calls;
    close_upload(); MACRO_CHECK(response[0] == id_unhandled, "DUPLICATE_CLOSE_NEEDS_CURRENT_OPEN_TRANSCRIPT");
    assert(program_calls == before && erase_calls == erased);
#ifndef __AVR__
    assert(era_state_sync_macro_revision() == revision + 1);
#endif
    cold_mount(); verify_bytes(accepted, sizeof(accepted));
    before = program_calls; erased = erase_calls;
    close_upload(); MACRO_CHECK(response[0] == id_unhandled, "DUPLICATE_CLOSE_NEEDS_CURRENT_OPEN_TRANSCRIPT");
    assert(program_calls == before && erase_calls == erased);
    puts("PASS strict duplicate CLOSE rejection in live/cold session preserves physical data/write counts and notifies hidden validity");
}
int main(int argc, char **argv) {
#ifdef VIA_EEPROM_ALLOW_RESET
#    error This fixture must exercise the actual supported flag with VIA_EEPROM_ALLOW_RESET absent
#endif
    assert(dynamic_keymap_macro_get_buffer_size() == EXPECTED_SIZE);
    if (argc < 2 || strcmp(argv[1], "stock") == 0) stock_cases();
    if (argc < 2 || strcmp(argv[1], "cold") == 0) cold_failure();
    if (argc < 2 || strcmp(argv[1], "reset") == 0) reset_proof();
    if (argc < 2 || strcmp(argv[1], "live") == 0) live_recovery();
    if (argc < 2 || strcmp(argv[1], "sealed") == 0) repaired_close();
    if (argc < 2 || strcmp(argv[1], "combined") == 0) combined_opener();
    if (argc < 2 || strcmp(argv[1], "duplicate") == 0) duplicate_close();
    printf("PASS actual geometry macro_size=%u native=%u; production owner/raw caller/adapters\n", EXPECTED_SIZE,
#ifdef __AVR__
           1U
#else
           0U
#endif
    );
    return 0;
}
