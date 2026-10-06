# Physical flow and artifact contract

Run from the repository root (or use `make -C hw -f hw.mk`):

```sh
make synth VIVADO=/opt/Xilinx/2026.1/Vivado/bin/vivado
make bit VIVADO=/opt/Xilinx/2026.1/Vivado/bin/vivado
make check-artifact ARTIFACT=/absolute/path/to/bit-run/manifest.json
```

`check-artifact` has no hardware side effects. `prog` and `prog-ofl` require
that same explicit `ARTIFACT`, board, geometry and STEP_DIV. They are separate
user actions. This consolidation does not program hardware.

## One run, one source snapshot

`hw/scr/artifacts.py` creates a new `hw/build/vivado/runs/MODE-UTC-ID/` directory.
It copies `rtl/`, `con/` and `scr/`, then runs Vivado from that snapshot. Direct
Tcl entry points remain useful for investigation, but only the Make/Python
runner publishes a manifest. Python 3.11 or later is required.

Manifest schema 1 records command/cwd, commit and dirty status when Git is
available, source SHA-256 hashes, board/part, all top-level generics, constrained
period, floorplan/strategy, requested and resolved executable/version, elapsed
time, exit status, gates, resource reports, setup/hold slack and artifact
hashes. `requested_period_ns` retains the caller's constraint; `period_ns`
records the actual applied value (for Fmax, the final validated candidate).
Periods are explicitly quantized to a 1 ps grid before `create_clock`, then
checked against Vivado's clock object. The endpoint checks use that same
period, without relaxing the nonnegative setup/hold criteria. This avoids
comparing fractional-picosecond search requests with rounded tool requirements.
Synth and implemented resource counts have distinct report keys.
`hardware_test: NOT_RUN` is deliberate. The generated bitstream has its own
hash; the C benchmark's optional bitstream ID is separate caller metadata.

Completed successful runs atomically update `current/BOARD-RxC-divN-MODE.json`,
a small relative manifest pointer with its hash. Each pointer means the last
valid run for that key; read the manifest for its period and strategy. Failed,
timed-out or interrupted runs cannot replace it. Logs/reports of failures stay
in their own directory. An initial/incomplete manifest is never accepted for
programming, even if a `.bit` exists. No stale file is copied into a new run.

Programming validation checks the manifest type/status, all required gates,
matching target/geometry/STEP_DIV, finite nonnegative routed slack, intact
source/artifact hashes and containment of paths inside the run directory.
Both supported boards use 100 MHz; a weaker constraint (`PERIOD > 10 ns`)
cannot qualify a programmable artifact. A smaller constraint does not change
the physical frequency either. The Vivado helper rejects zero or multiple
matching JTAG devices. JTAG identifies the die, not the board/package/speed
grade; the explicit board selection remains a caller obligation. The manifest
provides integrity/provenance for trusted local artifacts, not a signed supply
chain or proof against malicious modification of both data and hashes.

## Physical gates

`synth` retains a synthesis checkpoint and reports. It requires no errors or
critical warnings and runs the structural checks below. It does **not** claim
routed setup/hold closure. `sta` implements and gates timing without writing a
bitstream. `bit` performs the same checks before bit generation and checks tool
errors/critical warnings again afterward. `fmax` retains the search and gates
the final, independently implemented candidate; it is a heuristic search,
not a mathematical maximum or a board clock measurement.

- **Register coverage:** enumerate all PE result bits, CFG bits 0..25 and west/
  north input bits for the chosen dimensions. CFG bits 26..31 are reserved and
  have no RTL consumers. Unexpected/missing register names fail, even if the
  total count happens to match.
- **Data timing:** every expected PE D endpoint has a real setup/hold path from
  the selected launch registers with requirements `period * STEP_DIV` and 0.
  Vivado uses the launching registers' C pins as `-from`; Q is not a valid
  startpoint for this command. Cell-wide `-to` would also include control pins.
- **Control timing:** check each PE CE/reset endpoint has a real setup path
  with one-period requirement. It must not inherit the datapath exception.
- **Synchronizers:** ASYNC_REG preserved on both stages of RX and reset,
  first-stage Q drives only second-stage D, common constrained clock, nonempty
  loads; after placement each pair is in the same slice. This is a concrete
  check for these compatible-control two-register chains, not an analog proof.
- **CDC:** retain the full report; require the two recognized CDC-3 information
  entries and reject CDC warning/critical/error entries. False paths alone do
  not satisfy this check.
- **Constraints/DRC:** retain exception coverage and verbose timing checks;
  reject missing/constant/multiple clocks, unconstrained internal endpoints,
  loops and partial I/O delays. The asynchronous input first-stage D paths and
  UART TX/LED ports have explicit exceptions; no additional unspecified I/O
  delay is allowed. DRC errors/critical warnings fail.

The controller's formally proved reset/first-capture, capture-spacing and
CFG/WR-to-capture guards support the multicycle premise for the documented
instances. They do not certify the XDC, analog CDC or place-and-route. Refer
to the [formal contract](../hw/formal/README.md) for assumptions and limits.

The real Vivado 2026.1 reports are evidence for tool behavior; script mocks
only test flow decisions and publication failures. Primary references:
[AMD multicycle syntax](https://docs.amd.com/r/en-US/ug835-vivado-tcl-commands/set_multicycle_path),
[AMD ASYNC_REG](https://docs.amd.com/r/2022.2-English/ug912-vivado-properties/ASYNC_REG).
