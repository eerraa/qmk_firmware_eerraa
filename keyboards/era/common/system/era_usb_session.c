// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* The ERA sleep decision, and why it takes two answers instead of one.
 *
 * ERA policy (owner decision 2026-08-11): where a board enables lighting sleep,
 * the sleep decision consumes both the host's explicit USB suspend and the loss
 * of USB frames. The failure it exists for is a host that dies without ever
 * saying so - a forced shutdown behind a port that keeps supplying power - and
 * the visible symptom is the RGB effect running on forever.
 *
 * The two are not two events on the wire, and this is the sentence to read
 * before changing anything here. USB suspend IS frame loss: the host suspends a
 * device by stopping SOF, and the controller latches SUSPEND after 3 ms of an
 * idle bus. So what the second arm buys is not a second signal, it is a second
 * *detector*, and one that does not depend on the ChibiOS/LLD state machine
 * having survived the sequence. That machine can be woken back out of
 * USB_SUSPENDED by control traffic while the RP SIE still reports the bus
 * suspended -- the case the RP_USB_SYNC_SUSPEND_AFTER_REMOTE_WAKEUP_SET delta in
 * the pinned ChibiOS submodule repairs for split boards -- and after that, no
 * amount of waiting produces another SUSPEND event. This arm reads the frame
 * counter register directly, so it is true whenever frames really have stopped,
 * whatever the state machine believes.
 *
 * The counter is polled, not interrupt-driven, and that is deliberate: the RP
 * LLD keeps USB_INTE_DEV_SOF disabled unless a board installs a sof_cb, so an
 * interrupt would mean enabling a 1 kHz IRQ to learn something a register read
 * already tells us. era_split_authority_reducer.c consumes this same sampler
 * for a different question -- authority freshness, over a 10 ms window -- and
 * it calls era_usb_session_sample_frame_age() at its own call site rather than
 * reading an age an earlier pass left behind; the staleness bound that
 * adjacency buys is written at that call. It kept a duplicate sampler until
 * 2026-08-11; one reader is the point now, because every additional reader of
 * this register multiplies the side-effect exposure below.
 *
 * READING USB->SOFRD IS NOT FREE, and this paragraph is the reason the sampler
 * below is guarded rather than a bare read. The RP2040 clears the DEV_SOF
 * interrupt flag when SOF_RD is read ("Cleared by reading SOF_RD",
 * lib/pico-sdk/.../hardware/regs/usb.h at USB_INTR_DEV_SOF), and that flag is
 * load-bearing for exactly one path: usb_lld_wakeup_host() arms
 * USB_INTE_DEV_SOF when ERA asks for a remote wake, on the comment "remote
 * wakeup doesn't trigger the wakeup interrupt, therefore we use the SOF
 * interrupt to detect resume of the bus" (hal_usb_lld.h), and the SOF handler
 * is where _usb_wakeup() then runs (hal_usb_lld.c). So an unguarded poll can
 * consume the one interrupt that ends the suspend ERA itself requested.
 *
 * Two guards, and each is also correct on its own terms rather than being a
 * cost dodge. While INTE_DEV_SOF is set the ISR owns the register, so the
 * sampler stands off entirely -- and standing off reports the truth, because a
 * pending remote wake means frames really are absent: if the wake lands the LLD
 * clears the bit and the next sample sees a moved counter, and if the host is
 * dead the bit stays set and the age keeps growing, which is the answer we
 * want. On top of that the read is rate-limited to once per millisecond,
 * because SOF is a 1 kHz signal and this task runs at the scan rate: sampling
 * eighteen times per frame learns nothing and multiplies the exposure above by
 * eighteen.
 *
 * The never-enumerated port is excluded on purpose. A board plugged into a
 * charger sees no frames from the first millisecond, and calling that a
 * sleeping host would turn every power bank into an off switch. The latch below
 * asks a different question -- has a host ever configured this board since it
 * powered on -- so what the arm actually detects is a host that WAS there.
 */

#include "era_usb_session.h"

#include "quantum.h"
#include "suspend.h"
#include "timer.h"
#include "usb_device_state.h"

#if defined(PROTOCOL_CHIBIOS)
#    include <hal.h>
#    include "usb_main.h" /* USB_DRIVER, for the remap's configuration read */
#endif
#if defined(MCU_RP)
#    include "hardware/structs/timer.h" /* timer_hw->timerawl, the raw microsecond counter */
#endif

