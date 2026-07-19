# CGRA

A Coarse-Grained Reconfigurable Array on FPGA, used as a runtime-reconfigurable
accelerator driven by a host PC over UART.

Project for the *Advanced Logic Design* course, University of Trento.

## Overview

```
 host PC                                        FPGA
┌───────────────────────────────┐   UART    ┌──────────────────────────────┐
│  cgra CLI (sw/)                │ 115200 8N1│  uart_rx/tx ─ cgra_ctrl      │
│    │  .cgra DSL + config       ├───────────┤       │            │         │
│    └─ libcgra.a (lib/)         │  or sim:  │       └── 4x4 PE array ──────│
│         serial │ emulator      │           └──────────────────────────────┘
└───────────────────────────────┘
```

The fabric is a 4×4 mesh of 16-bit processing elements (PEs). Each PE has
two operand muxes (N/S/E/W neighbour, immediate, own register, zero), an
ALU (add/sub/mul/mac/logic/shift/min/max/abs/…) and one output register.
The host *reconfigures* the array by loading one 32-bit configuration word
per PE, injects operands into the west (per-row) and north (per-column)
edge registers, clocks the array a chosen number of steps and reads back
all 16 PE registers.

Everything above the wire is UNIX-style: a `cgra` command-line tool drives the
array through named **modes** and **pipelines** written in a small custom
configuration language (`.cgra`), discovered from `~/.config/cgra/` and
project-local files. An in-process **emulator** (`device sim:`) speaks the exact
same protocol, so the whole stack runs and is tested without an FPGA.

## Repository layout

| Path | Content |
|------|---------|
| `hw/rtl/` | Synthesisable VHDL (package, UART, PE, array, controller, top) |
| `hw/sim/` | GHDL testbenches (`tb_pe`, `tb_cgra_top`, `tb_matvec`) |
| `hw/con/` | Constraint files (`basys3.xdc`, `nexys_a7.xdc`) |
| `hw/scr/` | Vivado batch scripts (`build.tcl`, `program.tcl`) |
| `lib/` | Host C library `libcgra.a` (serial transport, kernels, `sim:` emulator) |
| `sw/` | `cgra` CLI: DSL parser, config discovery, mode compiler, vector I/O |
| `sw/config/` | Reference `.cgra` config files |
| `doc/` | LaTeX (IEEE) hardware report — modular `config/`, `src/`, `figures/` |

Each component has a standalone `*.mk` fragment; the root `Makefile` just
delegates (`make -C sw -f sw.mk …`). A `flake.nix` provides a dev shell with
the whole toolchain (`nix develop`) and a package build (`nix build`).

## Quickstart

```sh
make            # build libcgra.a and the cgra CLI
make test       # run the CLI + kernels against the sim: emulator (no FPGA)
make sim        # GHDL testbenches (tb_pe, tb_cgra_top)
```

Drive the array — the emulator needs no hardware, so try it first:

```sh
cgra=sw/build/cgra

$cgra selftest                                   # kernels vs. a host reference
$cgra run add  -d sim --a "1 2 3 4" --b "5 6 7 8"    # -> 6 8 10 12
echo "-3 -1 0 5" | $cgra run relu -d sim              # -> 0 0 0 5
$cgra run muli -d sim --a "1 2 3 4" --imm 10 --io hex # -> 0x000A ...
$cgra pipe saxpy -d sim --a "1 2 3 4"                 # a*3+7 -> 10 13 16 19
$cgra run dot -d sim --a "1 2 3" --b "4 5 6"          # dot product -> 32
$cgra matvec -d sim -m "1 2 3 4  5 6 7 8  9 10 11 12  13 14 15 16" --x "1 1 1 1"
                                                      # systolic A*x -> 10 26 42 58
```

On real hardware, drop `-d sim` (uses the `default` device profile) or point at
a port: `cgra run add -d /dev/ttyUSB1 --a … --b …`.

