#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the production lease initialization against hardware call boundaries.

The real RP2040 electrical/partial-frame premise remains NOT MEASURED.
"""
import unittest
from test_production import body, run_c

STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef unsigned uint;
typedef unsigned pin_t;
typedef unsigned iomode_t;
typedef struct { float div; } pio_sm_config;
#define PAL_RP_PAD_IE 0
#define PAL_RP_GPIO_OE 0
#define PAL_RP_PAD_SCHMITT 0
#define PAL_RP_PAD_PUE 0
#define PAL_RP_PAD_SLEWFAST 0
#define PAL_RP_PAD_DRIVE12 0
#define PAL_RP_IOCTRL_OEOVER_DRVINVPERI 0
#define PAL_MODE_ALTERNATE_PIO0 0
#define PAL_MODE_ALTERNATE_PIO1 1
#define RESETS_ALLREG_PIO0 0
#define RESETS_ALLREG_PIO1 1
#define ERA_SPLIT_TRANSACTION_BACKEND_UART_TX_WRAP_TARGET 0
#define ERA_SPLIT_TRANSACTION_BACKEND_UART_TX_WRAP 4
#define ERA_SPLIT_TRANSACTION_BACKEND_UART_RX_WRAP_TARGET 0
#define ERA_SPLIT_TRANSACTION_BACKEND_UART_RX_WRAP 5
#define PIO_FIFO_JOIN_TX 0
#define PIO_FIFO_JOIN_RX 1
#define clk_sys 0
static unsigned era_split_transaction_backend_pio;
static int era_split_transaction_backend_tx_sm=0,era_split_transaction_backend_rx_sm=1;
static int era_split_transaction_backend_tx_program_offset=2,era_split_transaction_backend_rx_program_offset=8;
static unsigned g_era_split_transaction_backend_baud=460800;
static bool enabled[2];
static unsigned initialized[2],cleared[2],entered,program_calls,claim_calls;
static uint pio_get_index(unsigned p) {return p;}
static void hal_lld_peripheral_unreset(unsigned p) {(void)p;}
static bool era_split_transaction_backend_load_pio_programs(void) {++program_calls;return true;}
static bool era_split_transaction_backend_claim_pio_state_machines(void) {++claim_calls;return true;}
static void pio_sm_set_enabled(unsigned p,int sm,bool value) {(void)p;enabled[sm]=value;}
static void pio_sm_clear_fifos(unsigned p,int sm) {(void)p;assert(!enabled[sm]);++cleared[sm];}
static void pio_sm_set_pins_with_mask(unsigned p,int sm,unsigned v,unsigned mask) {(void)p;(void)sm;(void)v;(void)mask;}
static void pio_sm_set_consecutive_pindirs(unsigned p,int sm,unsigned pin,unsigned count,bool out) {(void)p;(void)sm;(void)pin;(void)count;(void)out;}
static void palSetLineMode(unsigned pin,unsigned mode) {(void)pin;(void)mode;}
static pio_sm_config pio_get_default_sm_config(void) {return (pio_sm_config){0};}
#define sm_config_set_wrap(c,...) ((void)(c))
#define sm_config_set_sideset(c,...) ((void)(c))
#define sm_config_set_out_shift(c,...) ((void)(c))
#define sm_config_set_out_pins(c,...) ((void)(c))
#define sm_config_set_sideset_pins(c,...) ((void)(c))
#define sm_config_set_in_pins(c,pin) ((void)(c),(void)(pin))
#define sm_config_set_jmp_pin(c,...) ((void)(c))
#define sm_config_set_in_shift(c,...) ((void)(c))
#define sm_config_set_fifo_join(c,...) ((void)(c))
static unsigned clock_get_hz(unsigned c) {(void)c;return 125000000;}
static void sm_config_set_clkdiv(pio_sm_config *c,float div) {c->div=div;}
static void pio_sm_init(unsigned p,int sm,unsigned pc,const pio_sm_config *c) {
    (void)p;assert(!enabled[sm] && cleared[sm]==initialized[sm]+1 && c->div>0);
    assert(pc==(unsigned)(sm?era_split_transaction_backend_rx_program_offset:era_split_transaction_backend_tx_program_offset));
    ++initialized[sm];
}
static void era_split_transaction_backend_enable_owner_irq_sources(void) {assert(initialized[0] && initialized[0]==initialized[1]);}
static void era_split_transaction_backend_enter_rx_state(void) {assert(enabled[0] && enabled[1]);++entered;}
'''

class PioLease(unittest.TestCase):
    def test_fresh_initialization_for_same_role_relaunch(self):
        path='keyboards/era/common/split/era_split_transaction_backend_rp2040.c'
        unit=STUBS
        for name,args,ret in [('pio_tx_init','pin_t tx_pin','void'),('pio_rx_init','pin_t rx_pin','void'),('pio_init','pin_t tx_pin,pin_t rx_pin','bool'),('driver_init_required','bool role_changed','bool')]:
            name='era_split_transaction_backend_'+name
            unit+='\nstatic '+ret+' '+name+'('+args+') {\n'+body(path,name)+'\n}\n'
        unit+=r'''
int main(void) {
    for(unsigned lease=0;lease<3;++lease) {
        assert(era_split_transaction_backend_driver_init_required(lease==0));
        assert(era_split_transaction_backend_pio_init(1,1));
        assert(entered==lease+1 && initialized[0]==lease+1 && initialized[1]==lease+1);
    }
    assert(program_calls==3 && claim_calls==3);
    puts("PASS production PIO lease call order: disable/clear/reinitialize both machines before RX; hardware premise NOT MEASURED");
    return 0;
}
'''
        run_c(unit)

if __name__=='__main__': unittest.main(verbosity=2)
