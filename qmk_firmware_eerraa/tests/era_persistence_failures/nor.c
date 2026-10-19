#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include "eeprom.h"
#include "keyboards/era/common/storage/era_nvm.c"
#include "keyboards/era/common/storage/era_eeprom_driver.c"
#include "keyboards/era/common/storage/era_eeprom_config_io.h"
#include "keyboards/era/common/system/era_state_sync.h"
#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR 801U
#include "macro_functions.inc"
void raw_hid_send(uint8_t *data,uint8_t size) { (void)data;(void)size; }
static uint8_t physical[ERA_NVM_PHYSICAL_SIZE_BYTES];
static unsigned programs,erases, fail_at;
static unsigned fail_mode,fail_erase_at,erase_mode;
static bool read_fail,fail_all;
static bool flash_init(void *c) { (void)c;return true; }
static bool flash_read(void *c,uint32_t o,void *d,size_t n) { (void)c; assert(o+n<=sizeof(physical)); if(read_fail){read_fail=false;return false;} memcpy(d,physical+o,n);return true; }
static bool flash_program(void *c,uint32_t o,const void *d,size_t n) {
    (void)c;assert(o+n<=sizeof(physical));assert(n<=256 && o%256+n<=256);programs++;
    bool fail=programs==fail_at || fail_all;
    size_t take=fail && fail_mode==0 ? 0 : fail && fail_mode==1 ? n/2 : n;
    for(size_t i=0;i<take;i++) physical[o+i]&=((const uint8_t *)d)[i];
    if(fail && fail_mode==3) {read_fail=true;return true;}
    return !fail;
}
static bool flash_erase(void *c,uint32_t o) { (void)c;assert(o%4096==0);assert(o+4096<=sizeof(physical));erases++;
    bool fail=erases==fail_erase_at;
    size_t take=fail && erase_mode==0 ? 0U : fail && erase_mode==1 ? 2048U : 4096U;
    memset(physical+o,255,take);
    if(fail && erase_mode==3) { read_fail=true;return true; }
    return !fail; }