Bitstream (Vivado batch, no GUI; on a machine with Vivado):

```sh
make bit BOARD=basys3      # or BOARD=nexys_a7
make prog                  # program via Vivado hw_server
make prog-ofl              # alternative: openFPGALoader, no Vivado needed
```

## The `cgra` CLI

```
cgra [-d DEVICE] [-c FILE] <command> [args]

  modes | devices | pipelines | io | config   list configuration
  check                                        validate the loaded configuration
  init [--force]                               install defaults to ~/.config/cgra
  ping | reset | dump              [-d DEVICE] talk to the device
  probe                            [-d DEVICE] bring-up: identify + known-answer tests
  show MODE [--imm N] [-v]                     print compiled config words (-v decodes them)
  run  MODE [nums...] [--a SRC] [--b SRC] [--imm N] [--steps N] [--io P]
  pipe NAME [nums...] [--a SRC] [--b SRC] [--io P]
  matvec -m SRC --x SRC [--cols N] [--io P]    systolic y = A * x
  conv -m KERNEL --x SIGNAL [--io P]           1-D convolution
  scan [nums...] [--a SRC] [--op sum|max|min|prod]   inclusive prefix scan
  bench [MODE] [--size N] [--repeat R]         time a mode + report link stats
  emulate                                      serve a virtual CGRA on a pty
  selftest                                     run kernels on the sim: emulator
```

Input vectors (`--a`/`--b`, or positional numbers, or stdin) accept an inline
list `"1 2 3"`, a file `@path`, or `-` for stdin — so the tool composes in
pipes: `seq 0 255 | cgra run relu -d sim`.

Output formatting: `--io PROFILE` (dec/hex/bin/json), `--json`, `--dtype u16|s16`,
`-o/--out FILE`.

Device resolution precedence: `-d` flag → `$CGRA_DEVICE` → the `default`
profile. `-d` also accepts a raw path or `sim:`. `$CGRA_PORT` / `$CGRA_BAUD`
override the resolved profile.

**Develop without an FPGA.** `cgra emulate` serves the exact same protocol on a
pseudo-terminal; point any serial tool — or another cgra process — at the path
it prints:

```sh
cgra emulate &                       # prints e.g. "serving on /dev/pts/7"
cgra -d /dev/pts/7 probe             # identify + run every kernel, checked
```

`cgra probe` with a device is the FPGA bring-up check (identify + known-answer
tests). With **no** device it *autodiscovers*: it scans `/dev/ttyUSB*` and
`/dev/ttyACM*`, pings each, reports any CGRA found and records the first one so
later commands can use `-d auto`:

```sh
cgra probe                    # -> "found CGRA v2 on /dev/ttyUSB0 ..."
cgra -d auto run add --a … --b …
```

Structured output composes with `jq`: `--json` prints
`{"count": N, "result": [...]}`, so `cgra run … --json | jq '.result'` works.
Bash completion lives in `sw/completions/cgra.bash`.

**Robustness.** The library retransmits CFG/WR/RD automatically on a NACK,
timeout or bad checksum (`cgra_set_retries`, default 3). RUN is never retried
because it advances state; instead a failed RUN triggers a resync (flush + reset)
so the device is never left hanging. `cgra_get_stats` exposes
transaction/retry/byte counters — `cgra bench` prints them. The `sim:flaky`
device deterministically NACKs the first CFG and WR to exercise the retry path.

## The `.cgra` configuration language

Custom, INI-flavoured, one entity per named section. Files merge **by name**
across the discovery chain, so any later file overrides individual fields of an
entity defined earlier.

