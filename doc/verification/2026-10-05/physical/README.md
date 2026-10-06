# Integrated physical gate — 5 October 2026

`bit-20261005T133942Z-0bc27739/manifest.json` is the completed manifest from
actual Vivado 2026.1 (SW build 6511674). Board Nexys A7 xc7a100tcsg324-1,
4x4, STEP_DIV=2, 10 ns, default directives, no floorplan. Command:

```
make -C hw -f hw.mk bit VIVADO=/opt/Xilinx/2026.1/Vivado/bin/vivado
```

Exit 0, duration 459.83 seconds. All nine gates PASS, bitstream generated;
no hardware programmed. Setup WNS +1.862 ns, hold WHS +0.098 ns. Default
synthesis 4999 LUT / 1034 FF / 16 DSP / 0 BRAM tiles; implemented 4971 LUT /
1034 FF / 16 DSP / 0 BRAM tiles. Counts belong to this exact run, not the
separate historical synth gate with hierarchy none.

The new constraints inspect 800 expected launch registers and 256 PE data
endpoints with setup requirement 20 ns and hold requirement 0 ns. All 512 PE
CE/reset endpoints remain at 10 ns. The report retains every endpoint. Both
two-stage chains preserve ASYNC_REG, one load on the first stage, a common
clock, and one slice per pair. CDC reports two informational recognized chains.

`make check-artifact ARTIFACT=.../manifest.json` was actually run against the
original complete run and passed (`../physical-artifact-validation/`). The
archive retains reports, exact input snapshots, tool log and original hashes;
large checkpoints and the bitstream remain in the ignored original run under
hw/build/vivado/runs/. Their hashes remain in the manifest. This review
archive omits those binaries and is not itself a programmable artifact.
Regenerate with the recorded command/tool or transfer the whole original run
including those hashed files. The manifest is not a signed attestation.

`../physical-ghdl/` records the full GHDL suite PASS. `../physical-scripts/`
records 80 synthetic Tcl checks and 31 artifact regressions PASS (not timing
proofs), including missing reports, failing gates, target mismatches, changed
sources/bitstreams, source path escape and no Git executable. Rejected artifacts
are tested to fail before either programming tool can be launched.

`interrupted-september-run/` preserves the earlier incomplete runner manifest
unchanged. Its tool log completed bit generation, but there is no terminal
runner status or published successful manifest. It is not accepted as PASS.