#if defined(MCU_RP) && defined(PROTOCOL_CHIBIOS) && defined(USB_SOF_RD_COUNT_Pos) && defined(USB_SOF_RD_COUNT_Msk)
#    define ERA_USB_SESSION_HAS_SOF_SAMPLE 1
#else
#    define ERA_USB_SESSION_HAS_SOF_SAMPLE 0
#endif

/* Frames arrive every millisecond, and the controller's own suspend detector
   fires at 3 ms, so anything in this range is far past a host hiccup and far
   short of a user noticing. It is long rather than short because this arm is
   the backstop for the controller's, not a race with it: firing early would
   mean sleeping through a bus the LLD is still bringing back. */
#ifndef ERA_USB_SESSION_SOF_STALE_MS
#    define ERA_USB_SESSION_SOF_STALE_MS 300
#endif
#define ERA_USB_SESSION_SOF_STALE_US (ERA_USB_SESSION_SOF_STALE_MS * 1000UL)

/* Longer than a host's re-enumeration, shorter than an unplug the owner would
   read as Apply tearing the pair. Independent of the VIA quiet bound: that
   number is the application's silence, this one is the bus-down window. */
#ifndef ERA_USB_SESSION_REATTACH_HOLD_MS
#    define ERA_USB_SESSION_REATTACH_HOLD_MS 2000
#endif

static uint32_t era_usb_session_sof_last_change_us;
static uint32_t era_usb_session_sof_last_read_us;
static uint16_t era_usb_session_sof_count;
static bool     era_usb_session_sof_seen;
static bool     era_usb_session_host_seen;

/* This unit keeps time in raw microseconds (timer_hw->timerawl, one register
   read) and NOT through timer_read32(), and the choice is priced, not
   stylistic. timer_read32() on this platform is an interrupt lock plus a
   64-bit multiply-and-divide (tick-to-ms), ~2 us apiece — and this unit's
   questions change at 1 kHz while its callers run at the scan rate. The
   2026-08-15 qwin bisect priced the difference: the passes that asked these
   questions through four to five conversions each scan cost the scan loop
   ~12 us and a 22 kHz scan rate read 16.4 kHz, every step of the fall
   bracketing to a commit that added conversions to this chain. The raw
   counter wraps at ~71.6 min rather than 49.7 days; every comparison below
   is wrap-safe unsigned subtraction, and the one value that could outlive a
   wrap — the change stamp under a bus that stays dead for over an hour — is
   saturated at twice the stale threshold inside the sampler, so an age never
   aliases back to "fresh". frames_lost() reads the pass's cached sample
   instant instead of taking its own reading — its thresholds are hundreds of
   milliseconds, so a value one pass old is the same answer. */
static uint32_t era_usb_session_sampled_now_us;
static bool     era_usb_session_reattach_live;
static uint32_t era_usb_session_reattach_started_ms;

#if !defined(SPLIT_KEYBOARD)
static bool era_usb_session_applied;
#endif

/* Zero is the never-stamped sentinel as well as a timestamp, so a real zero
   reading is stamped 1 - the same trade era_flash_slice.c makes, at the cost
   of at most 1 us per 71.6-minute wrap. */
static uint32_t era_usb_session_now(void) {
#if defined(MCU_RP)
    uint32_t now = timer_hw->timerawl;
#else
    uint32_t now = timer_read32();
#endif
    return now == 0 ? 1 : now;
}

/* The remap, and the one copy of it. An unconfigured SUSPEND is what a host
   death behind a powered port leaves behind, and it is also the ordinary state
   of a split half whose USB was never plugged in -- so reporting it as SUSPEND
   would have a PEER half declare its own host asleep and darken lighting the
   HOST drives over the wire. Reading it as INIT is what keeps that from
   happening, and the sleep decision's frame arm covers the case it costs.

   Lived verbatim in era_split_keyboard.c and era_split_authority_reducer.c
   until 2026-08-11. Two copies of one predicate is how the sleep decision and
   the authority decision come to disagree about what state the board is in. */
uint8_t era_usb_session_configure_state(void) {
    uint8_t configure_state = (uint8_t)usb_device_state_get_configure_state();
#if defined(MCU_RP) && defined(PROTOCOL_CHIBIOS)
    if (configure_state == USB_DEVICE_STATE_SUSPEND && USB_DRIVER.configuration == 0) {
        return USB_DEVICE_STATE_INIT;
    }
#endif
    return configure_state;
}

