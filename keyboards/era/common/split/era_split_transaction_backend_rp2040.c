// Copyright 2022 Stefan Kerkmann
// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_transaction_backend.h"

#include <string.h>

#include "serial_usart.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/regs/timer.h"
#include "hardware/structs/padsbank0.h"
#include "hardware/structs/timer.h"
#include "pico/platform.h"
#include "communication_core/era_split_communication_core_owner.h"


#if !defined(MCU_RP)
#    error "ERA RP2040 transaction backend requires MCU_RP."
#endif

/* This backend claims PIO0 and only PIO0. The SERIAL_PIO_USE_PIO1 arms that
   stood beside every claim were unreachable -- the selector belongs to the
   stock split serial driver and no ERA board sets it -- and they were deleted
   on 2026-08-11 rather than kept as a port that does not exist. Deleting them
   is only safe with this #error beside it: era_vector_defaults.c fills
   whichever PIO slot the backend left, and it decides which one from the same
   selector, so a board that set it without this guard would install one
   handler and default the other. That failure is silent, and it is exactly the
   mirror-image mistake the vector file's own comment records having made
   twice. */
#if defined(SERIAL_PIO_USE_PIO1)
#    error "ERA RP2040 transaction backend claims PIO0; SERIAL_PIO_USE_PIO1 is not supported."
#endif

/* The arm that selected 1 asked for SERIAL_USART_FULL_DUPLEX or
   SERIAL_USART_PIN_SWAP. Every ERA split config.h #errors on the first and no
   ERA board sets the second, so the reinit-on-role-change path was unreachable
   in every buildable configuration. */
#ifndef ERA_SPLIT_SERIAL_REINIT_ON_ROLE_CHANGE
#    define ERA_SPLIT_SERIAL_REINIT_ON_ROLE_CHANGE 0
#endif

#define ERA_SPLIT_TRANSACTION_BACKEND_UART_TX_WRAP_TARGET 0
#define ERA_SPLIT_TRANSACTION_BACKEND_UART_TX_WRAP 3
#define ERA_SPLIT_TRANSACTION_BACKEND_UART_RX_WRAP_TARGET 0
#define ERA_SPLIT_TRANSACTION_BACKEND_UART_RX_WRAP 8
#define ERA_SPLIT_TRANSACTION_BACKEND_TURNAROUND_US_RAW (1000000U * 11U / SERIAL_USART_SPEED)
/* One byte on the wire is 10 bit-times (start, eight data, stop): the TX
   program spends exactly 80 state-machine cycles per byte at 8 cycles a bit.
   Floored to a whole microsecond so a sleep computed from it always ends
   before the byte does. */
#define ERA_SPLIT_TRANSACTION_BACKEND_BYTE_US_FLOOR (1000000U * 10U / SERIAL_USART_SPEED)
#define ERA_SPLIT_TRANSACTION_BACKEND_TURNAROUND_US ((ERA_SPLIT_TRANSACTION_BACKEND_TURNAROUND_US_RAW > 0U) ? ERA_SPLIT_TRANSACTION_BACKEND_TURNAROUND_US_RAW : 1U)
#define ERA_SPLIT_TRANSACTION_BACKEND_SERIAL_TIMEOUT_US ((uint32_t)SERIAL_USART_TIMEOUT * 1000U)
/* The three macros above are the High level's values, and the wire runs at one
   of three. Every one of them is a byte count in disguise -- eleven bit times,
   ten bit times, and a bound whose whole job is to sit above the longest legal
   frame -- so each is multiplied by the level's scale rather than restated per
   level, and a level change moves all of them together. Moving one without the
   others is the failure this arrangement exists to make impossible: at 115200 a
   271-byte bulk storage frame takes 23.5 ms, so the unscaled 20 ms send bound
   would refuse the frame the storage lane sends on every VIA save.

   Wire-time windows scale and silence windows do not, and that split is the
   whole rule. ERA_SPLIT_RESPONDER_SILENCE_MS, the standing liveness beat and
   the initiator unresponsive bound all bound how long nothing may happen; what
   scales against them is the poll period, which the scheduler moves with the
   same scale this unit answers.

   **The baud is the one fact and every number above is asked of it.** There is
   no second copy of the rate here and no map from anything else to it: the
   caller hands over a baud, and the scale, the byte time, the turnaround and
   the send bound are all recomputed from that one argument in one step. What
   this replaced was a level-to-scale map beside a separately stored level, and
   the failure that arrangement made possible was the two disagreeing after
   only one of them had moved. */
static uint32_t g_era_split_transaction_backend_baud              = (uint32_t)SERIAL_USART_SPEED;
static uint8_t  g_era_split_transaction_backend_wire_scale        = 1U;
static uint32_t g_era_split_transaction_backend_byte_us           = ERA_SPLIT_TRANSACTION_BACKEND_BYTE_US_FLOOR;
static uint32_t g_era_split_transaction_backend_turnaround_us     = ERA_SPLIT_TRANSACTION_BACKEND_TURNAROUND_US;
static uint32_t g_era_split_transaction_backend_serial_timeout_us = ERA_SPLIT_TRANSACTION_BACKEND_SERIAL_TIMEOUT_US;

uint8_t era_split_transaction_backend_wire_scale(void) {
    return g_era_split_transaction_backend_wire_scale;
}
#define ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_ALARM 3U
#define ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_IRQ RP_TIMER_IRQ3_NUMBER
#define ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_MASK TIMER_INTE_ALARM_3_BITS
typedef struct {
    bool                                 initialized;
    bool                                 transaction_backend_initialized;
    era_split_transaction_backend_role_t transaction_backend_init_role;
    era_split_transaction_backend_role_t transaction_backend_role;
} era_split_transaction_backend_rp2040_state_t;

static era_split_transaction_backend_rp2040_state_t g_era_split_transaction_backend_rp2040;

static const PIO era_split_transaction_backend_pio = pio0;

// clang-format off
static const uint16_t era_split_transaction_backend_uart_tx_program_instructions[] = {
    0x9fa0, // pull block side 1 [7]
    0xf727, // set x, 7 side 0 [7]
    0x6081, // out pindirs, 1
    0x0642, // jmp x--, 2 [6]
};

static const uint16_t era_split_transaction_backend_uart_rx_program_instructions[] = {
    0x2020, // wait 0 pin, 0
    0xea27, // set x, 7 [10]
    0x4001, // in pins, 1
    0x0642, // jmp x--, 2 [6]
    0x00c8, // jmp pin, 8
    0xc020, // irq wait 0
    0x20a0, // wait 1 pin, 0
    0x0000, // jmp 0
    0x8020, // push block
};
// clang-format on

