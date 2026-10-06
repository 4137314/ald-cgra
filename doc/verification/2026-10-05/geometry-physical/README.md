# Matched geometry sweep — 5 October 2026

Real Vivado 2026.1, Nexys A7 xc7a100tcsg324-1, STEP_DIV=2, 10 ns, no floorplan.
All five STA executions passed; the runner checks identical source hashes and
tool identity across points. Each point runs once and retains its own inputs,
command, log, reports, metrics and manifest. No bitstream or board programming.

| Shape | Synth LUT | Routed LUT | FF | DSP | WNS (ns) | WHS (ns) |
|---|---:|---:|---:|---:|---:|---:|
| 1x16 | 5017 | 4999 | 1179 | 16 | +2.433 | +0.081 |
| 2x8 | 5003 | 4974 | 1066 | 16 | +0.775 | +0.105 |
| 4x4 | 4999 | 4971 | 1034 | 16 | +1.862 | +0.098 |
| 8x2 | 5002 | 4975 | 1066 | 16 | +2.350 | +0.130 |
| 16x1 | 5010 | 4993 | 1179 | 16 | +1.247 | +0.098 |

All points report zero BRAM tiles. Same PE count does not imply equal register
or LUT use: edge storage/controller geometry and routing differ. Positive slack
is closure at the 10 ns constraint, not a measured Fmax or a board speedup.
The five workload wire measurements remain in the separate software benchmark
evidence; host emulator timings must not be converted into FPGA throughput.

Run: `python3 scripts/measure-physical.py --out NEW_DIRECTORY --vivado /opt/Xilinx/2026.1/Vivado/bin/vivado`.
The exact executed runner is copied here. Summary paths are relative to this
directory; original manifests are unchanged. DCP binaries remain in the
ignored original build directory with hashes in the manifests. The archive
omits them; regenerate or copy the complete original run for checkpoint reuse.
