#!/usr/bin/env python3
# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production discovery period/due/completion functions with fake time.

Run in the synchronized WSL build tree. Extracted function bodies are production
source, not a second scheduler implementation. Registers, queue handoff and
physical timings are outside this test; the firmware build checks their wiring.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_source_contracts import source, body

TIMING = "split/scheduler/era_split_transport_scheduler_timing.c"
ROUTES = "split/scheduler/era_split_transport_scheduler_routes.c"


def production_unit():
    header = source("split/scheduler/era_split_transport_scheduler_internal.h")
    constants = []
    for name in ("ERA_SPLIT_WIRE_BOOTSTRAP_PERIOD_MS", "ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER",
                 "ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS"):
        value = re.search(r"^#\s*define\s+" + name + r"\s+(\d+)\b", header, re.M)
        if value is None:
            raise AssertionError("Missing numeric discovery default: " + name)
        constants.append(f"#define {name} {value[1]}\n")
    declarations = [
        (TIMING, "void era_split_transport_scheduler_reset_session_probe_backoff(void)"),
        (TIMING, "static uint32_t era_split_transport_scheduler_bootstrap_period_ms(void)"),
        (TIMING, "static bool era_split_transport_scheduler_period_due(uint32_t period_ms)"),
        (TIMING, "static bool era_split_transport_scheduler_bootstrap_due(void)"),
        (TIMING, "bool era_split_transport_scheduler_local_status_revalidation_due(void)"),
        (ROUTES, "static void era_split_transport_scheduler_mark_peer_stale_revalidation(void)"),
        (ROUTES, "static void era_split_transport_scheduler_note_attach_status_request_attempt(era_split_transaction_engine_result_t result, bool request_sent, bool stale_on_no_response)"),
    ]
    functions = []
    for path, declaration in declarations:
        name = re.search(r"(\w+)\(", declaration)[1]
        functions.append(declaration + " {\n" + body(path, name) + "\n}\n")
    return "\n".join(constants) + STUBS + "\n".join(functions) + CASES


STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef enum { ERA_SPLIT_TRANSACTION_RESULT_OK, TEST_TIMEOUT } era_split_transaction_engine_result_t;
static struct {
    bool attach_status_last_tx_valid, local_status_pending, peer_session_stale;
    uint32_t attach_status_last_tx_ms;
    uint8_t attach_status_miss_streak;
} g_era_split_transport_scheduler;
static uint32_t now;
static unsigned sent, dirty, due;
static uint32_t timer_read32(void) { return now; }
static uint32_t timer_elapsed32(uint32_t start) { return now-start; }
static void era_split_scheduler_session_note_local_status_sent(void) { ++sent; }
static void era_split_transport_scheduler_mark_dirty(unsigned flags) { dirty |= flags; }
static void era_split_transport_scheduler_mark_route_due(unsigned flags) { due |= flags; }
#define ERA_SPLIT_SCHEDULER_DIRTY_PEER_STALE 1U
#define ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS 1U
'''

CASES = r'''
#define SHOULD_SEND() era_split_transport_scheduler_local_status_revalidation_due()
#define COMPLETE(result, transmitted, stale) era_split_transport_scheduler_note_attach_status_request_attempt(result, transmitted, stale)
int main(void) {
    /* First ten misses retain the 25ms bootstrap and continuity boundary. */
    g_era_split_transport_scheduler.local_status_pending = true;
    assert(SHOULD_SEND());
    for (unsigned n=1;n<=10;++n) {
        COMPLETE(TEST_TIMEOUT,true,false);
        uint32_t period=n<10 ? 25U : 100U;
        assert(era_split_transport_scheduler_bootstrap_period_ms()==period);
        assert(!SHOULD_SEND()); now+=period-1; assert(!SHOULD_SEND());
        now++; assert(SHOULD_SEND());
    }
    assert(sent==10);
    /* Completion, not request construction, owns the next interval. */
    now+=80; COMPLETE(TEST_TIMEOUT,true,false);
    now+=99; assert(!SHOULD_SEND()); now++; assert(SHOULD_SEND());
    /* Unsent failures still cannot create a tight retry loop. */
    unsigned before=sent;
    COMPLETE(TEST_TIMEOUT,false,false);
    assert(sent==before && !SHOULD_SEND());
    now+=100; assert(SHOULD_SEND());
    /* Success removes pending bootstrap; an explicit reset reopens it. */
    COMPLETE(ERA_SPLIT_TRANSACTION_RESULT_OK,true,false);
    now+=10000; assert(!SHOULD_SEND());
    era_split_transport_scheduler_reset_session_probe_backoff();
    assert(!SHOULD_SEND());
    g_era_split_transport_scheduler.local_status_pending=true;
    assert(SHOULD_SEND());
    /* Counter saturation and raw timer wrap cannot accelerate discovery. */
    g_era_split_transport_scheduler.attach_status_miss_streak=UINT8_MAX;
    now=UINT32_MAX-50; COMPLETE(TEST_TIMEOUT,true,false);
    assert(g_era_split_transport_scheduler.attach_status_miss_streak==UINT8_MAX);
    now+=99; assert(!SHOULD_SEND()); now++; assert(SHOULD_SEND());
    /* Known-peer failure retains the existing immediate stale revalidation. */
    COMPLETE(TEST_TIMEOUT,true,true);
    assert(g_era_split_transport_scheduler.peer_session_stale && dirty && due);
    assert(SHOULD_SEND());
    /* Long-lived NO LINK: at most ten new requests in each second after the
       initial burst, even with a 1ms task. Supplied transaction durations are
       model inputs, NOT CPU busy time. Only one transaction is in flight. */
    for (unsigned duration=2;duration<=8;duration*=2) {
        memset(&g_era_split_transport_scheduler,0,sizeof(g_era_split_transport_scheduler));
        g_era_split_transport_scheduler.local_status_pending=true;
        g_era_split_transport_scheduler.attach_status_miss_streak=10;
        unsigned requests=0, in_second=0;
        uint32_t finish=0; bool busy=false;
        for (now=0;now<10000;++now) {
            if (now && now%1000==0) { assert(in_second<=10); in_second=0; }
            if (busy && now==finish) { COMPLETE(TEST_TIMEOUT,true,false); busy=false; }
            if (!busy && SHOULD_SEND()) {
                finish=now+duration; busy=true; ++requests; ++in_second;
            }
        }
        assert(in_second<=10 && requests<=100 && requests>=90);
        printf("duration=%ums: %u probes/10s (model, not CPU utilization)\n",duration,requests);
    }
    puts("PASS: production sender bootstrap, completion, failure, success, wrap, stale and rate limit");
    return 0;
}
'''


class DiscoveryCadence(unittest.TestCase):
    def test_production_discovery_cadence(self):
        with tempfile.TemporaryDirectory(prefix="era-discovery-") as directory:
            root = Path(directory)
            unit, executable = root / "cadence.c", root / "cadence"
            unit.write_text(production_unit(), encoding="utf-8")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(unit), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
