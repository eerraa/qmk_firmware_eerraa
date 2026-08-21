// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "era_split_transaction_types.h"

typedef enum {
    ERA_SPLIT_TRANSACTION_BACKEND_ROLE_DISABLED = 0,
    ERA_SPLIT_TRANSACTION_BACKEND_ROLE_INITIATOR,
    ERA_SPLIT_TRANSACTION_BACKEND_ROLE_RESPONDER,
} era_split_transaction_backend_role_t;

typedef struct {
    uint8_t initialized;
    uint8_t init_role;
    uint8_t role;
    uint8_t reinit_on_role_change;
} era_split_transaction_backend_diagnostics_t;

typedef enum {
    ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK = 0,
    ERA_SPLIT_TRANSACTION_BACKEND_WAIT_TIMEOUT,
    ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OWNER,
    ERA_SPLIT_TRANSACTION_BACKEND_WAIT_EPOCH,
    ERA_SPLIT_TRANSACTION_BACKEND_WAIT_CANCEL,
    ERA_SPLIT_TRANSACTION_BACKEND_WAIT_RESET,
    ERA_SPLIT_TRANSACTION_BACKEND_WAIT_PIO_ERROR,
    ERA_SPLIT_TRANSACTION_BACKEND_WAIT_IO_ERROR,
} era_split_transaction_backend_wait_result_t;

/* The wait result as a transaction failure, with the caller naming what a
   timeout means to it. One definition because the two IO paths that need it --
   era_split_transaction_io.c on core0 and
   communication_core/era_split_communication_core_storage_service.c on core1 --
   each held a character-identical private copy, and a difference between them
   would be a wrong failure code on one core with nothing in a build able to
   see it.

   `static inline` in the header rather than a call into one unit: both callers
   are wire paths, one of them is core1's, and what was worth removing is the
   second *definition*, not the second instance. */
static inline era_split_transaction_failure_t era_split_transaction_backend_failure_from_wait(era_split_transaction_backend_wait_result_t wait_result, era_split_transaction_failure_t timeout_failure) {
    switch (wait_result) {
        case ERA_SPLIT_TRANSACTION_BACKEND_WAIT_TIMEOUT:
            return timeout_failure;
        case ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OWNER:
            return ERA_SPLIT_TRANSACTION_FAILURE_OWNER;
        case ERA_SPLIT_TRANSACTION_BACKEND_WAIT_EPOCH:
            return ERA_SPLIT_TRANSACTION_FAILURE_EPOCH;
        case ERA_SPLIT_TRANSACTION_BACKEND_WAIT_CANCEL:
            return ERA_SPLIT_TRANSACTION_FAILURE_CANCEL;
        case ERA_SPLIT_TRANSACTION_BACKEND_WAIT_RESET:
            return ERA_SPLIT_TRANSACTION_FAILURE_RESET;
        case ERA_SPLIT_TRANSACTION_BACKEND_WAIT_PIO_ERROR:
            return ERA_SPLIT_TRANSACTION_FAILURE_PIO_ERROR;
        case ERA_SPLIT_TRANSACTION_BACKEND_WAIT_IO_ERROR:
            return ERA_SPLIT_TRANSACTION_FAILURE_IO;
        case ERA_SPLIT_TRANSACTION_BACKEND_WAIT_OK:
        default:
            return ERA_SPLIT_TRANSACTION_FAILURE_NONE;
    }
}

typedef struct {
    uint16_t owner_epoch;
    uint32_t deadline_us;
} era_split_transaction_backend_response_window_t;

#ifdef ERA_SPLIT_TRANSACTION_TIMING_DIAGNOSTICS_ENABLE
typedef struct {
    uint32_t tx_us;
    uint32_t wait_rx_us;
} era_split_transaction_backend_send_timing_t;
#endif

void era_split_transaction_backend_init(void);
/* The wire's rate, as a baud, and everything derived from it. Core0 pushes the
   number in; the backend never learns that levels exist, which is what keeps
   both EEPROM and the link unit off this side of the boundary. Byte time,
   turnaround, the send bound and the window scale all come out of this one
   argument, so they cannot disagree about what the wire is doing. Returns
   whether anything moved. */