bool era_nvm_rp2040_flash_bind(era_nvm_flash_t *f) { *f=(era_nvm_flash_t){.init=flash_init,.read=flash_read,.program=flash_program,.erase_sector=flash_erase};return true; }
static void fresh(void) { memset(physical,255,sizeof(physical));programs=erases=fail_at=fail_erase_at=0;read_fail=false;fail_all=false; eeprom_driver_init();assert(eeprom_persistence_ok());era_state_sync_set_revisions_for_testing(1,1,1); }
static void reboot(void) { fail_at=0;read_fail=false;eeprom_driver_init();assert(eeprom_persistence_ok()); }
static void macro_seed(void) { static uint8_t old[16384];memset(old,0x31,sizeof(old));old[16383]=0;assert(era_eeprom_driver_replace(801,old,sizeof(old),ERA_NVM_ORIGIN_REMOTE_APPLY)==ERA_NVM_RESULT_OK); }
static void scoped_factory(void) {
    assert(eeprom_begin_transaction());eeprom_driver_format(false);
    const uint8_t data[]={0xE7,0xFE,4,1,0x19,0xA9};eeprom_write_block(data,(void*)0,sizeof(data));
}
static void ordinary_and_factory_tests(void) {
    fresh();unsigned before=programs;assert(eeprom_begin_transaction());assert(eeprom_begin_transaction());
    eeprom_update_byte((void*)4,0x42);assert(eeprom_read_byte((void*)4)==0x42);assert(programs==before);
    assert(eeprom_end_transaction());assert(programs==before);assert(eeprom_end_transaction());assert(programs==before+2);
    assert(era_state_sync_config_revision()==2);reboot();assert(eeprom_read_byte((void*)4)==0x42);
    fresh();uint8_t old[6]={0xE7,0xFE,3,2,0x17,0x35};eeprom_update_block(old,(void*)0,6);
    static uint8_t seeded[sizeof(physical)];memcpy(seeded,physical,sizeof(seeded));
    scoped_factory();before=programs;assert(eeprom_end_transaction());unsigned count=programs-before;assert(count==99);
    unsigned cases=0;
    for(unsigned cut=1;cut<=count;cut++) for(unsigned mode=0;mode<4;mode++) {
        memcpy(physical,seeded,sizeof(seeded));reboot();before=programs;fail_at=before+cut;fail_mode=mode;
        scoped_factory();assert(!eeprom_end_transaction());assert(!eeprom_persistence_ok());
        assert(!memcmp(s_era_eeprom_nvm.image,old,sizeof(old)));assert(s_era_eeprom_nvm.tail_sealed);
        fail_at=0;read_fail=false;uint8_t recovered[6];assert(era_eeprom_driver_replay_read(0,recovered,6)==ERA_NVM_RESULT_OK);
        const uint8_t candidate[]={0xE7,0xFE,4,1,0x19,0xA9};
        assert(!memcmp(recovered,old,6)||!memcmp(recovered,candidate,6));
        assert(eeprom_begin_transaction());eeprom_update_block(old,(void*)0,6);assert(eeprom_end_transaction());
        reboot();assert(!memcmp(s_era_eeprom_nvm.image,old,6));cases++;
    }
    printf("PASS nested scope and factory rotation: %u program fault cases, public rollback, replay old-or-new, equal retry repairs\n",cases);
}
static void journal_fault_tests(void) {
    fresh();const uint8_t old=0x35,newv=0x67;eeprom_update_byte((void*)4,old);
    static uint8_t seed[sizeof(physical)];memcpy(seed,physical,sizeof(seed));unsigned cases=0;
    for(unsigned cut=1;cut<=2;cut++) for(unsigned mode=0;mode<4;mode++) {
        memcpy(physical,seed,sizeof(seed));reboot();unsigned p=programs;fail_at=p+cut;fail_mode=mode;
        assert(eeprom_begin_transaction());eeprom_update_byte((void*)4,newv);assert(!eeprom_end_transaction());
        assert(eeprom_read_byte((void*)4)==old);assert(!eeprom_persistence_ok());assert(s_era_eeprom_nvm.tail_sealed);
        fail_at=0;read_fail=false;uint8_t actual;assert(era_eeprom_driver_replay_read(4,&actual,1)==ERA_NVM_RESULT_OK);assert(actual==old||actual==newv);
        p=programs;eeprom_update_byte((void*)4,old);assert(eeprom_persistence_ok());assert(programs>=p+99);
        reboot();assert(eeprom_read_byte((void*)4)==old);cases++;
    }
    printf("PASS packed journal: %u fault cases, ambiguous final commit and same-old-value receipt requires checked rotation\n",cases);
}
static void macro_tests(void) {
    fresh();macro_seed();uint8_t open=255,close=0,payload=0x77;nvm_dynamic_keymap_macro_update_buffer(16383,1,&open);nvm_dynamic_keymap_macro_update_buffer(0,1,&payload);
    assert(era_state_sync_macro_revision()==3);
    assert(era_eeprom_driver_replace(801,&payload,1,ERA_NVM_ORIGIN_REMOTE_APPLY)==ERA_NVM_RESULT_BUSY);
    assert(eeprom_begin_transaction());eeprom_update_byte((void*)4,0x71);eeprom_update_byte((void*)18000,0x72);
    assert(!eeprom_end_transaction());assert(eeprom_read_byte((void*)4)==0 && eeprom_read_byte((void*)18000)==0);assert(eeprom_read_byte((void*)801)==payload);
    // Seal a journal by a rejected unrelated append, then save a setting. Rotation must preserve the durable macro.
    fail_at=programs+1;fail_mode=0;eeprom_update_byte((void*)4,0x19);assert(!eeprom_persistence_ok());fail_at=0;
    eeprom_update_byte((void*)4,0x19);assert(eeprom_persistence_ok());assert(eeprom_read_byte((void*)801)==payload && eeprom_read_byte((void*)17184)==255);
    uint8_t durable[2];assert(era_eeprom_driver_replay_read(801,durable,2)==ERA_NVM_RESULT_OK);assert(durable[0]==0x31);
    nvm_dynamic_keymap_macro_update_buffer(16383,1,&close);assert(eeprom_persistence_ok());assert(era_state_sync_macro_revision()==4);reboot();assert(eeprom_read_byte((void*)801)==payload);
    nvm_dynamic_keymap_macro_reset();assert(eeprom_persistence_ok());reboot();for(unsigned i=801;i<17185;i++) assert(eeprom_read_byte((void*)(uintptr_t)i)==0);
    puts("PASS macro staging, envelope crossing refusal, unrelated-save rotation, Apply exclusion, CLOSE and stock RESET");
}
static void erase_tests(void) {
    fresh();const uint8_t old=0x31;eeprom_update_byte((void*)4,old);
    // Make the inactive bank dirty, without creating a valid competitor.
    memset(physical+65536,0x13,65536);reboot();
    static uint8_t seed[sizeof(physical)];memcpy(seed,physical,sizeof(seed));unsigned cases=0;
    for(unsigned cut=1;cut<=16;cut++)for(unsigned mode=0;mode<4;mode++) {
        memcpy(physical,seed,sizeof(seed));reboot();fail_erase_at=erases+cut;erase_mode=mode;
        assert(eeprom_begin_transaction());eeprom_driver_format(false);eeprom_update_byte((void*)4,0x72);assert(!eeprom_end_transaction());
        assert(eeprom_read_byte((void*)4)==old);fail_erase_at=0;read_fail=false;
        uint8_t recovered;assert(era_eeprom_driver_replay_read(4,&recovered,1)==ERA_NVM_RESULT_OK && recovered==old);
        eeprom_update_byte((void*)4,old);assert(eeprom_persistence_ok());reboot();assert(eeprom_read_byte((void*)4)==old);cases++;
    }
    memcpy(physical,seed,sizeof(seed));reboot();unsigned before=erases;bool did;
    assert(era_eeprom_driver_maintenance_task(&did)==ERA_NVM_RESULT_OK && did && erases==before+1);
    fail_erase_at=erases+1;erase_mode=1;uint8_t cursor=s_era_eeprom_nvm.inactive_erase_sector;
    assert(era_eeprom_driver_maintenance_task(&did)==ERA_NVM_RESULT_IO_ERROR && !did && s_era_eeprom_nvm.inactive_erase_sector==cursor);
    fail_erase_at=0;assert(era_eeprom_driver_maintenance_task(&did)==ERA_NVM_RESULT_OK && did);
    printf("PASS factory rotation %u sector-erase/readback fault cases; old bank/public prefix preserved, equal repair; maintenance at most one sector and failed cursor retained\n",cases);
}
static void empty_macro_probe(void) {
    fresh();uint8_t open=255,close=0,zeros[28]={0};
    nvm_dynamic_keymap_macro_update_buffer(16383,1,&open);
    for (unsigned offset=0;offset<16383;offset+=28) { unsigned take=16383-offset<28?16383-offset:28; nvm_dynamic_keymap_macro_update_buffer(offset,take,zeros); }
    bool payload_seen=s_era_eeprom_nvm.macro_payload_seen;
    nvm_dynamic_keymap_macro_update_buffer(16383,1,&close);
    printf("PASS equal zero macro payload accepted: payload_seen=%u CLOSE_receipt=%u open=%u durable_old_zero=1\n",payload_seen,eeprom_persistence_ok(),era_eeprom_driver_macro_transaction_open());
    assert(payload_seen && eeprom_persistence_ok() && !era_eeprom_driver_macro_transaction_open());
    /* Same bytes without RESET must still count as received payload. */
    nvm_dynamic_keymap_macro_update_buffer(16383,1,&open);
    nvm_dynamic_keymap_macro_update_buffer(0,16,zeros);
    nvm_dynamic_keymap_macro_update_buffer(16383,1,&close);
    assert(eeprom_persistence_ok() && !era_eeprom_driver_macro_transaction_open());
}
static void semantic_envelope_probe(void) {
    fresh();const uint8_t left=0x12,right=0x34;
    era_state_sync_note_config_semantic_commit(0,88);era_state_sync_note_config_semantic_commit(172,24);
    uint32_t before=era_state_sync_config_revision();
    assert(eeprom_begin_transaction());
    assert(era_eeprom_update_config(&left,0,1)==1);assert(era_eeprom_update_config(&right,172,1)==1);
    assert(eeprom_end_transaction());
    uint32_t after=era_state_sync_config_revision();
    printf("PASS CONFIG SET for TapDance+timing then compound SAVE: revision %lu -> %lu (expected unchanged), durable bytes %u/%u\n",(unsigned long)before,(unsigned long)after,eeprom_read_byte((void*)37),eeprom_read_byte((void*)209));
    assert(after==before);
}
static void empty_reset_transcript(void) {
    fresh(); uint8_t open=255, close=0;
    nvm_dynamic_keymap_macro_update_buffer(16383,1,&close);
    assert(!eeprom_persistence_ok()); // raw CLOSE is still refused
    nvm_dynamic_keymap_macro_update_buffer(16383,1,&open);
    nvm_dynamic_keymap_macro_update_buffer(16383,1,&close);
    assert(!eeprom_persistence_ok()); // no received payload or RESET proof
    nvm_dynamic_keymap_macro_reset(); assert(eeprom_persistence_ok());
    unsigned before=programs;
    nvm_dynamic_keymap_macro_reset(); assert(eeprom_persistence_ok());
    assert(programs==before); // an already empty RESET is not another record
    nvm_dynamic_keymap_macro_update_buffer(16383,1,&open);
    nvm_dynamic_keymap_macro_update_buffer(16383,1,&close);
    assert(eeprom_persistence_ok() && !era_eeprom_driver_macro_transaction_open());
    nvm_dynamic_keymap_macro_update_buffer(16383,1,&open);
    nvm_dynamic_keymap_macro_update_buffer(16383,1,&close);
    assert(!eeprom_persistence_ok()); // RESET proof cannot be reused
    nvm_dynamic_keymap_macro_reset(); assert(eeprom_persistence_ok());
    /* A remount retires the RAM proof, even when the same engine is reused. */
    assert(era_nvm_mount(&s_era_eeprom_nvm)==ERA_NVM_RESULT_OK);
    nvm_dynamic_keymap_macro_update_buffer(16383,1,&open);
    nvm_dynamic_keymap_macro_update_buffer(16383,1,&close);
    assert(!eeprom_persistence_ok());
    puts("PASS zero-slot RESET upload, no-op RESET cost, raw CLOSE and stale-proof rejection");
}
static void public_macro_transcript(void) {
    fresh(); macro_seed(); era_state_sync_set_revisions_for_testing(1, 1, 1);
    uint8_t open = 255, close = 0, payload = 0x51, visible;
    nvm_dynamic_keymap_macro_update_buffer(16383, 1, &open);
    assert(era_state_sync_macro_revision() == 2);
    nvm_dynamic_keymap_macro_update_buffer(16383, 1, &open);
    assert(era_state_sync_macro_revision() == 2); // equal opener
    nvm_dynamic_keymap_macro_update_buffer(0, 1, &payload);
    nvm_dynamic_keymap_macro_read_buffer(0, 1, &visible);
    assert(visible == payload && era_state_sync_macro_revision() == 3);
    nvm_dynamic_keymap_macro_update_buffer(0, 1, &payload);
    assert(era_state_sync_macro_revision() == 3); // equal payload remains receipt evidence
    fail_at = programs + 1; fail_mode = 0;
    nvm_dynamic_keymap_macro_update_buffer(16383, 1, &close);
    assert(!eeprom_persistence_ok() && era_state_sync_macro_revision() == 3);
    nvm_dynamic_keymap_macro_read_buffer(16383, 1, &visible); assert(visible == 255);
    fail_at = 0;
    nvm_dynamic_keymap_macro_update_buffer(16383, 1, &close);
    nvm_dynamic_keymap_macro_read_buffer(16383, 1, &visible);
    assert(eeprom_persistence_ok() && visible == 0 && era_state_sync_macro_revision() == 4);
    // An ordinary public 16-byte read aborts a partial RESET transcript.
    // It must not be mistaken for the next private update_block scan chunk.
    uint8_t zeros[16] = {0}, query[16];
    eeprom_update_block(zeros, (void *)801, sizeof(zeros));
    nvm_dynamic_keymap_macro_read_buffer(16, sizeof(query), query);
    nvm_dynamic_keymap_macro_read_buffer(16383, 1, &visible);
    assert(visible == 0);
    // A large snapshot must likewise copy the restored marker, not the
    // invalid marker that existed before its read aborted the RESET scan.
    static uint8_t snapshot[16384];
    eeprom_update_block(zeros, (void *)801, sizeof(zeros));
    nvm_dynamic_keymap_macro_read_buffer(0, sizeof(snapshot), snapshot);
    assert(snapshot[16383] == 0);
    // Overflowing offsets fill safely, and the end is clipped before addition.
    uint8_t end[3] = {1, 1, 1};
    nvm_dynamic_keymap_macro_read_buffer(UINT32_MAX, 3, end);
    assert(end[0] == 0 && end[1] == 0 && end[2] == 0);
    nvm_dynamic_keymap_macro_read_buffer(16383, 3, end);
    assert(end[0] == 0 && end[1] == 0 && end[2] == 0);
    puts("PASS public NOR opener/payload/no-op/failure/retry revisions and bounded reads");
}
int main(void) { public_macro_transcript();ordinary_and_factory_tests();journal_fault_tests();macro_tests();erase_tests();empty_macro_probe();semantic_envelope_probe();empty_reset_transcript();return 0; }