static const pio_program_t era_split_transaction_backend_uart_tx_program = {
    .instructions = era_split_transaction_backend_uart_tx_program_instructions,
    .length       = 4,
    .origin       = -1,
};

static const pio_program_t era_split_transaction_backend_uart_rx_program = {
    .instructions = era_split_transaction_backend_uart_rx_program_instructions,
    .length       = 9,
    .origin       = -1,
};

static int                era_split_transaction_backend_rx_sm             = -1;
static int                era_split_transaction_backend_tx_sm             = -1;
static int                era_split_transaction_backend_rx_program_offset = -1;
static int                era_split_transaction_backend_tx_program_offset = -1;
static volatile bool      era_split_transaction_backend_rx_error_pending  = false;

static bool era_split_transaction_backend_deadline_expired(uint32_t deadline_us) {
    return (int32_t)(timer_hw->timerawl - deadline_us) >= 0;
}

/* Wake sources a park may arm beside the alarm; the alarm is always armed. */
enum {
    ERA_SPLIT_TRANSACTION_BACKEND_PARK_WAKE_RX_NOT_EMPTY = 1U << 0,
    ERA_SPLIT_TRANSACTION_BACKEND_PARK_WAKE_TX_NOT_FULL  = 1U << 1,
};

static void era_split_transaction_backend_park_until(uint32_t deadline_us, uint8_t wake_mask);

#ifdef ERA_SPLIT_CORE1_PARK_DIAGNOSTICS_ENABLE
/* How often and for how long this backend parked core1. Diagnostic images
   only: two timer reads a park is nothing against a park, but the release
   image does not carry instruments it does not print. */
static volatile uint32_t era_split_transaction_backend_park_count;
static volatile uint32_t era_split_transaction_backend_park_us;
#endif

static era_split_transaction_backend_wait_result_t era_split_transaction_backend_access_result(uint16_t owner_epoch) {
    switch (era_split_communication_core_owner_backend_access(owner_epoch)) {
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OK:
            return ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_EPOCH:
            return ERA_SPLIT_TRANSACTION_BACKEND_WAIT_EPOCH;
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_CANCEL:
            return ERA_SPLIT_TRANSACTION_BACKEND_WAIT_CANCEL;
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_RESET:
            return ERA_SPLIT_TRANSACTION_BACKEND_WAIT_RESET;
        case ERA_SPLIT_COMMUNICATION_CORE_BACKEND_ACCESS_OWNER:
        default:
            return ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OWNER;
    }
}

static bool era_split_transaction_backend_access_allowed(void) {
    return era_split_transaction_backend_access_result(0) == ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
}

/* Two converters, because there are two kinds of window and one of them must
   not scale.

   A **wire window** bounds how long a frame of bytes may take, so it is a byte
   count in disguise and moves with the rate: the compact response window and
   the responder frame window here, plus the storage lane bulk-page body
   deadline, which computes the same product at its own call site because it
   starts from a constant this unit does not hold.

   A **service window** bounds how long nothing may happen. The responder
   first-byte wait of 60 s is the only one that reaches this unit, and scaling
   it made that 240 s at Low -- harmless, because the receive loop leaves on an
   owner-epoch or cancel edge every pass, but four times what the constant
   says. One converter for both is what produced that, so there are two, and a
   window added later picks the one that names what it bounds. */
static uint32_t era_split_transaction_backend_wire_window_us(uint16_t timeout_ms) {
    return (uint32_t)timeout_ms * 1000U * g_era_split_transaction_backend_wire_scale;
}

static uint32_t era_split_transaction_backend_service_window_us(uint16_t timeout_ms) {
    return (uint32_t)timeout_ms * 1000U;
}

