// SPDX-License-Identifier: GPL-2.0-or-later
/* Hardware boundaries only. The sampler, decode and Bootmagic bodies are production. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <setjmp.h>
typedef unsigned uint;
typedef uint8_t pin_t;
typedef uint16_t matrix_row_t;
#define MATRIX_ROWS_PER_HAND 4
#define MATRIX_COLS 2
#define MATRIX_ROW_SHIFTER 1U
#define MATRIX_ROW_PINS {0,1,2,3}
#define MATRIX_COL_PINS {8,9}
#ifdef SPLIT_KEYBOARD
#    define MATRIX_ROW_PINS_RIGHT {4,5,6,7}
#    define MATRIX_COL_PINS_RIGHT {10,11}
#    define BOOTMAGIC_ROW_RIGHT 4
#    define BOOTMAGIC_COLUMN_RIGHT 1
#endif
#define BOOTMAGIC_ROW 0
#define BOOTMAGIC_COLUMN 0
#define BOOTMAGIC_DEBOUNCE 5
#ifndef FIXTURE_DISABLED
#    define BOOTMAGIC_ENABLE
#endif
#ifndef FIXTURE_LATE
#    define ERA_BOOTMAGIC_EARLY
#endif
static bool physical_left = true, cached_left;
static bool is_keyboard_left(void) {
#ifdef SPLIT_KEYBOARD
    return cached_left;
#else
    return true;
#endif
}
static bool split_hand_pin_is_left(void) {return physical_left;}
static void wait_us(unsigned us);
void bootloader_jump(void);
#define NO_PIN 255
#define ATOMIC_BLOCK_FORCEON if (true)
#define RESETS_ALLREG_PIO1 1
#define RESETS_ALLREG_DMA 2
#define PAL_RP_IOCTRL_FUNCSEL_PIO1 1
#define PAL_RP_PAD_IE 2
#define RP_DMA_CHANNEL_ID_ANY 0
#define DMA_CTRL_TRIG_DATA_SIZE_WORD 0
#define DMA_CTRL_TRIG_INCR_READ 0
#define DMA_CTRL_TRIG_INCR_WRITE 0
#define DMA_CTRL_TRIG_RING_SEL 0
#define DMA_CTRL_TRIG_IRQ_QUIET 0
#define DMA_CTRL_TRIG_RING_SIZE(x) 0
#define DMA_CTRL_TRIG_TREQ_SEL(x) 0
#define DMA_CTRL_TRIG_BUSY 1
static unsigned count_read(void);
#define TRANS_COUNT counts[count_read()]
typedef struct { uint32_t counts[1], WRITE_ADDR, CTRL_TRIG; } DMA_Channel_Typedef;
typedef struct { uint32_t MULTI_CHAN_TRIGGER; } dma_t;
typedef struct { DMA_Channel_Typedef *channel; dma_t *dma; uint32_t chnmask; } rp_dma_channel_t;
static DMA_Channel_Typedef registers[2];
static bool inject_torn;
static unsigned count_read(void) {
    if(inject_torn) registers[1].counts[0]-=16;
    return 0;
}
static dma_t dma;
static rp_dma_channel_t channels[2]={{&registers[0],&dma,1},{&registers[1],&dma,2}};
static struct {uint32_t rxf[4],txf[4],fdebug;} pio;
#define pio1 (&pio)
typedef struct {const uint16_t *instructions;uint8_t length;int origin;} pio_program_t;
typedef unsigned pio_sm_config;
static struct timer {uint32_t timerawl;} clock_register;
static struct timer *clock_tick(void) {clock_register.timerawl+=10;return &clock_register;}
#define timer_hw clock_tick()
static unsigned allocation, mode, claims, programs, disables, waits, delivered_words;
static bool sampler_running;
static bool allocation_failure, program_failure, sm_failure;
static void mock_enable(bool enabled);
static void mock_reset(unsigned mask) {
    if(mask & RESETS_ALLREG_PIO1) sampler_running=false;
    if(mask & RESETS_ALLREG_DMA) {
        registers[0].CTRL_TRIG=0;registers[1].CTRL_TRIG=0;
    }
}
#define hal_lld_peripheral_reset(mask) mock_reset(mask)
#define hal_lld_peripheral_unreset(...) ((void)0)
#define pio_can_add_program(...) (!program_failure)
#define pio_add_program(...) (programs++,0)
#define pio_claim_unused_sm(...) (claims++,sm_failure ? -1 : 0)
#define pio_get_default_sm_config() 0
#define sm_config_set_wrap(...) ((void)0)
#define sm_config_set_out_pins(...) ((void)0)
#define sm_config_set_in_pins(...) ((void)0)
#define sm_config_set_out_shift(...) ((void)0)
#define sm_config_set_in_shift(...) ((void)0)
#define sm_config_set_clkdiv_int_frac(...) ((void)0)
#define pio_sm_init(...) ((void)0)
#define pio_sm_set_pins_with_mask(...) ((void)0)
#define pio_sm_set_pindirs_with_mask(...) ((void)0)
#define palSetLineMode(...) ((void)0)
#define pio_sm_clear_fifos(...) ((void)0)
#define pio_sm_set_enabled(p,s,e) mock_enable(e)
#define gpio_set_pin_input_high(...) ((void)0)
static const rp_dma_channel_t *dmaChannelAlloc(unsigned a,unsigned b,void *c,void *d) {
    (void)a;(void)b;(void)c;(void)d;
    return allocation_failure ? NULL : &channels[allocation++];
}
static void dmaChannelDisableX(const rp_dma_channel_t *c) {disables++;c->channel->CTRL_TRIG=0;}
static void dmaChannelSetSourceX(const rp_dma_channel_t *c,uint32_t v) {(void)c;(void)v;}
static void dmaChannelSetDestinationX(const rp_dma_channel_t *c,uint32_t v) {c->channel->WRITE_ADDR=v;}
static void dmaChannelSetCounterX(const rp_dma_channel_t *c,uint32_t v) {c->channel->counts[0]=v;}
static void dmaChannelSetModeX(const rp_dma_channel_t *c,uint32_t v) {(void)c;(void)v;}
static void dmaChannelEnableX(const rp_dma_channel_t *c) {c->channel->CTRL_TRIG=DMA_CTRL_TRIG_BUSY;}
/* MATRIX_SAMPLER */
static void deliver(unsigned words, bool pressed) {
    unsigned bit = 8;
#ifdef SPLIT_KEYBOARD
    if(!physical_left) bit=11;
#endif
    for(unsigned i=0;i<ERA_RP2040_MATRIX_PIO_RING_WORDS;++i) {
        era_rp2040_matrix_pio_sample_ring[i]=UINT32_MAX;
        if(pressed && (i%ERA_RP2040_MATRIX_PIO_FRAME_WORDS)==0)
            era_rp2040_matrix_pio_sample_ring[i]&=~(1U<<bit);
    }
    delivered_words=words;
    registers[1].counts[0]=UINT32_MAX-words;
    registers[1].WRITE_ADDR=(uint32_t)(uintptr_t)era_rp2040_matrix_pio_sample_ring+
        (words%ERA_RP2040_MATRIX_PIO_RING_WORDS)*4;
}
static void mock_enable(bool enabled) {
    sampler_running=enabled;
    if(enabled) deliver(mode>=2 ? ERA_RP2040_MATRIX_PIO_FRAME_WORDS : mode==1 ? ERA_RP2040_MATRIX_PIO_FRAME_WORDS-1 : 0,mode!=3);
}
static void wait_us(unsigned us) {
    clock_register.timerawl+=us;
    waits++;
    if(!sampler_running || mode<2 || mode==4) return;
    if(mode==6 && waits>5 && waits<75) return;
    deliver(delivered_words+ERA_RP2040_MATRIX_PIO_FRAME_WORDS,
            mode!=3 && !(mode==5 && waits>10));
}
static matrix_row_t rows[MATRIX_ROWS_PER_HAND];
static unsigned resets,jumps;
static bool jump_exits;
static jmp_buf boot_exit;
static matrix_row_t matrix_get_row(uint8_t row) {return rows[row];}
static void matrix_scan(void) {era_rp2040_matrix_update_raw_rows(rows);}
static void wait_ms(unsigned ms) {(void)ms;}
static void bootmagic_reset_eeprom(void) {resets++;}
static void fixture_bootrom(void) {
#ifdef ERA_BOOTMAGIC_EARLY
    assert(!sampler_running);
    assert(registers[0].CTRL_TRIG==0 && registers[1].CTRL_TRIG==0);
#endif
    jumps++;
    if(jump_exits) longjmp(boot_exit,1);
}
#ifndef FIXTURE_REAL_BOOT_CHAIN
void bootloader_jump(void) {fixture_bootrom();}
#endif
/* BOOTMAGIC */
static void fresh(unsigned delivery) {
    memset(&era_rp2040_matrix_pio_state,0,sizeof(era_rp2040_matrix_pio_state));
    memset(era_rp2040_matrix_pio_sample_ring,0,sizeof(era_rp2040_matrix_pio_sample_ring));
    memset(registers,0,sizeof(registers));memset(rows,0,sizeof(rows));
    allocation=resets=jumps=claims=programs=disables=waits=delivered_words=0;mode=delivery;
    allocation_failure=program_failure=sm_failure=false;
    cached_left=inject_torn=sampler_running=jump_exits=false;
    clock_register.timerawl=UINT32_MAX-200;
}
#ifdef BOOTMAGIC_ENABLE
static void timeout_case(unsigned delivery) {
    fresh(delivery);era_rp2040_matrix_init_pins();bootmagic_scan();
    assert(resets==0 && jumps==0); /* unreceived samples cannot request reset */
    assert(!era_rp2040_matrix_pio_state.ready);
    for(unsigned r=0;r<MATRIX_ROWS_PER_HAND;++r)assert(rows[r]==0);
    deliver(ERA_RP2040_MATRIX_PIO_FRAME_WORDS,true);matrix_scan();
    assert(era_rp2040_matrix_pio_state.ready && rows[0]!=0);
    deliver(ERA_RP2040_MATRIX_PIO_FRAME_WORDS,false);matrix_scan();assert(rows[0]==0);
}
#endif
/* EARLY_BOOT_CHAIN */
int main(void) {
    /* CASE */
    return 0;
}
