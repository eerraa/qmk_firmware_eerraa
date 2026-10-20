#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the actual timing keymap with deterministic clock/NVM counters."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'keyboards/era/sirind/tomak/keymaps/sync_timing/keymap.c').read_text()
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define KC_F24 115
typedef struct { struct { bool pressed; } event; } keyrecord_t;
typedef struct { uint32_t program_count, erase_count; } era_nvm_diagnostics_t;
typedef struct { uint32_t retry_count, complete_count, abort_count; } era_host_peer_storage_diagnostics_t;
static uint32_t now, gen;
static era_nvm_diagnostics_t nvm;
static era_host_peer_storage_diagnostics_t storage;
static uint32_t timer_read32(void) { return now; }
static uint32_t timer_elapsed32(uint32_t t) { return now-t; }
static void era_eeprom_driver_get_nvm_diagnostics(era_nvm_diagnostics_t *p) { *p=nvm; }
static void era_host_peer_storage_get_diagnostics_snapshot(era_host_peer_storage_diagnostics_t *p) { *p=storage; }
static uint32_t era_eeprom_driver_timing_generation(void) { return gen; }
static void uprintf(const char *fmt, ...) {
    char out[256]; va_list args; va_start(args,fmt);
    int length=vsnprintf(out,sizeof(out),fmt,args); va_end(args);
    assert(length > 0 && length < 64);
}
'''
cases = r'''
int main(void) {
    era_sync_timing_write_begin(999); era_sync_timing_commit(999,false);
    assert(!active && !count);
    housekeeping_task_user();
    now=100; era_sync_timing_write_begin(1000);
    now=120; era_sync_timing_write_end(20);
    now=121; era_sync_timing_pending(true);
    now=200; era_sync_timing_commit(800,false); gen++; nvm.program_count=4; nvm.erase_count=1;
    now=1200; storage.complete_count++; era_sync_timing_pending(false);
    assert(count==1 && !active);
    assert(rows[0].requested==1000 && rows[0].local==800 && !rows[0].remote);
    assert(rows[0].end-rows[0].start==1100 && rows[0].end-rows[0].rise==1079);
    assert(rows[0].write_ms==20 && rows[0].generation==1 && rows[0].program==4 && rows[0].erase==1);
    now=2000; era_sync_timing_pending(true);
    era_sync_timing_abort(3,4,5,1);
    era_sync_timing_abort(9,8,7,2); /* Recovery cannot overwrite the first failure. */
    era_sync_timing_commit(800,true); era_sync_timing_apply_end(30);
    now=2500; era_sync_timing_pending(false);
    assert(rows[1].requested==0 && rows[1].remote==800 && rows[1].apply_ms==30);
    assert(rows[1].fault==0x03040501U && rows[0].fault==0);
    for (unsigned j=0;j<28;++j) {
        now+=1000; era_sync_timing_write_begin(500); era_sync_timing_pending(true);
        now+=400; era_sync_timing_write_end(2); era_sync_timing_pending(false);
    }
    assert(count==30 && !overflow);
    keyrecord_t key={.event={.pressed=true}};
    assert(!process_record_user(KC_F24,&key));
    era_sync_timing_write_begin(99); assert(!active);
    while(index_out<end_out) { now+=150; housekeeping_task_user(); }
    assert(index_out==30);
    puts("PASS boot exclusion; logical bytes; exact ms/rotation deltas; 30 spans; freeze; short paced output");
}
'''
with tempfile.TemporaryDirectory(prefix='era-sync-timing-') as directory:
    c = Path(directory) / 'timing.c'
    exe = Path(directory) / 'timing'
    c.write_text(prefix + source + cases)
    subprocess.run(['cc', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