bool era_usb_session_sample_frame_age(uint32_t *age_ms) {
    if (usb_device_state_get_configure_state() == USB_DEVICE_STATE_CONFIGURED) {
        era_usb_session_host_seen = true;
    }

#if ERA_USB_SESSION_HAS_SOF_SAMPLE
    uint32_t now                   = era_usb_session_now();
    era_usb_session_sampled_now_us = now;

    /* The rate limit first, and the USB register second, so the scan-rate
       fast path is one counter read plus arithmetic and nothing else: SOF is
       a 1 kHz signal, so inside one millisecond neither the counter nor the
       ISR-ownership answer can have changed, and reading USB->INTE every
       pass was itself a bus access the fast path has no use for. */
    if (era_usb_session_sof_last_read_us == 0 || (uint32_t)(now - era_usb_session_sof_last_read_us) >= 1000) {
        /* One reason remains to take no reading this millisecond, and it is
           not an error: INTE_DEV_SOF set means a remote wake is pending and
           the ISR owns this register. Standing off leaves the stamps where
           they are, which is the true answer — the unobserved span keeps
           growing until the wake lands or the staleness clauses speak. */
        if ((USB->INTE & USB_INTE_DEV_SOF) == 0) {
            uint32_t unobserved_us           = era_usb_session_sof_last_read_us != 0 ? (uint32_t)(now - era_usb_session_sof_last_read_us) : 0;
            era_usb_session_sof_last_read_us = now;

            uint16_t sof_count = (uint16_t)((USB->SOFRD & USB_SOF_RD_COUNT_Msk) >> USB_SOF_RD_COUNT_Pos);
            if (!era_usb_session_sof_seen || era_usb_session_sof_count != sof_count) {
                era_usb_session_sof_seen           = true;
                era_usb_session_sof_count          = sof_count;
                era_usb_session_sof_last_change_us = now;
            } else if (unobserved_us >= ERA_USB_SESSION_SOF_STALE_US) {
                /* An equal counter across an unobserved span is not evidence
                   frames stopped: the register is 11 bits and wraps every
                   2048 ms, so a long-enough gap can alias to equality, and the
                   span itself was not watched. Restamp and let the next
                   contiguous window decide — a genuinely dead bus re-earns the
                   age within one threshold. */
                era_usb_session_sof_last_change_us = now;
            } else if ((uint32_t)(now - era_usb_session_sof_last_change_us) > 2u * ERA_USB_SESSION_SOF_STALE_US) {
                /* The wrap saturation the state-block comment promises: a bus
                   that stays dead under a continuous watch grows this age
                   without bound, and a raw-microsecond age wraps at ~71.6 min,
                   which would read as one sub-threshold flicker of "fresh"
                   per wrap. Holding the stamp at twice the threshold keeps
                   frames_lost saturated-true, and a revived bus still
                   collapses the age on its first counted frame. */
                era_usb_session_sof_last_change_us = now - 2u * ERA_USB_SESSION_SOF_STALE_US;
            }
        }
    }

    if (!era_usb_session_sof_seen) {
        return false;
    }
    if (age_ms != NULL) {
        *age_ms = (uint32_t)(now - era_usb_session_sof_last_change_us) / 1000u;
    }
    return true;
#else
    (void)age_ms;
    return false;
#endif
}

/* The resume restamp, and it is a defect fix rather than an optimisation.
   QMK's suspend loop stops keyboard_task(), so this unit does not sample while
   the loop runs and the age is frozen at whatever it was. On the pass after the
   loop exits, the bus may have resumed before the first SOF has actually
   arrived - the LLD wakes the driver on RESUME_FROM_HOST or a SETUP packet as
   well - so a sampler reading a frozen age would report frames lost and blank
   the lighting QMK has just relit, for the millisecond until the first frame
   lands. Stamping the window at the resume edge removes the window rather than
   racing it. */
#if !defined(SPLIT_KEYBOARD)
static void era_usb_session_note_resume(void) {
    era_usb_session_sof_last_change_us = era_usb_session_now();
}
#endif

void era_usb_session_note_firmware_reattach(void) {
    era_usb_session_reattach_live       = true;
    era_usb_session_reattach_started_ms = timer_read32();
    /* The bounce stops frames by construction. Stamping the window here is
       what keeps the first resolve after the hold from reading the freeze as
       loss — the same repair era_usb_session_note_resume() makes for QMK's
       suspend loop on a non-split board. */
    era_usb_session_sof_last_change_us = era_usb_session_now();
    era_usb_session_sof_last_read_us   = era_usb_session_sof_last_change_us;
}

bool era_usb_session_firmware_reattach_hold(void) {
    if (!era_usb_session_reattach_live) {
        return false;
    }
    if (timer_elapsed32(era_usb_session_reattach_started_ms) >= ERA_USB_SESSION_REATTACH_HOLD_MS) {
        era_usb_session_reattach_live = false;
        return false;
    }
    return true;
}

