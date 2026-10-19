#include <ch.h>

#include "timer.h"

static uint32_t ticks_offset = 0;
static uint32_t last_ticks   = 0;
static uint32_t ms_offset    = 0;
static uint32_t saved_ms     = 0;
#if CH_CFG_ST_RESOLUTION < 32
static uint32_t last_systime = 0;
static uint32_t overflow     = 0;
#endif

#if defined(ERA_TIMER_MS_CACHE)
/* ERA: timer_read32() answers from a once-per-millisecond cache.
 *
 * On this platform the conversion in timer_read32() is a system lock, a
 * 64-bit multiply and a 64-bit divide (TIME_I2MS) -- about 2 us per call,
 * measured -- and the ERA image asks it three to four times per matrix scan
 * pass (the split scheduler's housekeeping-due gate, rgb_matrix_task()'s
 * frame timers) with nothing between the calls that could have moved the
 * answer. At 22 kHz that was 16 % of core0, and the whole of one measured
 * scan-rate regression. The value moves 1000 times a second, so the
 * conversion runs once per millisecond and every other call answers from
 * two words: the system tick at which the cached millisecond began, and the
 * millisecond itself. A cached read is one counter read and one comparison,
 * and it returns exactly what the conversion would have -- same offsets, same
 * TIME_I2MS rounding -- so no consumer can tell a cached answer from a
 * converted one, and no stamp comparison across callers changes meaning.
 *
 * The pair is published under a sequence word rather than read under the
 * system lock. Its only writer is the slow path, which already holds the lock
 * with interrupts masked, so no reader on this single reading core ever
 * observes a half-written pair; what the sequence detects is a writer that
 * ran in an interrupt between a reader's loads, and the reader's answer to
 * that is the slow path -- never a spin. Both paths read the same counter
 * through the same accessor, so there is no second clock to drift.
 *
 * TIME_I2MS rounds up: millisecond m covers the ticks ((m-1)*T, m*T] past
 * the offset, T ticks per millisecond, so its first tick is (m-1)*T + 1.
 * Tick 0 alone rounds to a millisecond of its own and is not cached. */
#    if CH_CFG_ST_RESOLUTION < 32
#        error "ERA_TIMER_MS_CACHE assumes a 32-bit system tick counter"
#    endif
#    if (CH_CFG_ST_FREQUENCY % 1000) != 0
#        error "ERA_TIMER_MS_CACHE assumes a whole number of system ticks per millisecond"
#    endif
#    define ERA_TIMER_TICKS_PER_MS ((uint32_t)(CH_CFG_ST_FREQUENCY / 1000))
static volatile uint32_t ms_cache_seq   = 0; // bumped before and after every write; a reader that sees it move retakes the slow path
static volatile uint32_t ms_cache_value = 0; // timer_read32() for the ticks [start, start + ERA_TIMER_TICKS_PER_MS)
// System tick at which ms_cache_value became the answer. It starts one millisecond *before* tick 0, so nothing answers
// from the cache before the slow path has published a real pair: that window is where the counter cannot be until it
// wraps 71 minutes after power-on, and every read from timer_init() onwards has long since published.
static volatile uint32_t ms_cache_start_tick = (uint32_t)0 - ERA_TIMER_TICKS_PER_MS;

// Must be called with the system lock held.
static inline void ms_cache_publish_locked(uint32_t start_tick, uint32_t value) {
    ms_cache_seq++;
    ms_cache_start_tick = start_tick;
    ms_cache_value      = value;
    ms_cache_seq++;
}

// Must be called with the system lock held, wherever the offsets change epoch: a pair computed under the previous
// offsets cannot answer for the new ones, so the published window is moved to end one millisecond before `now_tick`
// and every read until the next slow path misses.
static inline void ms_cache_invalidate_locked(uint32_t now_tick) {
    ms_cache_publish_locked(now_tick - ERA_TIMER_TICKS_PER_MS, 0);
}
#endif

// Get the current system time in ticks as a 32-bit number.
// This function must be called from within a system lock zone (so that it can safely use and update the static data).
static inline uint32_t get_system_time_ticks(void) {
    uint32_t systime = (uint32_t)chVTGetSystemTimeX();

#if CH_CFG_ST_RESOLUTION < 32
    // If the real system timer resolution is less than 32 bits, provide the missing bits by checking for the counter
    // overflow.  For this to work, this function must be called at least once for every overflow of the system timer.
    // In the 16-bit case, the corresponding times are:
    //    - CH_CFG_ST_FREQUENCY = 100000, overflow will occur every ~0.65 seconds
    //    - CH_CFG_ST_FREQUENCY = 10000, overflow will occur every ~6.5 seconds
    //    - CH_CFG_ST_FREQUENCY = 1000, overflow will occur every ~65 seconds
    if (systime < last_systime) {
        overflow += ((uint32_t)1) << CH_CFG_ST_RESOLUTION;
    }
    last_systime = systime;
    systime += overflow;
#endif

    return systime;
}

#if CH_CFG_ST_RESOLUTION < 32
static virtual_timer_t update_timer;