static uint32_t era_split_transaction_backend_irq_save(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void era_split_transaction_backend_irq_restore(uint32_t primask) {
    __set_PRIMASK(primask);
}

static void era_split_transaction_backend_set_irq0_source_enabled(enum pio_interrupt_source source, bool enabled) {
    if (enabled) {
        hw_set_bits(&era_split_transaction_backend_pio->inte0, 1U << source);
    } else {
        hw_clear_bits(&era_split_transaction_backend_pio->inte0, 1U << source);
    }
}

static void era_split_transaction_backend_set_rx_fifo_irq_enabled(bool enabled) {
    if (era_split_transaction_backend_rx_sm < 0) {
        return;
    }
    era_split_transaction_backend_set_irq0_source_enabled((enum pio_interrupt_source)(pis_sm0_rx_fifo_not_empty + era_split_transaction_backend_rx_sm), enabled);
}

static void era_split_transaction_backend_set_tx_fifo_irq_enabled(bool enabled) {
    if (era_split_transaction_backend_tx_sm < 0) {
        return;
    }
    era_split_transaction_backend_set_irq0_source_enabled((enum pio_interrupt_source)(pis_sm0_tx_fifo_not_full + era_split_transaction_backend_tx_sm), enabled);
}

static void era_split_transaction_backend_set_owner_irq_vector_enabled(bool enabled) {
    if (enabled) {
        nvicEnableVector(RP_PIO0_IRQ_0_NUMBER, CORTEX_MAX_KERNEL_PRIORITY);
    } else {
        nvicDisableVector(RP_PIO0_IRQ_0_NUMBER);
    }
}

static void era_split_transaction_backend_enable_owner_irq_sources(void) {
    hw_clear_bits(&timer_hw->inte, TIMER_INTE_ALARM_3_BITS);
    timer_hw->intr = TIMER_INTR_ALARM_3_BITS;
    NVIC_DisableIRQ((IRQn_Type)RP_TIMER_IRQ3_NUMBER);
    era_split_transaction_backend_set_rx_fifo_irq_enabled(false);
    era_split_transaction_backend_set_tx_fifo_irq_enabled(false);
    pio_interrupt_clear(era_split_transaction_backend_pio, 0UL);
    era_split_transaction_backend_rx_error_pending = false;
    era_split_transaction_backend_set_irq0_source_enabled(pis_interrupt0, true);
    era_split_transaction_backend_set_owner_irq_vector_enabled(true);
}

static void era_split_transaction_backend_disable_owner_irq_sources(void) {
    era_split_transaction_backend_set_rx_fifo_irq_enabled(false);
    era_split_transaction_backend_set_tx_fifo_irq_enabled(false);
    era_split_transaction_backend_set_irq0_source_enabled(pis_interrupt0, false);
    era_split_transaction_backend_set_owner_irq_vector_enabled(false);
    hw_clear_bits(&timer_hw->inte, TIMER_INTE_ALARM_3_BITS);
    timer_hw->intr = TIMER_INTR_ALARM_3_BITS;
    NVIC_DisableIRQ((IRQn_Type)RP_TIMER_IRQ3_NUMBER);
    __DMB();
}

static void era_split_transaction_backend_sm_set_enabled(uint sm, bool enabled) {
    const uint32_t mask = 1U << sm;
    if (enabled) {
        hw_set_bits(&era_split_transaction_backend_pio->ctrl, mask);
    } else {
        hw_clear_bits(&era_split_transaction_backend_pio->ctrl, mask);
    }
}

static void era_split_transaction_backend_sm_restart(uint sm) {
    hw_set_bits(&era_split_transaction_backend_pio->ctrl, 1U << (PIO_CTRL_SM_RESTART_LSB + sm));
}

static void era_split_transaction_backend_sm_exec_set(uint sm, enum pio_src_dest dest, uint pin, uint value) {
    uint32_t pinctrl_saved  = era_split_transaction_backend_pio->sm[sm].pinctrl;
    uint32_t execctrl_saved = era_split_transaction_backend_pio->sm[sm].execctrl;

    hw_clear_bits(&era_split_transaction_backend_pio->sm[sm].execctrl, PIO_SM0_EXECCTRL_OUT_STICKY_BITS);
    era_split_transaction_backend_pio->sm[sm].pinctrl  = (1U << PIO_SM0_PINCTRL_SET_COUNT_LSB) | (pin << PIO_SM0_PINCTRL_SET_BASE_LSB);
    era_split_transaction_backend_pio->sm[sm].instr    = pio_encode_set(dest, value);
    era_split_transaction_backend_pio->sm[sm].pinctrl  = pinctrl_saved;
    era_split_transaction_backend_pio->sm[sm].execctrl = execctrl_saved;
}

static void era_split_transaction_backend_set_pin(uint sm, uint pin, bool high) {
    era_split_transaction_backend_sm_exec_set(sm, pio_pins, pin, high ? 1U : 0U);
}

static void era_split_transaction_backend_set_pindir(uint sm, uint pin, bool is_out) {
    era_split_transaction_backend_sm_exec_set(sm, pio_pindirs, pin, is_out ? 1U : 0U);
}

static void era_split_transaction_backend_set_drive_strength(uint pin, enum gpio_drive_strength drive) {
    hw_write_masked(&padsbank0_hw->io[pin], (uint32_t)drive << PADS_BANK0_GPIO0_DRIVE_LSB, PADS_BANK0_GPIO0_DRIVE_BITS);
}

static void era_split_transaction_backend_turnaround_delay(void) {
    const uint32_t start_us = timer_hw->timerawl;
    while ((uint32_t)(timer_hw->timerawl - start_us) < g_era_split_transaction_backend_turnaround_us) {
    }
}

enum {
    ERA_SPLIT_TRANSACTION_BACKEND_IRQ_EVENT_RX    = 1U << 0,
    ERA_SPLIT_TRANSACTION_BACKEND_IRQ_EVENT_ERROR = 1U << 1,
    ERA_SPLIT_TRANSACTION_BACKEND_IRQ_EVENT_TX    = 1U << 2,
};

/* Both FIFO sources are level-sensitive, so the handler's job is to disable
   the one that fired and wake the parked core; the parked loop reads the FIFO
   itself. Neither drains nor validates anything here (era_invariants.md). */
static uint8_t era_split_transaction_backend_rp2040_serve_hardware_interrupt(void) {
    uint32_t irqs = era_split_transaction_backend_pio->ints0;
    uint8_t  events = 0;

    if (era_split_transaction_backend_rx_sm >= 0 && (irqs & (PIO_IRQ0_INTF_SM0_RXNEMPTY_BITS << era_split_transaction_backend_rx_sm)) != 0) {
        era_split_transaction_backend_set_rx_fifo_irq_enabled(false);
        events |= ERA_SPLIT_TRANSACTION_BACKEND_IRQ_EVENT_RX;
    }
    if (era_split_transaction_backend_tx_sm >= 0 && (irqs & (PIO_IRQ0_INTF_SM0_TXNFULL_BITS << era_split_transaction_backend_tx_sm)) != 0) {
        era_split_transaction_backend_set_tx_fifo_irq_enabled(false);
        events |= ERA_SPLIT_TRANSACTION_BACKEND_IRQ_EVENT_TX;
    }

    if (pio_interrupt_get(era_split_transaction_backend_pio, 0UL)) {
        pio_interrupt_clear(era_split_transaction_backend_pio, 0UL);
        era_split_transaction_backend_rx_error_pending = true;
        events |= ERA_SPLIT_TRANSACTION_BACKEND_IRQ_EVENT_ERROR;
    }
    if (events != 0) {
        __DMB();
        __SEV();
    }
    return events;
}

OSAL_IRQ_HANDLER(RP_PIO0_IRQ_0_HANDLER) {
    if (get_core_num() != 0) {
        (void)era_split_transaction_backend_rp2040_serve_hardware_interrupt();
        return;
    }
    OSAL_IRQ_PROLOGUE();
    (void)era_split_transaction_backend_rp2040_serve_hardware_interrupt();
    OSAL_IRQ_EPILOGUE();
}

static void era_split_transaction_backend_enter_rx_state(void) {
    uint32_t irq_state = era_split_transaction_backend_irq_save();
    while (!pio_sm_is_tx_fifo_empty(era_split_transaction_backend_pio, era_split_transaction_backend_tx_sm)) {
    }
    era_split_transaction_backend_turnaround_delay();
    era_split_transaction_backend_sm_set_enabled(era_split_transaction_backend_tx_sm, false);
    era_split_transaction_backend_set_drive_strength(SERIAL_USART_TX_PIN, GPIO_DRIVE_STRENGTH_2MA);
    era_split_transaction_backend_set_pin(era_split_transaction_backend_tx_sm, SERIAL_USART_TX_PIN, true);
    era_split_transaction_backend_set_pindir(era_split_transaction_backend_tx_sm, SERIAL_USART_TX_PIN, false);
    era_split_transaction_backend_sm_set_enabled(era_split_transaction_backend_rx_sm, true);
    era_split_transaction_backend_irq_restore(irq_state);
}

static void era_split_transaction_backend_leave_rx_state(void) {
    uint32_t irq_state = era_split_transaction_backend_irq_save();
    era_split_transaction_backend_sm_set_enabled(era_split_transaction_backend_rx_sm, false);
    era_split_transaction_backend_set_pindir(era_split_transaction_backend_tx_sm, SERIAL_USART_TX_PIN, true);
    era_split_transaction_backend_set_pin(era_split_transaction_backend_tx_sm, SERIAL_USART_TX_PIN, false);
    era_split_transaction_backend_set_drive_strength(SERIAL_USART_TX_PIN, GPIO_DRIVE_STRENGTH_12MA);
    era_split_transaction_backend_sm_restart(era_split_transaction_backend_tx_sm);
    era_split_transaction_backend_sm_set_enabled(era_split_transaction_backend_tx_sm, true);
    era_split_transaction_backend_irq_restore(irq_state);
}
static void era_split_transaction_backend_force_rx_state(void) {
    if (era_split_transaction_backend_rx_sm < 0 || era_split_transaction_backend_tx_sm < 0) {
        return;
    }
    uint32_t irq_state = era_split_transaction_backend_irq_save();
    era_split_transaction_backend_sm_set_enabled(era_split_transaction_backend_tx_sm, false);
    era_split_transaction_backend_sm_set_enabled(era_split_transaction_backend_rx_sm, false);
    pio_sm_clear_fifos(era_split_transaction_backend_pio, era_split_transaction_backend_tx_sm);
    pio_sm_clear_fifos(era_split_transaction_backend_pio, era_split_transaction_backend_rx_sm);
    era_split_transaction_backend_set_drive_strength(SERIAL_USART_TX_PIN, GPIO_DRIVE_STRENGTH_2MA);
    era_split_transaction_backend_set_pin(era_split_transaction_backend_tx_sm, SERIAL_USART_TX_PIN, true);
    era_split_transaction_backend_set_pindir(era_split_transaction_backend_tx_sm, SERIAL_USART_TX_PIN, false);
    era_split_transaction_backend_sm_restart(era_split_transaction_backend_rx_sm);
    era_split_transaction_backend_sm_set_enabled(era_split_transaction_backend_rx_sm, true);
    era_split_transaction_backend_irq_restore(irq_state);
}
/* The drain, parked. The state machine's byte timing is exact -- 80 cycles at
   the fixed clock divider -- so with `level` words still queued the FIFO
   cannot empty for another (level - 1) byte-times; that much is slept on the
   alarm, the last byte-time is observed by polling exactly as before, and the
   turnaround after the observed empty is slept and then spun to its end. The
   RX switch therefore lands where it always did: FIFO-empty observed plus
   TURNAROUND_US. What the responder's processing time is, and whether an
   earlier switch would be safe, was deliberately not measured for this; a
   byte-done PIO IRQ that would remove the last polled byte-time is a later
   rung, not this one. The switch sequence itself keeps its interrupt mask;
   the waits run outside it, because a masked core does not leave WFE on a
   pending interrupt. */
static bool era_split_transaction_backend_enter_rx_state_owned(uint16_t owner_epoch, era_split_transaction_backend_wait_result_t *wait_result) {
    uint32_t start_us = timer_hw->timerawl;
    uint     level    = pio_sm_get_tx_fifo_level(era_split_transaction_backend_pio, era_split_transaction_backend_tx_sm);
    if (level > 1U) {
        era_split_transaction_backend_park_until(start_us + (level - 1U) * g_era_split_transaction_backend_byte_us, 0);
    }
    while (!pio_sm_is_tx_fifo_empty(era_split_transaction_backend_pio, era_split_transaction_backend_tx_sm)) {
        era_split_transaction_backend_wait_result_t access = era_split_transaction_backend_access_result(owner_epoch);
        if (access != ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK) {
            *wait_result = access;
            return false;
        }
        if ((uint32_t)(timer_hw->timerawl - start_us) >= g_era_split_transaction_backend_serial_timeout_us) {
            *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_TIMEOUT;
            return false;
        }
    }
    uint32_t empty_us = timer_hw->timerawl;
    era_split_transaction_backend_park_until(empty_us + g_era_split_transaction_backend_turnaround_us, 0);
    while ((uint32_t)(timer_hw->timerawl - empty_us) < g_era_split_transaction_backend_turnaround_us) {
    }
    uint32_t irq_state = era_split_transaction_backend_irq_save();
    era_split_transaction_backend_sm_set_enabled(era_split_transaction_backend_tx_sm, false);
    era_split_transaction_backend_set_drive_strength(SERIAL_USART_TX_PIN, GPIO_DRIVE_STRENGTH_2MA);
    era_split_transaction_backend_set_pin(era_split_transaction_backend_tx_sm, SERIAL_USART_TX_PIN, true);
    era_split_transaction_backend_set_pindir(era_split_transaction_backend_tx_sm, SERIAL_USART_TX_PIN, false);
    era_split_transaction_backend_sm_set_enabled(era_split_transaction_backend_rx_sm, true);
    era_split_transaction_backend_irq_restore(irq_state);
    *wait_result = era_split_transaction_backend_access_result(owner_epoch);
    return *wait_result == ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
}

static void era_split_transaction_backend_rp2040_clear_fifo(void) {
    if (era_split_transaction_backend_rx_sm < 0) {
        return;
    }
    uint32_t irq_state = era_split_transaction_backend_irq_save();
    while (!pio_sm_is_rx_fifo_empty(era_split_transaction_backend_pio, era_split_transaction_backend_rx_sm)) {
        pio_sm_clear_fifos(era_split_transaction_backend_pio, era_split_transaction_backend_rx_sm);
    }
    era_split_transaction_backend_irq_restore(irq_state);
}

static bool era_split_transaction_backend_send_impl_owned(const uint8_t *source, size_t size, uint16_t owner_epoch, era_split_transaction_backend_wait_result_t *wait_result) {
    size_t   sent     = 0;
    uint32_t start_us = timer_hw->timerawl;
    while (sent < size) {
        era_split_transaction_backend_wait_result_t access = era_split_transaction_backend_access_result(owner_epoch);
        if (access != ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK) {
            *wait_result = access;
            return false;
        }
        if ((uint32_t)(timer_hw->timerawl - start_us) >= g_era_split_transaction_backend_serial_timeout_us) {
            *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_TIMEOUT;
            return false;
        }
        if (pio_sm_is_tx_fifo_full(era_split_transaction_backend_pio, era_split_transaction_backend_tx_sm)) {
            /* A frame longer than the eight-entry FIFO -- a bulk page -- used
               to spin here a byte-time per byte. Park until a slot frees, or
               until the send's own timeout, whichever comes first. */
            era_split_transaction_backend_park_until(start_us + g_era_split_transaction_backend_serial_timeout_us, ERA_SPLIT_TRANSACTION_BACKEND_PARK_WAKE_TX_NOT_FULL);
            continue;
        }

        uint32_t irq_state = era_split_transaction_backend_irq_save();
        while (sent < size && !pio_sm_is_tx_fifo_full(era_split_transaction_backend_pio, era_split_transaction_backend_tx_sm)) {
            pio_sm_put(era_split_transaction_backend_pio, era_split_transaction_backend_tx_sm, (uint32_t)(*source));
            source++;
            sent++;
        }
        era_split_transaction_backend_irq_restore(irq_state);
    }

    *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
    return true;
}

static bool era_split_transaction_backend_load_pio_programs(void) {
    bool tx_program_added = false;

    if (era_split_transaction_backend_tx_program_offset < 0) {
        if (!pio_can_add_program(era_split_transaction_backend_pio, &era_split_transaction_backend_uart_tx_program)) {
            return false;
        }
        era_split_transaction_backend_tx_program_offset = (int)pio_add_program(era_split_transaction_backend_pio, &era_split_transaction_backend_uart_tx_program);
        tx_program_added                                = true;
    }

    if (era_split_transaction_backend_rx_program_offset < 0) {
        if (!pio_can_add_program(era_split_transaction_backend_pio, &era_split_transaction_backend_uart_rx_program)) {
            if (tx_program_added) {
                pio_remove_program(era_split_transaction_backend_pio, &era_split_transaction_backend_uart_tx_program, (uint)era_split_transaction_backend_tx_program_offset);
                era_split_transaction_backend_tx_program_offset = -1;
            }
            return false;
        }
        era_split_transaction_backend_rx_program_offset = (int)pio_add_program(era_split_transaction_backend_pio, &era_split_transaction_backend_uart_rx_program);
    }

    return true;
}

static bool era_split_transaction_backend_claim_pio_state_machines(void) {
    bool tx_sm_claimed = false;

    if (era_split_transaction_backend_tx_sm < 0) {
        era_split_transaction_backend_tx_sm = pio_claim_unused_sm(era_split_transaction_backend_pio, false);
        if (era_split_transaction_backend_tx_sm < 0) {
            return false;
        }
        tx_sm_claimed = true;
    }

    if (era_split_transaction_backend_rx_sm < 0) {
        era_split_transaction_backend_rx_sm = pio_claim_unused_sm(era_split_transaction_backend_pio, false);
        if (era_split_transaction_backend_rx_sm < 0) {
            if (tx_sm_claimed) {
                pio_sm_unclaim(era_split_transaction_backend_pio, (uint)era_split_transaction_backend_tx_sm);
                era_split_transaction_backend_tx_sm = -1;
            }
            return false;
        }
    }

    return true;
}

static void era_split_transaction_backend_pio_tx_init(pin_t tx_pin) {
    uint pio_idx = pio_get_index(era_split_transaction_backend_pio);
    uint offset  = (uint)era_split_transaction_backend_tx_program_offset;

    // clang-format off
    iomode_t tx_pin_mode = PAL_RP_PAD_IE |
                           PAL_RP_GPIO_OE |
                           PAL_RP_PAD_SCHMITT |
                           PAL_RP_PAD_PUE |
                           PAL_RP_PAD_SLEWFAST |
                           PAL_RP_PAD_DRIVE12 |
                           PAL_RP_IOCTRL_OEOVER_DRVINVPERI |
                           (pio_idx == 0 ? PAL_MODE_ALTERNATE_PIO0 : PAL_MODE_ALTERNATE_PIO1);
    // clang-format on
    pio_sm_set_pins_with_mask(era_split_transaction_backend_pio, era_split_transaction_backend_tx_sm, 0U << tx_pin, 1U << tx_pin);
    pio_sm_set_consecutive_pindirs(era_split_transaction_backend_pio, era_split_transaction_backend_tx_sm, tx_pin, 1U, true);

    palSetLineMode(tx_pin, tx_pin_mode);

    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, offset + ERA_SPLIT_TRANSACTION_BACKEND_UART_TX_WRAP_TARGET, offset + ERA_SPLIT_TRANSACTION_BACKEND_UART_TX_WRAP);
    sm_config_set_sideset(&config, 2, true, true);
    sm_config_set_out_shift(&config, true, false, 32);
    sm_config_set_out_pins(&config, tx_pin, 1);
    sm_config_set_sideset_pins(&config, tx_pin);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    float div = (float)clock_get_hz(clk_sys) / (float)(8U * g_era_split_transaction_backend_baud);
    sm_config_set_clkdiv(&config, div);
    pio_sm_init(era_split_transaction_backend_pio, era_split_transaction_backend_tx_sm, offset, &config);
    pio_sm_set_enabled(era_split_transaction_backend_pio, era_split_transaction_backend_tx_sm, true);
}

