# Real Fmax search and period precision regression

Both runs used Vivado 2026.1 build 6511674, Nexys A7 xc7a100tcsg324-1,
4x4, STEP_DIV=2, no floorplan, default strategies. Each has immutable inputs,
commands, logs, reports and manifest. Initial search interval 5–10 ns, six
bisection points after validating the upper endpoint. Final implementation
is rerun independently with phys_opt, then full setup/hold/CDC/endpoint gates.

- `fmax-20261005T145125Z-24331b0b`: **FAIL**. The final endpoint check compared
  requested 5.859375 ns with Vivado's 5.859 ns requirement. No successful Fmax
  artifact/current pointer was published. The original failed log is retained.
- `fmax-20261006T084511Z-f920c389`: **PASS** after explicit 1 ps period
  quantization, clock-object verification and use of the applied period in
  gates and manifests. Final **5.859 ns**, **170.677590 MHz** (1000 / period),
  **WNS +0.177 ns, WHS +0.122 ns**, 4975 LUT, 1034 FF, 16 DSP, zero BRAM.
  All structural/CDC/constraints/DRC gates PASS. Actual duration 1217.166 s.

The search is heuristic, not a proof of the maximum possible frequency. The
100 MHz board oscillator was not changed and no board was programmed/tested.
There is no inferred CPU speedup or physical UART measurement.

The runner deliberately records both requested and actual periods. Negative
slack still fails; there is no added slack tolerance. The script regressions
are separately recorded in `../physical-period-scripts/` (83 Tcl, 31 artifact
checks). They are not substitutes for these real Vivado runs.

Large DCP binaries remain in the original ignored build directories, with
hashes in their manifests. This review archive omits them and scratch files;
it cannot itself be used as a complete checkpoint/programming artifact.