// Update the system tick counter every half of the timer overflow period; this should keep the tick counter correct
// even if something blocks timer interrupts for 1/2 of the timer overflow period.
#    define UPDATE_INTERVAL (((sysinterval_t)1) << (CH_CFG_ST_RESOLUTION - 1))

// VT callback function to keep the overflow bits of the system tick counter updated.
static void update_fn(struct ch_virtual_timer *timer, void *arg) {
    (void)arg;
    chSysLockFromISR();
    get_system_time_ticks();
    chVTSetI(&update_timer, UPDATE_INTERVAL, update_fn, NULL);
    chSysUnlockFromISR();
}
#endif

// The highest multiple of CH_CFG_ST_FREQUENCY that fits into uint32_t.  This number of ticks will necessarily
// correspond to some integer number of seconds.
#define OVERFLOW_ADJUST_TICKS ((uint32_t)((UINT32_MAX / CH_CFG_ST_FREQUENCY) * CH_CFG_ST_FREQUENCY))

// The time in milliseconds which corresponds to OVERFLOW_ADJUST_TICKS ticks (this is a precise conversion, because
// OVERFLOW_ADJUST_TICKS corresponds to an integer number of seconds).
#define OVERFLOW_ADJUST_MS (TIME_I2MS(OVERFLOW_ADJUST_TICKS))

void timer_init(void) {
    timer_clear();
#if CH_CFG_ST_RESOLUTION < 32
    chVTObjectInit(&update_timer);
    chVTSet(&update_timer, UPDATE_INTERVAL, update_fn, NULL);
#endif
}

void timer_clear(void) {
    chSysLock();
    ticks_offset = get_system_time_ticks();
    last_ticks   = 0;
    ms_offset    = 0;
#if defined(ERA_TIMER_MS_CACHE)
    ms_cache_invalidate_locked(ticks_offset);
#endif
    chSysUnlock();
}

__attribute__((weak)) void platform_timer_save_value(uint32_t value) {
    saved_ms = value;
}

__attribute__((weak)) uint32_t platform_timer_restore_value(void) {
    return saved_ms;
}

void timer_restore(void) {
    chSysLock();
    ticks_offset = get_system_time_ticks();
    last_ticks   = 0;
    ms_offset    = platform_timer_restore_value();
#if defined(ERA_TIMER_MS_CACHE)
    ms_cache_invalidate_locked(ticks_offset);
#endif
    chSysUnlock();
}

void timer_save(void) {
    platform_timer_save_value(timer_read32());
}

uint16_t timer_read(void) {
    return (uint16_t)timer_read32();
}

uint32_t timer_read32(void) {
#if defined(ERA_TIMER_MS_CACHE)
    {
        // Load order matters and every load is volatile: the sequence first, the pair, the counter, then the sequence
        // again. Equal sequences mean no writer ran between the loads, so the pair is consistent; the counter inside
        // the pair's window means the pair is the answer for it.
        uint32_t seq   = ms_cache_seq;
        uint32_t start = ms_cache_start_tick;
        uint32_t value = ms_cache_value;
        uint32_t now   = (uint32_t)chVTGetSystemTimeX();
        if ((uint32_t)(now - start) < ERA_TIMER_TICKS_PER_MS && seq == ms_cache_seq) {
            return value;
        }
    }
#endif
    syssts_t sts   = chSysGetStatusAndLockX();
    uint32_t ticks = get_system_time_ticks() - ticks_offset;
    if (ticks < last_ticks) {
        // The 32-bit tick counter overflowed and wrapped around.  We cannot just extend the counter to 64 bits here,
        // because TIME_I2MS() may encounter overflows when handling a 64-bit argument; therefore the solution here is
        // to subtract a reasonably large number of ticks from the tick counter to bring its value below the 32-bit
        // limit again, and then add the equivalent number of milliseconds to the converted value.  (Adjusting just the
        // converted value to account for 2**32 ticks is not possible in general, because 2**32 ticks may not correspond
        // to an integer number of milliseconds).
        ticks -= OVERFLOW_ADJUST_TICKS;
        ticks_offset += OVERFLOW_ADJUST_TICKS;
        ms_offset += OVERFLOW_ADJUST_MS;
    }
    last_ticks              = ticks;
    uint32_t ms_offset_copy = ms_offset; // read while still holding the lock to ensure a consistent value
#if defined(ERA_TIMER_MS_CACHE)
    // The conversion runs under the lock here so that the pair it publishes belongs to the offsets it was read
    // under; it is the once-per-millisecond path, so the lock is held about a microsecond longer at 1 kHz.
    uint32_t ms_rel = (uint32_t)TIME_I2MS(ticks);
    if (ticks != 0) {
        ms_cache_publish_locked(ticks_offset + (ms_rel - 1) * ERA_TIMER_TICKS_PER_MS + 1, ms_rel + ms_offset_copy);
    }
    chSysRestoreStatusX(sts);

    return ms_rel + ms_offset_copy;
#else
    chSysRestoreStatusX(sts);

    return (uint32_t)TIME_I2MS(ticks) + ms_offset_copy;
#endif
}