static void era_split_transaction_backend_pio_rx_init(pin_t rx_pin) {
    uint offset = (uint)era_split_transaction_backend_rx_program_offset;

    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, offset + ERA_SPLIT_TRANSACTION_BACKEND_UART_RX_WRAP_TARGET, offset + ERA_SPLIT_TRANSACTION_BACKEND_UART_RX_WRAP);
    sm_config_set_in_pins(&config, rx_pin);
    sm_config_set_jmp_pin(&config, rx_pin);
    sm_config_set_in_shift(&config, true, false, 32);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
    float div = (float)clock_get_hz(clk_sys) / (float)(8U * g_era_split_transaction_backend_baud);
    sm_config_set_clkdiv(&config, div);
    pio_sm_init(era_split_transaction_backend_pio, era_split_transaction_backend_rx_sm, offset, &config);
    pio_sm_set_enabled(era_split_transaction_backend_pio, era_split_transaction_backend_rx_sm, true);
}

static bool era_split_transaction_backend_pio_init(pin_t tx_pin, pin_t rx_pin) {
    uint pio_idx = pio_get_index(era_split_transaction_backend_pio);
    hal_lld_peripheral_unreset(pio_idx == 0 ? RESETS_ALLREG_PIO0 : RESETS_ALLREG_PIO1);

    if (!era_split_transaction_backend_load_pio_programs() || !era_split_transaction_backend_claim_pio_state_machines()) {
        return false;
    }

    pio_sm_set_enabled(era_split_transaction_backend_pio, era_split_transaction_backend_tx_sm, false);
    pio_sm_set_enabled(era_split_transaction_backend_pio, era_split_transaction_backend_rx_sm, false);
    pio_sm_clear_fifos(era_split_transaction_backend_pio, era_split_transaction_backend_tx_sm);
    pio_sm_clear_fifos(era_split_transaction_backend_pio, era_split_transaction_backend_rx_sm);

    era_split_transaction_backend_pio_tx_init(tx_pin);
    era_split_transaction_backend_pio_rx_init(rx_pin);

    era_split_transaction_backend_enable_owner_irq_sources();

    era_split_transaction_backend_enter_rx_state();
    return true;
}

