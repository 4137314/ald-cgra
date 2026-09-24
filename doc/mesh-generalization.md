# Generalizing the mesh

The complete accelerator now supports an **R x C mesh of 16-bit PEs with
1 <= R*C <= 16**. Dimensions are selected when the RTL is elaborated and
reported through `ID`; the same host binary discovers and uses them at runtime.
The default remains 4x4. Reconfiguration changes operations and routing, not
the physical number of PEs.

## Implemented

| Layer | Geometry contract |
|---|---|
| Array, controller, top | Positive `G_ROWS` / `G_COLS`, dimension-derived ports and storage |
| Controller indexing | Shared index covers `max(R*C, R+C)`; read mux guards indices beyond the PE array |
| Protocol v3 | CFG has R*C words, WR has R+C words; EXEC keeps its two-byte mask |
| Host library | Cached, validated identity; explicit buffer counts in `_n` transfers |
| Emulator | `sim:RxC`, optionally `:v2` or `:flaky`; default `sim:` remains 4x4 |
| Vector mapping | `min(R,C)` diagonal lanes, taps indexed by `r*C+c` |
| Matrix-vector / convolution | R x C tiles, including incomplete tiles in both dimensions |
| Prefix scan | C elements per block, with carry between blocks |
| DSL | Coordinates and output taps validated against actual device dimensions |
| CLI | `run`, `matvec`, `scan`, `conv`, `probe`, `dump` and device-specific `show` adapt |
| Vivado flows | `MESH_ROWS` / `MESH_COLS` forwarded by synth, bit, sta and fmax |

The wire format remains v3-compatible: bits for nonexistent PEs are ignored
by the controller; the host API rejects them before sending a request. The
response contains only selected existing registers, in ascending PE order,
followed by checksum and status. Mask corruption can still alter reply length;
the additive checksum is not a substitute for a framed transport.

## Try it without a board

```sh
make
./sw/build/cgra probe -d sim:2x3
./sw/build/cgra run add -d sim:2x3 --a "1 2 3 4 5" --b "10 20 30 40 50"
./sw/build/cgra show add -d sim:3x2
./sw/build/cgra dump -d sim:1x16
./sw/build/cgra scan -d sim:2x3:v2 --a "3 1 4 1 5 9"
make test
make PROFILE=asan test
make sim
```

`show` without an explicit device and the offline `check` command compile for
4x4. Use `show MODE -d DEVICE` to validate a mode against another geometry.
The public `cgra_emulate_pty` server retains its default 4x4 shape; the RTL test
harness creates its own rectangular PTY devices.

## C API and compatibility

`cgra_get_info` caches the identity and validates protocol v2/v3, 16-bit data,
and the supported geometry. It performs the first ID handshake if needed.

- `cgra_configure_n(dev, cfg, count)`: count must equal R*C.
- `cgra_write_inputs_n(dev, west, R, north, C)`: both input arrays need exact counts.
- `cgra_read_regs_n(dev, regs, capacity)`: capacity must be at least R*C.
- `cgra_exec_n`: exact input counts, valid PE mask and flags, and exactly
  `popcount(mask)` output slots; NULL inputs with count zero inject zeros.

Counts are in words. Size errors return `CGRA_ERR_ARG` without issuing the
transfer; an initial ID may still occur. The original fixed-size configure,
write, read, apply and exec entry points require 4x4 and return
`CGRA_ERR_GEOMETRY` otherwise. Existing callers retain their buffer contract;
vector and dot-product functions automatically adapt. Matrix-vector,
scan and convolution are also public installed functions (`cgra_matvec`,
`cgra_scan`, `cgra_conv`) with `size_t` dimensions and output capacities.
The CLI calls those same engines. All public kernels return a status code,
not a result length; see `libcgra(3)` for buffer and failure contracts.

## Verification and evidence

`make test` checks API bounds, mask validation, buffer canaries, cached ID and
legacy wrappers. Kernel results are compared with independent scalar arithmetic
on **1x1, 1x16, 16x1, 2x3, 3x2, 2x8, 8x2 and 4x4**, on both v2 and v3.
The shared workload includes signed values, 16-bit overflow, 19-element vectors,
a 5x7 matrix, four scan operators and convolution.

`make sim` adds three complementary checks:

1. **504 raw protocol transactions per geometry** against controller + array,
   with corrupt checksums, partial requests and varying transmit backpressure.
   The 4x4 cadence sweep uses STEP_DIV 1 through 4.
2. **Actual host kernel transcripts** for all eight geometries. The generator
   executes libcgra and the DSL engines through a POSIX PTY, verifies their
   outputs against the scalar references, and records requests and model
   replies. The bench replays those exact bytes against the RTL. A failed
   generator cannot replace a valid vector file.
3. **Full UART top-level replay** on 2x3 (233 transactions) and 3x2 (259),
   including the real UART receiver/transmitter and reset synchronizer.

Independent standalone array wiring tests also cover seven shapes through
8x8. The 8x8 case proves array connectivity only: the current controller and
protocol intentionally reject more than 16 PEs. Simulations do not establish
FPGA resource use, achievable frequency or physical serial-link performance.

## Synthesis and programming

```sh
make synth MESH_ROWS=2 MESH_COLS=3
make sta MESH_ROWS=2 MESH_COLS=3
make bit MESH_ROWS=2 MESH_COLS=3
make prog-ofl MESH_ROWS=2 MESH_COLS=3
```

The same dimensions must be supplied when selecting the bitstream to program.
Nondefault geometries use `hw/build/vivado/RxC/`, including their timing and
Fmax reports, so they do not overwrite 4x4 artifacts. Defaults keep the original
`hw/build/vivado/` paths. Tcl validates dimensions before synthesis. These
flows require Vivado; new area/timing figures remain to be measured.

## What the geometry changes

Configuration plus acknowledgement costs `4*R*C + 3` UART bytes. A standalone
input write costs `2*(R+C) + 3`. Under v3, EXEC returning K registers costs
`8 + 2*(R+C) + 2*K` bytes, including request and response. These are framing
counts, not timing measurements.

An N x N diagonal mapping has N useful lanes and N*N physical PEs. Larger
arrays amortize headers and round trips while configuration cost grows
quadratically. Rectangular matrix-vector tiles can reduce padding for some
workloads; a narrow rectangle reduces diagonal parallelism. Compare wire bytes,
round trips, resources and timing for the same workload before choosing a shape.

The next architectural extension is **more than 16 PEs**. It needs a versioned
protocol with a variable-size mask or tap list, bounded lengths and stronger
framing/recovery, plus revised host limits. Keep v2/v3 compatibility covered.
Datapath width changes and toroidal/arbitrary interconnects are separate
experiments: they affect immediate packing, arithmetic, boundary semantics
and routing algorithms.
