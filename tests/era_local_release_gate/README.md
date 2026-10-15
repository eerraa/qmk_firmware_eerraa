# ERA local release gate host fixture

Run from the root-owned Linux test snapshot synchronized by `era_build.py`:

```sh
make test:era_local_release_gate
python3 tests/era_local_release_gate/test_negative_mutations.py
```

The 18 positive cases exercise actual `keyboard_task()` / `matrix_task()`, the
RP2040 authority reducer, ChibiOS wake clear, QMK action/tapping/TD/report owners,
the HID interval owner, and the HP action-pass/origin binding. The keyboard and
shared-clock objects compile with `SPLIT_KEYBOARD`, so the real closed-authority
`quantum_task()` guard also runs. Assertions live in
`test_local_release.cpp`; the production caller connections live in `boundary.c`.

USB/SOF/GPIO facts, peer receiver/engine projection and report sink/completion are
simulated. Split hardware initialization is a no-op; the production sync timer
uses the host clock and its zero initial offset, without wire anchor updates. This target does not execute the wire receiver or ChibiOS USB queue
admission. Physical USB timing, device completion and scan performance require
separate evidence.

The negative runner rebuilds actual QMK with three temporary source mutations and
requires gtest runtime failures. It also checks isolated HP closure and the
restored full positive suite. Run it under the shared test lock with no concurrent
build or source editor: it restores exact `keyboard.c` bytes in `finally` and ends
with a build of the original production source.