static bool era_split_transaction_backend_driver_init_required(bool role_changed) {
    if (!g_era_split_transaction_backend_rp2040.transaction_backend_initialized) {
        return true;
    }
    (void)role_changed;
    return false;
}

static bool era_split_transaction_backend_init_role(era_split_transaction_backend_role_t role) {
    era_split_transaction_backend_init();
    if (!era_split_transaction_backend_access_allowed()) {
        return false;
    }
    era_split_transaction_backend_role_t previous_role = g_era_split_transaction_backend_rp2040.transaction_backend_role;
    bool                                 role_changed  = previous_role != role;
    if (role_changed && g_era_split_transaction_backend_rp2040.transaction_backend_initialized) {
        era_split_transaction_backend_rp2040_clear_fifo();
    }

    bool driver_init_required = era_split_transaction_backend_driver_init_required(role_changed);
    if (driver_init_required) {
        g_era_split_transaction_backend_rp2040.transaction_backend_role = role;
        pin_t tx_pin = SERIAL_USART_TX_PIN;
        pin_t rx_pin = SERIAL_USART_TX_PIN;

        bool initialized = era_split_transaction_backend_pio_init(tx_pin, rx_pin);
        if (!initialized) {
            g_era_split_transaction_backend_rp2040.transaction_backend_initialized = false;
            g_era_split_transaction_backend_rp2040.transaction_backend_role        = previous_role;
            return role_changed;
        }
        g_era_split_transaction_backend_rp2040.transaction_backend_initialized = true;
        g_era_split_transaction_backend_rp2040.transaction_backend_init_role   = role;
    }

    g_era_split_transaction_backend_rp2040.transaction_backend_role = role;
    if (g_era_split_transaction_backend_rp2040.transaction_backend_initialized &&
        (role_changed || driver_init_required)) {
        era_split_transaction_backend_rp2040_clear_fifo();
        era_split_transaction_backend_enable_owner_irq_sources();
    }
    return role_changed;
}