bool era_usb_session_frames_lost(void) {
#if ERA_USB_SESSION_HAS_SOF_SAMPLE
    if (era_usb_session_firmware_reattach_hold()) {
        return false;
    }
    if (!era_usb_session_host_seen || !era_usb_session_sof_seen) {
        return false;
    }
    /* The age is evidence only while the observation is fresh — the
       observation-gap doctrine the counter compare has always honored: an
       unobserved span is not evidence of absence. Without this clause a
       flash window starved the sampler while the change-stamp froze, and
       the first resolve to run before the sampler restamped read the
       frozen age past the threshold and pulsed lighting sleep for exactly
       one frame — the EEPROM SYNC lamp's mid-era blink, device-convicted
       2026-08-14 with `slp` rising at `brkms` to the millisecond on
       consecutive operations. A stale read returns false; one fresh sample
       re-validates either verdict, because a live bus moves the counter
       and collapses the age while a dead one leaves it standing for the
       next pass to report. */
    /* Ages against the pass's cached conversion, not two fresh ones. The
       sampler runs earlier in the same keyboard pass (era_common_features.c
       orders it ahead of the resolver's consumers), so the cache is at most
       one pass old against thresholds of hundreds of milliseconds — and this
       function is called at the scan rate, where two timer conversions per
       call were a measured piece of the 2026-08-15 scan-rate regression. */
    uint32_t now = era_usb_session_sampled_now_us;
    if (now == 0 || era_usb_session_sof_last_read_us == 0 ||
        (uint32_t)(now - era_usb_session_sof_last_read_us) >= ERA_USB_SESSION_SOF_STALE_US) {
        return false;
    }
    return (uint32_t)(now - era_usb_session_sof_last_change_us) >= ERA_USB_SESSION_SOF_STALE_US;
#else
    return false;
#endif
}

#if !defined(SPLIT_KEYBOARD)
/* A non-split board keeps QMK's own suspend loop - it does not set
   NO_USB_STARTUP_CHECK, which is the switch that deletes it and the reason the
   split layer had to rebuild this decision from parts. So this unit adds the
   frame-loss arm to that loop rather than replacing it, and the two agree by
   construction: both mean sleep, and both drive the same two quantum entry
   points, so a board's lighting cannot end up half-suspended.
 *
 * Ordering matters in exactly one direction. While QMK's loop is running,
 * keyboard_task() does not, so this task does not run either and the loop owns
 * the decision. What this arm is for is the moment after that loop exits with
 * the bus still dead: suspend_wakeup_init_quantum() has just turned the
 * lighting back on, frames are still absent, and one pass later this puts it
 * back out.
 *
 * The two hooks below are what make that repeatable rather than a one-shot.
 * They track what actually happened to the lighting whoever drove it, so this
 * unit never has to guess whether QMK's loop moved it underneath. They are
 * defined only for a non-split build: the three sirind split boards define
 * their own, and a future non-split board that wants them will fail the link
 * rather than silently lose one of these assignments. */
void suspend_power_down_kb(void) {
    era_usb_session_applied = true;
    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    era_usb_session_applied = false;
    /* Non-split only, and that is not an oversight: the frozen-age window this
       closes is created by QMK's suspend loop stopping keyboard_task(), and a
       split board deletes that loop with NO_USB_STARTUP_CHECK. There the
       sampler never stops, so there is no frozen age to restamp. */
    era_usb_session_note_resume();
    suspend_wakeup_init_user();
}

static void era_usb_session_apply(void) {
    bool sleep = era_usb_session_frames_lost();
    if (sleep == era_usb_session_applied) {
        return;
    }

    if (sleep) {
        suspend_power_down_quantum();
    } else {
        suspend_wakeup_init_quantum();
    }
}
#endif

void era_usb_session_task(void) {
#if ERA_USB_SESSION_HAS_SOF_SAMPLE
    /* The warm-keeping call, at the rate the answers can change rather than
       the rate the loop runs: if any reader sampled inside this millisecond
       (this task last pass, or the authority reducer at its own site), there
       is nothing new to learn and the pass keeps its microseconds. The
       freshness contracts this feeds are hundreds of milliseconds
       (frames_lost) and ten (the reducer, which samples for itself). */
    if (era_usb_session_sampled_now_us != 0 &&
        (uint32_t)(era_usb_session_now() - era_usb_session_sampled_now_us) < 1000) {
        return;
    }
#endif
    (void)era_usb_session_sample_frame_age(NULL);
#if !defined(SPLIT_KEYBOARD)
    era_usb_session_apply();
#endif
}
