// SPDX-License-Identifier: GPL-2.0-or-later
/* Hardware boundaries only. The sampler, decode and Bootmagic bodies are production. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef unsigned uint;
typedef uint8_t pin_t;
typedef uint16_t matrix_row_t;
#define MATRIX_ROWS_PER_HAND 4
#define MATRIX_COLS 2
#define MATRIX_ROW_SHIFTER 1U
#define MATRIX_ROW_PINS {0,1,2,3}
#define MATRIX_COL_PINS {8,9}
#define NO_PIN 255
#define ATOMIC_BLOCK_FORCEON if (true)
#define RESETS_ALLREG_PIO1 1
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
typedef struct { uint32_t TRANS_COUNT, WRITE_ADDR, CTRL_TRIG; } DMA_Channel_Typedef;
typedef struct { uint32_t MULTI_CHAN_TRIGGER; } dma_t;
typedef struct { DMA_Channel_Typedef *channel; dma_t *dma; uint32_t chnmask; } rp_dma_channel_t;
static DMA_Channel_Typedef registers[2];
static dma_t dma;
static rp_dma_channel_t channels[2]={{&registers[0],&dma,1},{&registers[1],&dma,2}};
static struct {uint32_t rxf[4],txf[4],fdebug;} pio;
#define pio1 (&pio)
typedef struct {const uint16_t *instructions;uint8_t length;int origin;} pio_program_t;
typedef unsigned pio_sm_config;
static struct timer {uint32_t timerawl;} clock_register;
static struct timer *clock_tick(void) {clock_register.timerawl+=50;return &clock_register;}
#define timer_hw clock_tick()
static unsigned allocation, mode;
static bool allocation_failure, program_failure, sm_failure;
static void mock_enable(bool enabled);
#define hal_lld_peripheral_reset(...) ((void)0)
#define hal_lld_peripheral_unreset(...) ((void)0)
#define pio_can_add_program(...) (!program_failure)
#define pio_add_program(...) 0
#define pio_claim_unused_sm(...) (sm_failure ? -1 : 0)
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
static void dmaChannelDisableX(const rp_dma_channel_t *c) {c->channel->CTRL_TRIG=0;}
static void dmaChannelSetSourceX(const rp_dma_channel_t *c,uint32_t v) {(void)c;(void)v;}
static void dmaChannelSetDestinationX(const rp_dma_channel_t *c,uint32_t v) {c->channel->WRITE_ADDR=v;}
static void dmaChannelSetCounterX(const rp_dma_channel_t *c,uint32_t v) {c->channel->TRANS_COUNT=v;}
static void dmaChannelSetModeX(const rp_dma_channel_t *c,uint32_t v) {(void)c;(void)v;}
static void dmaChannelEnableX(const rp_dma_channel_t *c) {c->channel->CTRL_TRIG=DMA_CTRL_TRIG_BUSY;}
/* MATRIX_SAMPLER */
static void deliver(unsigned words, bool pressed) {
    for(unsigned i=0;i<words;++i) era_rp2040_matrix_pio_sample_ring[i]=UINT32_MAX;
    if (pressed && words) era_rp2040_matrix_pio_sample_ring[0]&=~(1U<<8);
    registers[1].TRANS_COUNT=UINT32_MAX-words;
    registers[1].WRITE_ADDR=(uint32_t)(uintptr_t)era_rp2040_matrix_pio_sample_ring+words*4;
}
static void mock_enable(bool enabled) {
    if(enabled) deliver(mode==2 ? ERA_RP2040_MATRIX_PIO_FRAME_WORDS : mode==1 ? ERA_RP2040_MATRIX_PIO_FRAME_WORDS-1 : 0,mode==2);
}
static matrix_row_t rows[MATRIX_ROWS_PER_HAND];
static unsigned resets,jumps;
#define BOOTMAGIC_ROW 0
#define BOOTMAGIC_COLUMN 0
#define BOOTMAGIC_DEBOUNCE 5
static matrix_row_t matrix_get_row(uint8_t row) {return rows[row];}
static void matrix_scan(void) {era_rp2040_matrix_update_raw_rows(rows);}
static void wait_ms(unsigned ms) {(void)ms;}
static void bootmagic_reset_eeprom(void) {resets++;}
static void bootloader_jump(void) {jumps++;}
/* BOOTMAGIC */
static void fresh(unsigned delivery) {
    memset(&era_rp2040_matrix_pio_state,0,sizeof(era_rp2040_matrix_pio_state));
    memset(era_rp2040_matrix_pio_sample_ring,0,sizeof(era_rp2040_matrix_pio_sample_ring));
    memset(registers,0,sizeof(registers));memset(rows,0,sizeof(rows));
    allocation=resets=jumps=0;mode=delivery;
    allocation_failure=program_failure=sm_failure=false;
    clock_register.timerawl=UINT32_MAX-200;
}
static void timeout_case(unsigned delivery) {
    fresh(delivery);era_rp2040_matrix_init_pins();bootmagic_scan();
    assert(resets==0 && jumps==0); /* unreceived samples cannot request reset */
    assert(!era_rp2040_matrix_pio_state.ready);
    for(unsigned r=0;r<MATRIX_ROWS_PER_HAND;++r)assert(rows[r]==0);
    deliver(ERA_RP2040_MATRIX_PIO_FRAME_WORDS,true);matrix_scan();
    assert(era_rp2040_matrix_pio_state.ready && rows[0]==1);
    deliver(ERA_RP2040_MATRIX_PIO_FRAME_WORDS,false);matrix_scan();assert(rows[0]==0);
}
int main(void) {
    /* CASE */
    return 0;
}