void era_split_transaction_backend_init(void) {
    if (g_era_split_transaction_backend_rp2040.initialized) {
        return;
    }
    memset(&g_era_split_transaction_backend_rp2040, 0, sizeof(g_era_split_transaction_backend_rp2040));
    era_split_transaction_backend_rx_sm                = -1;
    era_split_transaction_backend_tx_sm                = -1;
    era_split_transaction_backend_rx_program_offset    = -1;
    era_split_transaction_backend_tx_program_offset    = -1;
    era_split_transaction_backend_rx_error_pending     = false;
    g_era_split_transaction_backend_rp2040.initialized = true;
}

/* The baud, and everything derived from it, in one step. Core0 calls it once
   before the wire is first opened (boot Low), again for a listener's recovery
   step, and again for the agreed raise (era_split_link.h's Reconciliation).
   The listener's step has no relation to interrupt; the raise runs with the
   owner torn down at a shared-clock deadline so both halves change together.
   Re-running the PIO init is safe by construction: the program load and the
   state-machine claim each check for an existing allocation first, so this
   reconfigures two state machines and allocates nothing.

   The scale is a **ceiling** over the compiled speed, so a build that moved
   SERIAL_USART_SPEED to a value the three levels do not divide rounds toward
   more margin rather than less. It is floored at one because a baud at or
   above the compiled speed would otherwise shrink every window below what the
   build was measured at. */
bool era_split_transaction_backend_set_speed(uint32_t baud) {
    if (baud == 0U || baud == g_era_split_transaction_backend_baud) {
        return false;
    }
    uint32_t scale                                    = ((uint32_t)SERIAL_USART_SPEED + baud - 1U) / baud;
    g_era_split_transaction_backend_baud              = baud;
    g_era_split_transaction_backend_wire_scale        = (scale > 1U) ? (uint8_t)scale : 1U;
    g_era_split_transaction_backend_byte_us           = 1000000U * 10U / baud;
    g_era_split_transaction_backend_turnaround_us     = 1000000U * 11U / baud;
    g_era_split_transaction_backend_serial_timeout_us = ERA_SPLIT_TRANSACTION_BACKEND_SERIAL_TIMEOUT_US * g_era_split_transaction_backend_wire_scale;
    if (g_era_split_transaction_backend_rp2040.transaction_backend_initialized) {
        (void)era_split_transaction_backend_pio_init(SERIAL_USART_TX_PIN, SERIAL_USART_TX_PIN);
    }
    return true;
}

bool era_split_transaction_backend_init_initiator(void) {
    return era_split_transaction_backend_init_role(ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR);
}

bool era_split_transaction_backend_init_responder(void) {
    return era_split_transaction_backend_init_role(ERA_SPLIT_TRANSACTION_BACKEND_ROLE_RESPONDER);
}

bool era_split_transaction_backend_role_ready(era_split_transaction_backend_role_t role) {
    return era_split_transaction_backend_access_allowed() &&
           g_era_split_transaction_backend_rp2040.transaction_backend_initialized &&
           g_era_split_transaction_backend_rp2040.transaction_backend_role == role;
}

void era_split_transaction_backend_reset_link_state(void) {
    era_split_transaction_backend_init();
    if (era_split_transaction_backend_access_allowed() && g_era_split_transaction_backend_rp2040.transaction_backend_initialized) {
        uint32_t irq_state                             = era_split_transaction_backend_irq_save();
        era_split_transaction_backend_rx_error_pending = false;
        pio_interrupt_clear(era_split_transaction_backend_pio, 0UL);
        era_split_transaction_backend_irq_restore(irq_state);
        era_split_transaction_backend_rp2040_clear_fifo();
    }
}

void era_split_transaction_backend_clear(void) {
    if (era_split_transaction_backend_access_allowed() && g_era_split_transaction_backend_rp2040.transaction_backend_initialized) {
        era_split_transaction_backend_rp2040_clear_fifo();
    }
}

void era_split_transaction_backend_release(void) {
    era_split_transaction_backend_init();
    era_split_transaction_backend_disable_owner_irq_sources();
    era_split_transaction_backend_force_rx_state();
    g_era_split_transaction_backend_rp2040.transaction_backend_role = ERA_SPLIT_TRANSACTION_BACKEND_ROLE_DISABLED;
    __DMB();
}

