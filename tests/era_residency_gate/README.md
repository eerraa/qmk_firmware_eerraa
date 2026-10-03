# SRAM allocation gate regression

Run in the configured WSL ARM toolchain:

```sh
python3 tests/era_residency_gate/test_residency_gate.py
```

The fixtures call the production section parser and capacity checker. Boundary
cases include every SRAM bank and the ROM scratch exclusion, overlap, heap
filler, malformed tables, linked stack reservations and linked flash/NVM bounds. The complete shell gate is also run on temporary ARM ELFs assembled
and linked by the real toolchain, proving that a small growth margin is accepted
while physical overflow, linked allocators, flash IRQ vectors and invalid ELFs
are rejected. Those ELF tests explicitly skip when the ARM toolchain is absent;
such a run is not gate validation.

These checks do not prove stack high-water marks or transitive startup closure.
The linked stack reservations and call-depth review remain separate checks.
