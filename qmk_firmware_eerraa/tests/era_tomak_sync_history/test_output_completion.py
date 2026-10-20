#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the production output boundary with deterministic DMA/PIO mocks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
driver = (root / "platforms/chibios/drivers/vendor/RP/RP2040/ws2812_vendor.c").read_text()
header = (root / "drivers/led/ws2812.h").read_text()


def function(name):
    start = driver.index(name + "(")
    start = driver.rfind("\n", 0, start) + 1
    body = driver.index("{", start)
    depth, end = 1, body + 1
    while depth:
        depth += (driver[end] == "{") - (driver[end] == "}")
        end += 1
    return driver[start:end]


enum_start = header.index("typedef enum {", header.index("ERA_TOMAK_SYNC_OUTPUT_ENABLE"))
enum_end = header.index("} ws2812_transfer_status_t;", enum_start) + len("} ws2812_transfer_status_t;")
prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define WS2812_LED_COUNT 99
#define WS2812_TRST_US 280
#define WS2812_T1H 900
#define WS2812_T1L 350
#define WS2812_T0H 350
#define WS2812_T0L 900
#define MAX(a,b) ((a)>(b)?(a):(b))
#define TIME_MS2I(x) (x)
#define MSG_TIMEOUT (-1)
#define DMA_CTRL_TRIG_READ_ERROR (1U << 30)
#define DMA_CTRL_TRIG_WRITE_ERROR (1U << 29)
typedef uint64_t absolute_time_t;
typedef uint32_t rtcnt_t;
static absolute_time_t LAST_TRANSFER;
static uint64_t now;
static int TRANSFER_COUNTER = 1;
static unsigned locks, signals, launches, fifo;
static bool fail_wait;
static int pio, STATE_MACHINE, dma_channel, RP_DMA_MODE_WS2812;
static uint32_t WS2812_BUFFER[WS2812_LED_COUNT];
static volatile uint32_t txf[1];
typedef struct { uint8_t r, g, b; } ws2812_led_t;
static ws2812_led_t ws2812_leds[WS2812_LED_COUNT];
static void osalSysLock(void) { ++locks; }
static void osalSysUnlock(void) { assert(locks); --locks; }
static void osalSysLockFromISR(void) { osalSysLock(); }
static void osalSysUnlockFromISR(void) { osalSysUnlock(); }
static bool time_reached(absolute_time_t t) { return now >= t; }
static uint64_t time_us_64(void) { return now; }
static void update_us_since_boot(absolute_time_t *t, uint64_t value) { *t = value; }
static unsigned pio_sm_get_tx_fifo_level(int p, int s) { return fifo; }
static void chSemSignalI(int *s) { ++*s; ++signals; }
static int chSemWaitTimeout(int *s, int timeout) {
    if (fail_wait || !*s) return MSG_TIMEOUT;
    --*s; return 0;
}
static void chSemReset(int *s, int count) { *s = count; }
static void busy_wait_until(absolute_time_t t) { if (now < t) now = t; }
static void wait_us(unsigned us) { now += us; }
static void dprintln(const char *s) {}
static void dmaChannelDisableX(int c) {}
static void pio_sm_clear_fifos(int p, int s) {}
static void pio_sm_restart(int p, int s) {}
static uint32_t rgbw8888_to_u32(uint8_t r,uint8_t g,uint8_t b,uint8_t w) { return r; }
static void dmaChannelSetSourceX(int c, uint32_t address) {}
static void dmaChannelSetCounterX(int c, int count) {}
static void dmaChannelSetModeX(int c, int mode) {}
static void dmaChannelEnableX(int c) { ++launches; }
"""
cases = r"""
int main(void) {
#ifdef ERA_TOMAK_SYNC_OUTPUT_ENABLE
    assert(ws2812_transfer_status() == WS2812_TRANSFER_IDLE);
#endif
    ws2812_flush();
    assert(launches == 1 && TRANSFER_COUNTER == 0);
#ifdef ERA_TOMAK_SYNC_OUTPUT_ENABLE
    assert(ws2812_transfer_status() == WS2812_TRANSFER_BUSY);
#endif
    now = 3000; fifo = 2;
    ws2812_dma_callback(0, 0);
    assert(signals == 1 && LAST_TRANSFER == 3370);
    /* DMA finished, but three words and the reset interval are still pending. */
#ifdef ERA_TOMAK_SYNC_OUTPUT_ENABLE
    for (unsigned i=0;i<10;++i) assert(ws2812_transfer_status() == WS2812_TRANSFER_BUSY);
    assert(TRANSFER_COUNTER == 1);
    now = LAST_TRANSFER - 1;
    assert(ws2812_transfer_status() == WS2812_TRANSFER_BUSY);
    ++now;
    assert(ws2812_transfer_status() == WS2812_TRANSFER_COMPLETE);
#endif
    ws2812_flush();
    assert(launches == 2 && TRANSFER_COUNTER == 0);
#ifdef ERA_TOMAK_SYNC_OUTPUT_ENABLE
    /* The old, expired timestamp cannot certify the new DMA. */
    assert(ws2812_transfer_status() == WS2812_TRANSFER_BUSY);
#endif
    fail_wait = true;
    ws2812_flush();
#ifdef ERA_TOMAK_SYNC_OUTPUT_ENABLE
    assert(launches == 2);
    assert(ws2812_transfer_status() == WS2812_TRANSFER_FAILED);
    assert(TRANSFER_COUNTER == 1);
    fail_wait = false;
    ws2812_flush();
    assert(launches == 3 && TRANSFER_COUNTER == 0);
    assert(ws2812_transfer_status() == WS2812_TRANSFER_BUSY);
    ws2812_dma_callback(0, 0);
    uint64_t completed_at = LAST_TRANSFER;
    assert(now < completed_at);
    ws2812_flush();
    assert(now == completed_at && launches == 4);
    assert(ws2812_transfer_status() == WS2812_TRANSFER_BUSY);
    ws2812_dma_callback(0, DMA_CTRL_TRIG_READ_ERROR);
    assert(ws2812_transfer_status() == WS2812_TRANSFER_FAILED);
    now = LAST_TRANSFER;
    assert(ws2812_transfer_status() == WS2812_TRANSFER_FAILED);
    ws2812_flush();
    assert(ws2812_transfer_status() == WS2812_TRANSFER_BUSY);
    ws2812_dma_callback(0, DMA_CTRL_TRIG_WRITE_ERROR);
    assert(ws2812_transfer_status() == WS2812_TRANSFER_FAILED);
#else
    /* Non-TOMAK builds retain the existing recovery/launch behavior. */
    assert(launches == 3);
#endif
    assert(locks == 0);
    puts("PASS DMA submission, PIO tail/reset completion, non-consuming observation, stall and legacy path");
}
"""
with tempfile.TemporaryDirectory(prefix="tomak-output-completion-") as directory:
    for enabled in (False, True):
        define = "#define ERA_TOMAK_SYNC_OUTPUT_ENABLE\n" if enabled else ""
        state = ("static volatile ws2812_transfer_status_t transfer_status = WS2812_TRANSFER_IDLE;\n"
                 + function("ws2812_transfer_status")) if enabled else ""
        source = define + prefix + header[enum_start:enum_end] + "\n" + state
        source += "\n" + function("ws2812_dma_callback")
        source += "\n" + function("sync_ws2812_transfer")
        source += "\n" + function("ws2812_flush") + "\n" + cases
        c = Path(directory) / "output.c"
        exe = Path(directory) / "output.exe"
        c.write_text(source)
        subprocess.run(["gcc", str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