bool era_split_transaction_backend_send_owned(const uint8_t *source, size_t size, uint16_t owner_epoch, era_split_transaction_backend_wait_result_t *wait_result) {
    if (wait_result == NULL) {
        return false;
    }
    *wait_result = era_split_transaction_backend_access_result(owner_epoch);
    if (*wait_result != ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK ||
        !g_era_split_transaction_backend_rp2040.transaction_backend_initialized ||
        source == NULL || size == 0) {
        if (*wait_result == ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK) {
            *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_IO_ERROR;
        }
        return false;
    }

    era_split_transaction_backend_rp2040_clear_fifo();
    uint32_t irq_state                             = era_split_transaction_backend_irq_save();
    era_split_transaction_backend_rx_error_pending = false;
    pio_interrupt_clear(era_split_transaction_backend_pio, 0UL);
    era_split_transaction_backend_irq_restore(irq_state);

    era_split_transaction_backend_leave_rx_state();
    bool sent = era_split_transaction_backend_send_impl_owned(source, size, owner_epoch, wait_result);
    bool rx_ready = era_split_transaction_backend_enter_rx_state_owned(owner_epoch, wait_result);
    if (!sent || !rx_ready) {
        era_split_transaction_backend_force_rx_state();
        return false;
    }

    *wait_result = era_split_transaction_backend_access_result(owner_epoch);
    return *wait_result == ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
}

bool era_split_transaction_backend_response_window_begin(uint16_t owner_epoch, uint16_t timeout_ms, era_split_transaction_backend_response_window_t *window, era_split_transaction_backend_wait_result_t *wait_result) {
    if (window == NULL || wait_result == NULL) {
        return false;
    }

    *wait_result = era_split_transaction_backend_access_result(owner_epoch);
    if (*wait_result != ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK ||
        !g_era_split_transaction_backend_rp2040.transaction_backend_initialized ||
        timeout_ms == 0) {
        if (*wait_result == ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK) {
            *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_IO_ERROR;
        }
        return false;
    }

    window->owner_epoch = owner_epoch;
    window->deadline_us = timer_hw->timerawl + era_split_transaction_backend_wire_window_us(timeout_ms);
    *wait_result        = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
    return true;
}

bool era_split_transaction_backend_receive_response_window_until(era_split_transaction_backend_response_window_t *window, uint8_t *destination, size_t size, era_split_transaction_backend_wait_result_t *wait_result) {
    if (window == NULL || destination == NULL || size == 0 || wait_result == NULL) {
        return false;
    }

    size_t read = 0;
    while (read < size) {
        *wait_result = era_split_transaction_backend_access_result(window->owner_epoch);
        if (*wait_result != ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK) {
            return false;
        }
        if (era_split_transaction_backend_rx_error_pending) {
            *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_PIO_ERROR;
            return false;
        }

        uint32_t irq_state = era_split_transaction_backend_irq_save();
        while (read < size && !pio_sm_is_rx_fifo_empty(era_split_transaction_backend_pio, era_split_transaction_backend_rx_sm)) {
            destination[read++] = *((uint8_t *)&era_split_transaction_backend_pio->rxf[era_split_transaction_backend_rx_sm] + 3U);
        }
        era_split_transaction_backend_irq_restore(irq_state);
        if (read == size) {
            break;
        }
        if (era_split_transaction_backend_deadline_expired(window->deadline_us)) {
            *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_TIMEOUT;
            return false;
        }
        /* The initiator's response window parks the way the responder's idle
           and frame windows do: on the RX FIFO source and the deadline. The
           FIFO/error/deadline/epoch predicates above stay the receive
           authority; the wake is a hint (era_invariants.md). */
        era_split_transaction_backend_park_until(window->deadline_us, ERA_SPLIT_TRANSACTION_BACKEND_PARK_WAKE_RX_NOT_EMPTY);
    }

    *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
    return true;
}

static void era_split_transaction_backend_disarm_core1_deadline(void) {
    hw_clear_bits(&timer_hw->inte, ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_MASK);
    timer_hw->intr = ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_MASK;
    NVIC_DisableIRQ((IRQn_Type)ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_IRQ);
    __DMB();
}

static void era_split_transaction_backend_arm_core1_deadline(uint32_t deadline_us) {
    timer_hw->intr = ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_MASK;
    timer_hw->alarm[ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_ALARM] = deadline_us;
    hw_set_bits(&timer_hw->inte, ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_MASK);
    NVIC_EnableIRQ((IRQn_Type)ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_IRQ);
    __DMB();
}

void era_split_transaction_backend_core1_deadline_irq(void) {
    timer_hw->intr = ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_MASK;
    hw_clear_bits(&timer_hw->inte, ERA_SPLIT_TRANSACTION_BACKEND_CORE1_DEADLINE_MASK);
    __DMB();
    __SEV();
}

/* One alarm, one handler, two sequential users. The contract and why sharing is
 * safe are at the declaration in era_split_transaction_backend.h. */
void era_split_transaction_backend_arm_core1_idle_wake(uint32_t deadline_us) {
    era_split_transaction_backend_arm_core1_deadline(deadline_us);
}

/* The one wait primitive of this backend, and the only place core1 parks
 * inside a transaction, in either role. Arms the requested FIFO source(s) and
 * the core1 alarm at deadline_us, parks once on WFE, disarms. Wake-source
 * neutral by construction: a source is a mask bit the caller passes, so a DMA
 * completion later is a bit and an ISR arm, not a second wait loop.
 *
 * No predicate is re-checked after arming except the deadline, and that is
 * enough because every wake this backend relies on is level or sticky: a FIFO
 * source already true when its INTE bit is set interrupts at once; an
 * owner-epoch change from core0 arrives as a SEV that sets this core's event
 * register whether or not WFE has been reached; the PIO error flag is sticky
 * and its source stays enabled. The alarm is the one edge -- an alarm set at a
 * time already past fires only after the 32-bit timer wraps -- so it is the
 * one thing checked after arming. A spurious return (a stale event-register
 * bit) costs the caller one loop pass. WFE runs with interrupts enabled: a
 * pending but masked interrupt does not end a WFE on this core.
 *
 * Alarm sharing with the idle wake stays exactly as declared in the header: a
 * park inside a transaction overwrites an idle arm and the standing service
 * re-arms on its next pass. */
