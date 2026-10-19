#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile real input owners/codec with TOMAK geometry; execute no wire model."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent

def body(path,name):
    text=re.sub(r'/\*.*?\*/|//[^\n]*','',(ROOT/path).read_text(encoding='utf-8'),flags=re.S)
    start=re.search(r'^[ \t]*(?:static[ \t]+)?(?:inline[ \t]+)?[A-Za-z_]\w*[ \t*]+'+name+r'\([^;{}]*\)\s*\{',text,re.M).end(); depth=1
    for end in range(start,len(text)):
        depth+=(text[end]=='{')-(text[end]=='}')
        if not depth:return text[start:end]
    raise AssertionError(name)

def run_c(unit, includes=()):
    with tempfile.TemporaryDirectory(prefix='era-input-') as folder:
        file=Path(folder)/'production.c'; exe=Path(folder)/'production'
        file.write_text(unit,encoding='utf-8')
        subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(HERE),'-I'+str(ROOT),'-I'+str(ROOT/'tmk_core/protocol'),*includes,str(file),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

class InputProduction(unittest.TestCase):
    def owners_unit(self, filename):
        unit=(HERE/filename).read_text(encoding='utf-8')
        for name,args in [('era_split_restart_authority_valid','uint8_t act, uint8_t param, bool armed'),('era_split_restart_arm_valid','uint8_t act, uint8_t param, uint32_t commit_ms')]:
            unit+='\nbool '+name+'('+args+') {\n'+body('keyboards/era/common/split/era_split_restart_agreement.c',name)+'\n}\n'
        return unit

    def test_input_owners(self):
        run_c(self.owners_unit('input_history.c'))

    def test_actual_responder(self):
        run_c(self.owners_unit('responder.c'), ['-I'+str(ROOT/'tests/era_split_standing_lifecycle/include')])

    def test_actual_initiator(self):
        run_c(self.owners_unit('initiator.c'), ['-I'+str(ROOT/'tests/era_split_standing_lifecycle/include')])

    def test_actual_action_binding(self):
        unit='#define main input_owner_tests\n'+self.owners_unit('input_history.c')+'\n#undef main\n'+BINDING_STUBS
        for name,args,ret in [('era_split_input_action_begin','bool action_open','void'),('era_split_input_action_end','void','void'),('era_split_input_event_remote','uint8_t row','bool')]:
            unit+='\n'+ret+' '+name+'('+args+') {\n'+body('keyboards/era/common/split/era_split_input_qmk.c',name)+'\n}\n'
        run_c(unit+BINDING_CASES)

BINDING_STUBS = r'''
static matrix_row_t matrix_previous[MATRIX_ROWS],composed[6];
static bool input_remote_pass,input_previous_host,closure;
static uint32_t input_hid_pressure_seen,pressure_serial;
static uint8_t room=66;
static unsigned projections,wakes;
static bool is_keyboard_left(void) {return true;}
static uint8_t action_tapping_remote_room(void) {return room;}
static bool era_hid_report_interval_pressured(void) {return false;}
static uint32_t era_hid_report_interval_pressure_serial(void) {return pressure_serial;}
static bool era_matrix_engine_peer_closure_pending(void) {return closure;}
static void era_split_keyboard_note_input_edge(void) {++wakes;}
static void era_matrix_engine_accept_peer_snapshot(const matrix_row_t *rows) {memcpy(composed,rows,sizeof(composed));}
static void era_matrix_engine_sync_peer_projection(bool host) {++projections;if(!host) {memset(composed,0,sizeof(composed));closure=false;}}
'''
BINDING_CASES = r'''
int main(void) {
    open(); era_split_input_request_t r=delta(1,0);assert(deliver(&r,1)==ERA_INPUT_ACCEPTED);
    era_split_input_action_begin(false);assert(wakes==1 && !projections && !receiver.claimed);
    era_split_input_action_begin(true);assert(receiver.claimed && composed[0]==1 && era_split_input_event_remote(6));
    memcpy(matrix_previous+6,composed,sizeof(composed));era_split_input_action_end();
    era_split_input_receiver_gate(false,0,0);closure=true;room=0;
    era_split_input_action_begin(true);assert(closure && projections==1 && era_split_input_event_remote(6));
    room=1;era_split_input_action_begin(true);assert(!closure && composed[0]==0 && projections==2);
    era_split_input_action_end();memset(matrix_previous,0,sizeof(matrix_previous));
    era_split_input_action_begin(true);assert(!era_split_input_event_remote(6));
    puts("PASS production action binding: suspended remote wake without claim, complete group projection, closure release reservation and retained HP origin");
    return 0;
}
'''

if __name__=='__main__':unittest.main(verbosity=2)