bool era_split_transaction_backend_set_speed(uint32_t baud);
/* ceil(SERIAL_USART_SPEED / baud) -- 1 / 2 / 4 at the three compiled levels.
   Read by the one wire-time window that is computed outside this unit (the
   storage lane's bulk-page body deadline) and by the scheduler, which scales
   the DUAL-HOST poll period with it. */
uint8_t era_split_transaction_backend_wire_scale(void);
bool era_split_transaction_backend_init_initiator(void);
bool era_split_transaction_backend_init_responder(void);
bool era_split_transaction_backend_role_ready(era_split_transaction_backend_role_t role);
void era_split_transaction_backend_reset_link_state(void);
void era_split_transaction_backend_clear(void);
void era_split_transaction_backend_release(void);
bool era_split_transaction_backend_send_owned(const uint8_t                               *source,
                                              size_t                                       size,
                                              uint16_t                                     owner_epoch,
                                              era_split_transaction_backend_wait_result_t *wait_result);
bool era_split_transaction_backend_response_window_begin(uint16_t                                         owner_epoch,
                                                         uint16_t                                         timeout_ms,
                                                         era_split_transaction_backend_response_window_t *window,
                                                         era_split_transaction_backend_wait_result_t     *wait_result);
bool era_split_transaction_backend_receive_response_window_until(era_split_transaction_backend_response_window_t *window,
                                                                 uint8_t                                         *destination,
                                                                 size_t                                           size,
                                                                 era_split_transaction_backend_wait_result_t     *wait_result);
bool era_split_transaction_backend_responder_idle_window_begin(uint16_t                                         owner_epoch,
                                                               uint16_t                                         timeout_ms,
                                                               era_split_transaction_backend_response_window_t *window,
                                                               era_split_transaction_backend_wait_result_t     *wait_result);
bool era_split_transaction_backend_responder_frame_window_begin(era_split_transaction_backend_response_window_t *window,
                                                                uint16_t                                         timeout_ms,
                                                                era_split_transaction_backend_wait_result_t     *wait_result);
bool era_split_transaction_backend_receive_responder_until(era_split_transaction_backend_response_window_t *window,
                                                           uint8_t                                         *destination,
                                                           size_t                                           size,
                                                           era_split_transaction_backend_wait_result_t     *wait_result);
void era_split_transaction_backend_core1_deadline_irq(void);
/* The idle wake for a core1 actor that has a deadline of its own. It arms the
 * same alarm and the same handler the response/responder windows use, which is
 * the point rather than a shortcut: the backend owns core1's timer, and a
 * second owner of one alarm is how two schedulers silently overwrite each
 * other.
 *
 * Sharing is safe because the two uses are strictly sequential on one core.
 * A window arm overwrites an idle arm, and the caller re-arms on the next loop
 * pass -- which it always reaches, because a pass that ran a transaction
 * continues rather than parking. The reverse cannot happen: an idle arm is
 * only issued on a pass where no transaction ran.
 *
 * Without this a standing exchange parks on WFE with nothing to wake it for
 * its own period. The alternative is not parking, and that is worse than it
 * looks: core1 spinning reads striped main SRAM and steals bandwidth from the
 * matrix scan, which is the one cost the standing exchange exists to avoid
 * paying. */
void era_split_transaction_backend_arm_core1_idle_wake(uint32_t deadline_us);
void era_split_transaction_backend_get_diagnostics_snapshot(era_split_transaction_backend_diagnostics_t *snapshot);
#ifdef ERA_SPLIT_CORE1_PARK_DIAGNOSTICS_ENABLE
/* Boot-cumulative: how many times the backend parked core1 on WFE inside a
 * wait -- the responder's windows, and the initiator's response window, TX
 * FIFO-full wait and drain -- and the
 * microseconds those parks lasted in total. The lifecycle's own idle park is
 * counted separately (era_split_communication_core_diagnostics.h); a reader
 * takes deltas of both against the elapsed window for core1's sleep share. */
void era_split_transaction_backend_get_park_diagnostics(uint32_t *count, uint32_t *us);
#endif