```ini
[device lab]              # a connection profile
port    = /dev/ttyUSB1
baud    = 115200
timeout = 2000            # ms
board   = basys3

[mode relu]               # a reconfiguration ("mode")
doc     = out = max(a, 0)
pattern = diagonal        # diagonal | reduce | custom
op      = max             # any PE opcode
a       = north           # operand source: north|west|self|zero|const(K)
b       = const(0)
steps   = rows            # rows | auto | <int>

[mode my_kernel]          # hand-placed PEs (pattern = custom)
pattern = custom
out     = diag            # diag | pe R,C
reset   = each            # each | once — clear PE regs between chunks
pe 0,0  = op=mac a=north b=west
pe 1,1  = op=add a=self  b=const(1)

[pipeline saxpy]          # host chains modes, feeding output -> input
doc     = out = a * 3 + 7
stage   = muli imm=3
stage   = addi imm=7

[io hex]                  # data binding profile
format  = hex             # dec | hex | bin | json
width   = 16
sep     = ws              # ws | comma | newline
```

A file may pull in another with `include other.cgra` (path relative to the
including file), so you can split devices, modes and pipelines across files.
`set NAME = value` defines a variable usable as `$NAME` / `${NAME}` in any later
value, e.g. `set GAIN = 7` then `b = const($GAIN)`.

**Patterns** (how a mode maps onto the array):
* `diagonal` — element-wise; `a[]` enters from the north, `b[]` (or a
  `const`) from the west, off-diagonal PEs are PASS chains, the four diagonal
  PEs each compute one element. 4 lanes per transaction, `out = diag`.
* `reduce` — MAC/ACC accumulation into PE(0,0); one element per step, scalar
  result (e.g. `dot`).
* `custom` — you place each PE with `pe R,C = op=.. a=.. b=..`; unlisted PEs
  are NOP. Output taps default to the diagonal (`out = diag`) or a single PE.
* `scan` — inclusive prefix sum on row 0: the element sits in the immediate and
  is re-added each step while the running prefix flows west→east; tiled over
  columns with the carry chained. Driven by `cgra scan`.
* `conv` — 1-D convolution as a Toeplitz matrix–vector, driven by `cgra conv`.
* `systolic` — weight-stationary matrix–vector `y = A·x`, driven by
  `cgra matvec` (matrix-shaped input, so it is not a plain vector `run`).
  The vector `x` is held stationary in the PE immediates; the matrix streams
  in one row per step as a wavefront that marches south (row 0 multiplies,
  rows 1–3 form a delay line), and the array is tiled over 4×4 blocks for any
  `M×N`. All 16 PEs are used for the products and data movement; the host adds
  the four skewed partials per output. See `matvec_run` in `sw/compile.c`.

Why weight-stationary and not a full TPU-style array: each PE here has a
*single* output register and external data enters only on the north (row 0)
and west (column 0) edges, so a PE cannot both forward an activation and carry
a partial sum. That rules out fused spatial accumulation, and makes the
weight-stationary streaming dataflow above the natural systolic fit.

**Config discovery** (low → high precedence):
built-in defaults → `/etc/cgra` → `$XDG_CONFIG_HOME/cgra` (or `~/.config/cgra`)
→ `$CGRA_PATH` dirs → project-local `$CGRA_CONFIG` or `./.cgra/` → `-c FILE`.
Within each directory, `config.cgra` loads first, then the other top-level
`*.cgra` alphabetically, then a `conf.d/` drop-in dir alphabetically — so
`99_overrides.cgra` in `conf.d/` overrides `01_base.cgra` by name (the
Nginx/systemd pattern). `CGRA_PATH` is a colon-separated list of extra library
roots (like `PATH`). `cgra config` prints the resolved paths.

**Standard library.** `cgra init` writes the defaults to your user config dir
*and* installs a small stdlib (`arithmetic`, `linalg`, `dsp`, `crypto`) to
`~/.config/cgra/stdlib/`. Pull pieces in with `include "linalg.cgra"`; the
`include` search order is: the including file's dir, each `$CGRA_PATH` entry,
`~/.config/cgra` and its `stdlib/`, then the system share dir.

## Documentation

Everything is documented in-place and buildable with `make docs`:

| What | Format | Build | Output |
|------|--------|-------|--------|
| CLI reference | man(1) + GNU info | `make -C sw -f sw.mk doc` | `sw/build/cgra.info`, `sw/doc/cgra.1` |
| `.cgra` file format | man(5) | `make -C sw -f sw.mk man` | `sw/doc/cgra.5` |
| Library API | man(3) | `make -C lib -f lib.mk man` | `lib/doc/libcgra.3` |
| Hardware report | LaTeX (IEEE) | `make doc` | `doc/build/main.pdf` |

Documentation is hand-written and Unix-style — **no Doxygen**. The C API carries
plain block comments in `lib/include/cgra.h` and a full `libcgra(3)` man page;
the VHDL is commented per module; the `.cgra` language has its own `cgra(5)`
page; and every source subdirectory carries a plain-text `README`. The `doc/`
report is an IEEE-style write-up of the *logical* design — PE datapath, mesh,
control FSM, dataflow mappings and a wire-level cost model as diagrams, no code
listings. It is modular (`doc/config/*.tex`, `doc/src/*.tex`,
`doc/figures/*.tex`) and its PDF opens with a hyperlinked bookmark outline;
`IEEEtran.cls` is vendored so it builds without a full TeX Live.

Install the pages system-wide with, e.g., `install -Dm644 sw/doc/cgra.1
/usr/local/share/man/man1/cgra.1` (and `cgra.5` → `man5/`, `libcgra.3` →
`man3/`).

## UART protocol (v2)

115200 baud, 8N1, little-endian payloads. Single-byte commands, `ACK = 0x79`,
`NACK = 0x1F`. Multi-byte payloads carry an 8-bit additive checksum (sum mod
256); the device NACKs a bad CFG/WR and appends a checksum to the RD reply.
Defined in `hw/rtl/cgra_pkg.vhd`, mirrored in `lib/include/cgra.h` and the
emulator `lib/src/emu.c` — **keep the three in sync**.

| Cmd | Byte | Payload → reply |
|-----|------|-----------------|
| `ID`  | `0x01` | — → `0xCA`, version, ROWS, COLS, DATA_W |
| `CFG` | `0x02` | 16 × 32-bit config words + checksum → ACK / NACK |
| `WR`  | `0x03` | 8 × 16-bit (west 0–3, north 0–3) + checksum → ACK / NACK |
| `RUN` | `0x04` | 1 byte step count N (array clocked N cycles) → ACK |
| `RD`  | `0x05` | — → 16 × 16-bit PE registers + checksum |
| `RST` | `0x06` | — (clears PE registers, keeps config) → ACK |

The `ID` reply reports the array geometry, so the host adapts at runtime
(`cgra ping` / `cgra probe`). A watchdog in `cgra_ctrl` aborts partially
received commands after ~1 s so a desynchronised host can always recover.

## PE configuration word

```
[31:26] reserved  [25:23] sel_b  [22:20] sel_a  [19:16] opcode  [15:0] imm
```

Opcodes: `NOP PASS ADD SUB MUL MAC AND OR XOR SHL SHR MAX MIN ABS ACC CONST`.
Operand selects: `N S E W CONST SELF ZERO`; on the edges, N of row 0 reads the
north input port of that column and W of column 0 reads the west input port of
that row (S/E edges read zero).

## Notes

* Datapath is 16-bit signed with wrap-around; `MUL`/`MAC` keep the low 16 bits.
* Array geometry and data width are constants in `cgra_pkg.vhd`; the C header
  assumes 4×4×16-bit — change both sides together.
* The `sim:` emulator (`lib/src/emu.c`) is the golden reference for the RTL:
  it re-implements the PE ALU and the protocol FSM, so `make test` validates
  the host stack against the same semantics the hardware must have.
* Timing: everything runs in the 100 MHz clock domain; UART I/O and buttons are
  false-pathed in the XDC.
