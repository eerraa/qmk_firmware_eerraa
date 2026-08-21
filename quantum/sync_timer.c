/*
Copyright (C) 2020 Ryan Caltabiano <https://github.com/XScorpion2>

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

If you happen to meet one of the copyright holders in a bar you are obligated
to buy them one pint of beer.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "sync_timer.h"
#include "keyboard.h"

#if defined(SPLIT_KEYBOARD) && !defined(DISABLE_SYNC_TIMER)
volatile int32_t sync_timer_ms;

/* The half whose local clock is the shared clock; every other half adopts
   the source's time through sync_timer_update(). */
__attribute__((weak)) bool sync_timer_is_time_source(void) {
    return is_keyboard_master();
}

void sync_timer_init(void) {
    sync_timer_ms = 0;
}

void sync_timer_update(uint32_t time) {
    if (sync_timer_is_time_source()) return;
    sync_timer_ms = time - timer_read32();
}

uint16_t sync_timer_read(void) {
    if (sync_timer_is_time_source()) return timer_read();
    return sync_timer_read32();
}

uint32_t sync_timer_read32(void) {
    if (sync_timer_is_time_source()) return timer_read32();
    return sync_timer_ms + timer_read32();
}

uint16_t sync_timer_elapsed(uint16_t last) {
    if (sync_timer_is_time_source()) return timer_elapsed(last);
    return TIMER_DIFF_16(sync_timer_read(), last);
}

uint32_t sync_timer_elapsed32(uint32_t last) {
    if (sync_timer_is_time_source()) return timer_elapsed32(last);
    return TIMER_DIFF_32(sync_timer_read32(), last);
}
#endif
