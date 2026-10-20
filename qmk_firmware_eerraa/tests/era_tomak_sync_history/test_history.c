// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include "../../keyboards/era/sirind/tomak/keymaps/sync_diag/history.h"
int main(void) {
    tomak_sync_history_t h = {0};
    unsigned values[TOMAK_SYNC_HISTORY_CAPACITY] = {0};
    for (unsigned n = 1; n <= 10000; ++n) {
        values[tomak_sync_history_push(&h)] = n;
        unsigned kept = n < TOMAK_SYNC_HISTORY_CAPACITY ? n : TOMAK_SYNC_HISTORY_CAPACITY;
        assert(h.count == kept);
        assert(h.overwritten == n - kept);
        for (unsigned i = 0; i < kept; ++i) {
            assert(values[tomak_sync_history_index(&h, i)] == n - kept + 1 + i);
        }
    }
}