static void era_split_transaction_backend_park_until(uint32_t deadline_us, uint8_t wake_mask) {
    uint32_t irq_state = era_split_transaction_backend_irq_save();
    if ((wake_mask & ERA_SPLIT_TRANSACTION_BACKEND_PARK_WAKE_RX_NOT_EMPTY) != 0) {
        era_split_transaction_backend_set_rx_fifo_irq_enabled(true);
    }
    if ((wake_mask & ERA_SPLIT_TRANSACTION_BACKEND_PARK_WAKE_TX_NOT_FULL) != 0) {
        era_split_transaction_backend_set_tx_fifo_irq_enabled(true);
    }
    era_split_transaction_backend_arm_core1_deadline(deadline_us);
    __DMB();
    bool expired = era_split_transaction_backend_deadline_expired(deadline_us);
    if (expired) {
        era_split_transaction_backend_set_rx_fifo_irq_enabled(false);
        era_split_transaction_backend_set_tx_fifo_irq_enabled(false);
        era_split_transaction_backend_disarm_core1_deadline();
    }
    era_split_transaction_backend_irq_restore(irq_state);
    if (expired) {
        return;
    }
#ifdef ERA_SPLIT_CORE1_PARK_DIAGNOSTICS_ENABLE
    uint32_t park_start_us = timer_hw->timerawl;
#endif
    __WFE();
#ifdef ERA_SPLIT_CORE1_PARK_DIAGNOSTICS_ENABLE
    era_split_transaction_backend_park_us += (uint32_t)(timer_hw->timerawl - park_start_us);
    era_split_transaction_backend_park_count++;
#endif
    irq_state = era_split_transaction_backend_irq_save();
    era_split_transaction_backend_set_rx_fifo_irq_enabled(false);
    era_split_transaction_backend_set_tx_fifo_irq_enabled(false);
    era_split_transaction_backend_irq_restore(irq_state);
    era_split_transaction_backend_disarm_core1_deadline();
    __DMB();
}

#ifdef ERA_SPLIT_CORE1_PARK_DIAGNOSTICS_ENABLE
void era_split_transaction_backend_get_park_diagnostics(uint32_t *count, uint32_t *us) {
    if (count != NULL) {
        *count = era_split_transaction_backend_park_count;
    }
    if (us != NULL) {
        *us = era_split_transaction_backend_park_us;
    }
}
#endif

static bool era_split_transaction_backend_responder_window_ready(uint16_t owner_epoch, era_split_transaction_backend_wait_result_t *wait_result) {
    *wait_result = era_split_transaction_backend_access_result(owner_epoch);
    if (*wait_result != ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK) {
        return false;
    }
    if (!g_era_split_transaction_backend_rp2040.transaction_backend_initialized ||
        g_era_split_transaction_backend_rp2040.transaction_backend_role != ERA_SPLIT_TRANSACTION_BACKEND_ROLE_RESPONDER ||
        era_split_transaction_backend_rx_sm < 0) {
        *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_IO_ERROR;
        return false;
    }
    return true;
}

bool era_split_transaction_backend_responder_idle_window_begin(uint16_t owner_epoch, uint16_t timeout_ms, era_split_transaction_backend_response_window_t *window, era_split_transaction_backend_wait_result_t *wait_result) {
    if (wait_result == NULL) {
        return false;
    }
    *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_IO_ERROR;
    if (window == NULL || timeout_ms == 0 || !era_split_transaction_backend_responder_window_ready(owner_epoch, wait_result)) {
        return false;
    }

    window->owner_epoch = owner_epoch;
    /* The one service window in this unit: how long the responder may sit with
       nothing arriving, which is not a byte count and does not scale. */
    window->deadline_us = timer_hw->timerawl + era_split_transaction_backend_service_window_us(timeout_ms);
    return true;
}

bool era_split_transaction_backend_responder_frame_window_begin(era_split_transaction_backend_response_window_t *window, uint16_t timeout_ms, era_split_transaction_backend_wait_result_t *wait_result) {
    if (wait_result == NULL) {
        return false;
    }
    *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_IO_ERROR;
    if (window == NULL || timeout_ms == 0 || !era_split_transaction_backend_responder_window_ready(window->owner_epoch, wait_result)) {
        return false;
    }

    window->deadline_us = timer_hw->timerawl + era_split_transaction_backend_wire_window_us(timeout_ms);
    return true;
}

/* The responder's wait, on the same primitive the initiator's windows use.
   The predicates -- owner epoch, PIO error, FIFO, deadline -- are the receive
   authority; the park is the wake hint. Nothing is left armed on any exit,
   because the primitive disarms after every park and arms nothing before. */
bool era_split_transaction_backend_receive_responder_until(era_split_transaction_backend_response_window_t *window, uint8_t *destination, size_t size, era_split_transaction_backend_wait_result_t *wait_result) {
    if (window == NULL || destination == NULL || size == 0 || wait_result == NULL) {
        return false;
    }

    size_t read = 0;
    while (read < size) {
        if (!era_split_transaction_backend_responder_window_ready(window->owner_epoch, wait_result)) {
            return false;
        }
        if (era_split_transaction_backend_rx_error_pending) {
            *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_PIO_ERROR;
            return false;
        }

        uint32_t irq_state = era_split_transaction_backend_irq_save();
        while (read < size && !pio_sm_is_rx_fifo_empty(era_split_transaction_backend_pio, era_split_transaction_backend_rx_sm)) {
            destination[read++] = *((uint8_t *)&era_split_transaction_backend_pio->rxf[era_split_transaction_backend_rx_sm] + 3U);
        }
        era_split_transaction_backend_irq_restore(irq_state);
        if (read == size) {
            *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
            return true;
        }
        if (era_split_transaction_backend_deadline_expired(window->deadline_us)) {
            *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_TIMEOUT;
            return false;
        }
        era_split_transaction_backend_park_until(window->deadline_us, ERA_SPLIT_TRANSACTION_BACKEND_PARK_WAKE_RX_NOT_EMPTY);
    }

    *wait_result = ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK;
    return true;
}

void era_split_transaction_backend_get_diagnostics_snapshot(era_split_transaction_backend_diagnostics_t *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->initialized           = g_era_split_transaction_backend_rp2040.transaction_backend_initialized ? 1 : 0;
    snapshot->init_role             = (uint8_t)g_era_split_transaction_backend_rp2040.transaction_backend_init_role;
    snapshot->role                  = (uint8_t)g_era_split_transaction_backend_rp2040.transaction_backend_role;
    snapshot->reinit_on_role_change = ERA_SPLIT_SERIAL_REINIT_ON_ROLE_CHANGE ? 1 : 0;
}
