# CGRA

A Coarse-Grained Reconfigurable Array on FPGA, used as a runtime-reconfigurable
accelerator driven by a host PC over UART.

Project for the *Advanced Logic Design* course, University of Trento.

The [correction tracker](TASKS.md) records open issues, completed fixes and
verification results from the [September 2026 audit](doc/audit-2026-09-19.md).
The audit is a historical snapshot; consult the tracker for current status.

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
| `hw/scr/` | Vivado batch scripts (`synth`/`fmax`/`build`/`timing`/`program`.tcl + sourced `constraints`/`floorplan`.tcl) |
| `hw/perf/` | Historical Fmax notes (`PERFLOG`), pending new measurements after the audit fixes |
| `lib/` | Host C library `libcgra` (static + shared; transport, kernels, `sim:` emulator, `test/`, `libcgra.3`, `cgra.pc.in`) |
| `sw/` | `cgra` CLI: DSL parser, config discovery, mode compiler, vector I/O, `test/` |
| `sw/config/` | Reference `.cgra` config files |
| `doc/` | LaTeX (IEEE) hardware report — modular `config/`, `src/`, `figures/` |

Each component has a standalone `*.mk` fragment; the root `Makefile` just
delegates (`make -C sw -f sw.mk …`). A `flake.nix` provides a dev shell with
the whole toolchain (`nix develop`) and a package build (`nix build`).

## Quickstart

```sh
make            # build libcgra.a (+ .so) and the cgra CLI, optimized (-O3)
make test       # unit tests (assert.h) + CLI smoke tests on the sim: emulator
make sim        # GHDL benches, C/RTL differential checks and mesh shape sweep
make check-deps # report which build/runtime deps are present
```

Everything is standard Unix and KISS: plain Makefiles, `pkg-config`, man pages,
and a `README` in every source subdirectory. Nix is offered as an optional
package-manager path, not a requirement.

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
make synth                 # fast synthesis-only gate (error/critical-warning)
make fmax                  # binary-search the max closing clock + name the wall
make bit                   # timing-gated: no .bit unless setup+hold met
make sta                   # static timing analysis gate on its own
make sta FLOORPLAN=1       # any of the above, with the PE-array floorplan
make prog                  # program via Vivado hw_server
make prog-ofl              # alternative: openFPGALoader, no Vivado needed
```

`BOARD` defaults to `nexys_a7` (Digilent **Nexys 4 DDR** / Nexys A7-100T,
xc7a100t); pass `BOARD=basys3` for the Basys 3. `make bit` requires nonempty
setup/hold paths and nonnegative slack before writing a new bitstream;
`make sta` runs the timing check without writing one. Results depend on the
applied constraints. The default target is 10 ns; `PERIOD=<ns>` changes the
constraint, not the board oscillator. A previous build's files may remain
after a failed run. New physical measurements are still needed after the RTL
and constraint corrections. `make fmax` reports `1000 / period` only after a
successful final run and records it in `fmax_validated_history.csv`.

## Install

Standard `make install` with a configured `PREFIX` and optional `DESTDIR` staging:

```sh
sudo make install                      # -> /usr/local (optimized -O3 build)
make install PREFIX=~/.local           # user install
make install DESTDIR=/tmp/stage PREFIX=/usr   # staged (packaging)
make uninstall                         # same PREFIX/DESTDIR
```

It installs the CLI, the static **and** shared library (with soname symlinks),
the `cgra.h` header, a **pkg-config** file, the man pages (`cgra.1`, `cgra.5`,
`libcgra.3`), the GNU info manual, the bash completion, and the `.cgra` stdlib.
The CLI records `PREFIX/share/cgra/stdlib` at build time; `pkgdatadir` can
override the parent directory. Changing the prefix rebuilds the affected
objects without a clean. `DESTDIR` is excluded from runtime paths: deploy
the staged files to the configured prefix. Moving an installed tree to a
different prefix requires rebuilding, or setting `CGRA_PATH` for includes
and `CGRA_STDLIB` for `init`.

`libdir` and `includedir` overrides are reflected in the installed pkg-config
file, including when `DESTDIR` is used. For example, `make install PREFIX=/usr
libdir=/usr/lib64 includedir=/usr/include/cgra DESTDIR=/tmp/stage` stages those
directories while keeping `/tmp/stage` out of the runtime metadata.

Downstream builds then use pkg-config:

```sh
cc myapp.c $(pkg-config --cflags --libs cgra) -o myapp
```

### Optional: Nix as the package manager

A `flake.nix` wraps the same `make install`, so Nix is a drop-in alternative
(not the primary path):

```sh
nix build .            # -> ./result/{bin,lib,include,share}
nix run . -- selftest  # run the CLI without installing
nix profile install .  # install like any package
nix build .#doc        # the IEEE PDF report
nix develop            # dev shell with the whole toolchain
```

## Building, testing, sanitizers

The C build is strict (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow
-Wcast-qual …`) and has four **profiles**, selected with `PROFILE=`:

| PROFILE | Flags | Use |
|---------|-------|-----|
| `release` (default) | `-O3 -DNDEBUG` | builds and `install` |
| `debug` | `-O0 -g3` | stepping in a debugger |
| `asan` | `-fsanitize=address,undefined` | memory/UB bug hunting |
| `ubsan` | `-fsanitize=undefined` | undefined-behaviour only |

```sh
make test                     # unit (assert.h) + smoke, release
make PROFILE=asan test        # everything under ASan + UBSan
make test-install             # isolated install + CLI and external C client
make test-compdb              # compiler database, profiles and quoted paths
make -C lib -f lib.mk valgrind   # unit tests under valgrind
make -C lib -f lib.mk analyze    # gcc -fanalyzer
make -C hw  -f hw.mk  lint       # GHDL RTL syntax/elaboration check
```

Unit tests use `assert.h` with TAP-style output (`ok N …`, a `1..N` plan) and a
non-zero exit on failure; assertions stay live even in release builds. Builds
are parallel-safe (`make -j`).

Release outputs remain in `lib/build/` and `sw/build/`; other profiles use
`build/<profile>/` in each component. Switching to ASan therefore builds
instrumented objects without reusing release objects.

`make sim` also needs a C11 compiler and POSIX pseudo-terminals. It compares
504 byte-level protocol transactions per geometry between `emu.c` and the RTL,
checks 1,024 PE results and seven standalone array shapes through 8x8, and
replays real host kernel traffic on eight device geometries. Full UART replay
covers 2x3 and 3x2; the 4x4 cadence sweep uses `STEP_DIV=1..4`.

### Rectangular meshes

The full stack supports positive **R x C geometries with at most 16 PEs** and a
16-bit datapath. The default is 4x4. Hardware dimensions are fixed at elaboration;
the host discovers them through ID and adapts transfers and kernel tiling.

```sh
./sw/build/cgra probe -d sim:2x3
./sw/build/cgra run add -d sim:2x3 --a "1 2 3 4 5" --b "10 20 30 40 50"
./sw/build/cgra show add -d sim:3x2
make synth MESH_ROWS=2 MESH_COLS=3  # requires Vivado
make bit MESH_ROWS=2 MESH_COLS=3
make prog-ofl MESH_ROWS=2 MESH_COLS=3
```

Nondefault Vivado outputs use `hw/build/vivado/RxC/`. C callers use
`cgra_get_info` and the `_n` transfer functions with explicit buffer counts;
legacy fixed-size primitives require 4x4. All public kernels adapt:
`cgra_vec_*`, `cgra_dot`, `cgra_matvec`, `cgra_scan` and `cgra_conv`.
The last three use `size_t` dimensions and explicit output capacities and
share their implementation with the CLI. Vector/scan operations support
exact in-place output; matrix/convolution output must not overlap inputs.
See `libcgra(3)` for empty inputs and partial results on failure.
`sim:2x3:v2` exercises the older protocol on a rectangle.
See [supported geometries, API contract and verification](doc/mesh-generalization.md).

`clangd` reads a generated compilation database (there is no `compile_flags.txt`):

```sh
make compdb        # -> ./compile_commands.json (git-ignored; or `bear -- make`)
make compdb PROFILE=asan CC=clang
```

The database uses the makefiles' compiler and flags, with the selected profile's
generated header and optional readline settings. It includes host implementation
and C unit-test sources; generation does not compile them. Regenerate when those
settings change. Python 3 is required; paths and flags are serialized as JSON
argument arrays to preserve spaces and quotes.

### Stress benchmark (the FPGA bring-up pipeline)

Once the board is programmed with the synthesized RTL, one command sweeps the
loaded `.cgra` modes and prints a structured report with scalar verification,
throughput and serial link cost per supported mode, plus a JSON artifact:

```sh
make bench DEV=auto                       # discovers + checks the FPGA, then benchmarks
make bench DEV=sim SIZES="1024 4096" REPEAT=100   # dry-run on the emulator
cgra benchall -d auto --size 4096 --json  # the underlying structured command
```

`DEV=auto` first runs `cgra probe` (autodiscover + known-answer correctness), so
the initial checks run before the sweep. Each measured invocation is then
compared with an independent scalar reference outside the timing interval.
JSON includes every mode as `passed`, `failed` or `skipped`. Failures cause a
nonzero exit status and remain in the saved artifact. Custom/stateful-diagonal
mappings without a scalar reference are explicitly skipped after validation;
the built-in 4×4 custom template is invalid on smaller shapes. Supported sizes
are 1..4096; larger requests are rejected. See
[sw/scripts/cgra-bench.sh](sw/scripts/cgra-bench.sh) and `cgra(1)`.

### Profiling the C code

Whole-program profiles of the CLI driving the emulator (CPU-bound, so the
hotspots are the protocol framing, checksums and the PE model):

```sh
make gprof       # -pg build -> sw/build/gprof/gprof.txt
make perf        # perf record/report -> sw/build/perf/perf.txt
make callgrind   # valgrind -> sw/build/perf/callgrind.txt (+ kcachegrind)
make profile     # all three
```

Override the workload with `WORKLOAD='benchall -d sim --size 4096 --repeat 500'`.

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
  benchall [--size N] [--repeat R] [--json]    stress-bench every mode (structured)
  emulate                                      serve a virtual CGRA on a pty
  selftest                                     run kernels on the sim: emulator
  shell                                        interactive prompt (readline)
```

Options are parsed with `getopt_long`, so they may appear anywhere on the line;
bare negative numbers go after `--` (`cgra run relu -- -3 -1 0 5`) or through
`--a`/`--b`/stdin. `cgra shell` is an interactive REPL with line editing and
history (readline when available, else plain `fgets`).

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

**Robustness.** CFG/WR and EXEC with fused reset retry complete NACK replies
(`cgra_set_retries`, default 3), assuming intact command bytes and frame
lengths. RUN/RD are never retried. Timeout, I/O failure or malformed reply
stops the session; subsequent protocol calls return `CGRA_ERR_DESYNC` without
sending bytes. Restore the remote parser before reopening and reinitializing
the device; reopening alone does not reset it. See the recovery contract in
`libcgra(3)`. Protocol v2/v3 cannot reliably detect arbitrary byte loss,
insertion or corrupted commands. `sim:flaky` exercises NACK retries, `sim:v2`
the legacy fallback, and PTY tests verify that uncertain transfers stop without
an implicit reset. `cgra_get_stats` exposes transaction/retry/byte counters.

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
reset   = each            # each chunk; once (default) starts each run from zero
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
  `const`) from the west, off-diagonal PEs are PASS chains, the diagonal
  PEs each compute one element. `min(R,C)` lanes per transaction, `out = diag`.
* `reduce` — MAC/ACC accumulation into PE(0,0); one element per step, scalar
  result (e.g. `dot`).
* `custom` — you place each PE with `pe R,C = op=.. a=.. b=..`; unlisted PEs
  are NOP. Output taps default to the diagonal (`out = diag`) or a single PE.
* `scan` — inclusive prefix fold on row 0: the block's elements arrive on the
  north edge, the running fold flows west→east, and the previous block's result
  is injected as the carry on the west edge; tiled over columns. Because the
  elements ride the edge rather than the PE immediates, the configuration is
  data-independent and loaded once for the whole vector. Driven by `cgra scan`.
* `conv` — 1-D convolution as a Toeplitz matrix–vector, driven by `cgra conv`.
  The matrix is banded, and the engine skips its all-zero R×C tiles.
* `systolic` — weight-stationary matrix–vector `y = A·x`, driven by
  `cgra matvec` (matrix-shaped input, so it is not a plain vector `run`).
  The vector `x` is held stationary in the PE immediates; the matrix streams
  in one row per step as a wavefront that marches south (row 0 multiplies,
  remaining rows form a delay line), and the array is tiled over R×C blocks for any
  `M×N`. PEs are used for products and data movement; the host adds
  the skewed partials per output. See `cgra_matvec` in `lib/src/kernels.c`.

**Applicable fields.** Diagonal modes accept `op/a/b`, `steps`, `out=diag`
and `reset`. Custom modes accept PE lines, `steps`, `out` and `reset`; their
opcodes and operands belong on the PE lines. Reductions accept `op/a/b`,
`steps` and optionally `reset=once`; output is implicitly PE(0,0).
Dedicated `systolic`/`conv`/`scan` entries accept only `doc` and `pattern`:
the dedicated commands choose their parameters from CLI arguments.
Inapplicable fields are rejected, including fields inherited through merging.
Clear ordinary fields with an empty assignment, or use a new mode name to
discard an inherited custom layout.

**Reset.** Each nonempty diagonal/custom run starts with zero PE registers.
The default `reset=once` preserves state between chunks within that run;
`reset=each` clears before each chunk. Reductions reset once per run and reject
`each`. State does not carry between separate mode invocations. These rules
apply on v2 and v3; see `cgra(5)` for empty inputs and step counts too short
to settle all diagonal results.

The MAC instruction supports local output-stationary accumulation, as used by
`cgra dot`. A PE exposes only one result register, so retaining a sum and
forwarding independent activation/weight values in the same step requires
additional routing, phases or storage. The implemented matrix mapping uses
C multipliers in row 0 and (R−1)C delay cells, followed by a host reduction:
four multipliers and twelve delay cells on the default 4×4 mesh.

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
the user config directory and its `stdlib/`, then the configured installed
stdlib and `/etc/cgra`. Installed includes work before `init`. The copy source
for `init` is `CGRA_STDLIB`, then the installed directory, then the relative
source directories `sw/config/stdlib` and `config/stdlib`. An invalid explicit
`CGRA_STDLIB`, missing library or copy/write error returns nonzero; a failure
can leave defaults or some copied files in the user directory.

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
listings. It uses atomic topic files, separate TikZ/caption units and individual table
files; see the [editing map](doc/STRUCTURE.md). For example,
`make -C doc -f doc.mk figure-pe` builds just that figure and its caption.
The PDF opens with a hyperlinked bookmark outline;
`IEEEtran.cls` is vendored so it builds without a full TeX Live.

Install the pages system-wide with, e.g., `install -Dm644 sw/doc/cgra.1
/usr/local/share/man/man1/cgra.1` (and `cgra.5` → `man5/`, `libcgra.3` →
`man3/`).

## UART protocol (v3)

115200 baud, 8N1, little-endian payloads. Single-byte commands, `ACK = 0x79`,
`NACK = 0x1F`. Multi-byte payloads carry an 8-bit additive checksum (sum mod
256); the device NACKs a bad CFG/WR and appends a checksum to the RD reply.
Defined in `hw/rtl/cgra_pkg.vhd`, mirrored in `lib/include/cgra.h` and the
emulator `lib/src/emu.c` — **keep the three in sync**.

| Cmd | Byte | Payload → reply |
|-----|------|-----------------|
| `ID`  | `0x01` | — → `0xCA`, version, ROWS, COLS, DATA_W |
| `CFG` | `0x02` | R*C × 32-bit config words + checksum → ACK / NACK |
| `WR`  | `0x03` | (R+C) × 16-bit (west rows, then north columns) + checksum → ACK / NACK |
| `RUN` | `0x04` | 1 byte step count N (array clocked N cycles) → ACK |
| `RD`  | `0x05` | — → R*C × 16-bit PE registers + checksum |
| `RST` | `0x06` | — (clears PE registers, keeps config) → ACK |
| `EXEC`| `0x07` | steps, flags, 16-bit tap mask, (R+C) × 16-bit inputs + checksum → masked registers + checksum + status |

`EXEC` (**v3**) is the fused transaction: it does the work of `WR` + `RUN` +
`RD` in **one round trip** and returns only the registers named by the tap
mask (bit *i* = PE *i*, row-major), so a 4-lane element-wise chunk ships 8
result bytes instead of 32. Flags bit 0 folds in an `RST`, allowing retry of a
complete, checksum-valid NACK under the fault model above. Reply length follows
the mask received by the controller: it is predictable only if the mask and
command boundaries arrive intact. On checksum failure the device returns the
*current* selected registers and a `NACK` without stepping the array.

The `ID` reply reports the array geometry and the protocol version
(`cgra ping` / `cgra probe`). Geometry and protocol selection adapt at runtime;
`libcgra` uses `EXEC` only
when the device reports v3 and otherwise falls back to the v2 `WR`/`RUN`/`RD`
sequence, which keeps an older bitstream working. Both paths are held to the
same known-answer suite (`cgra selftest` runs it twice, once against `sim:` and
once against the deliberately v2-only `sim:v2`). A watchdog in `cgra_ctrl`
aborts a stalled payload after its configured interval (~1 s by default).
Host recovery also requires pending traffic to finish; neither the watchdog
nor reopening the port alone guarantees synchronization under arbitrary faults.

`CFG` and edge-input words are written as they arrive, before their checksum
is validated. A NACK does not restore the old configuration or inputs; after
a complete NACK, resend a valid payload before executing. An uncertain transfer
requires the session recovery described above. A rejected
`EXEC` does not reset or step the PE registers, but its inputs have been latched.

### Cost of the fused transaction

The current public kernels can be compared on v2 and v3 with fixed inputs:

```sh
make -C doc -f doc.mk measure
```

Each row counts one successful call on a 4×4 emulator after identification:
CFG and computation are included, initial ID is excluded, no retries occur.
The runner verifies independent scalar results before recording counters.

| Workload | Transactions v2 → v3 | TX+RX bytes v2 → v3 |
|---|---:|---:|
| vector ADD, 256 elements | 193 → 65 | 3 651 → 2 115 |
| dot, 256 elements | 515 → 259 | 5 735 → 6 247 |
| scan ADD, 256 elements | 257 → 65 | 3 779 → 2 115 |
| dense matvec, 32×32 | 648 → 264 | 8 472 → 8 728 |
| full conv, k=5, n=64 | 336 → 144 | 5 040 → 5 168 |

The [CSV](doc/data/wire-cost.csv) and [provenance](doc/data/wire-cost.json)
record the workloads and source hashes. The report's table and chart are
both generated from this CSV. The versions share the current mappings,
including scan configuration reuse and zero-tile skipping, so this experiment
isolates the protocol paths without measuring those earlier optimisations.

`EXEC` reduces transactions in every row, but increases bytes for dot,
dense matvec and convolution. Elapsed-time benefit depends on link latency
and throughput; neither these counters nor the emulator's wall clock establish
a hardware speedup over the CPU. Physical link timings and a CPU baseline
remain to be measured. Convolution still constructs a dense Toeplitz matrix
in host memory; skipping its zero tiles saves transfers, not workspace.

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
* Array geometry uses `G_ROWS` / `G_COLS` (default 4×4); the datapath is 16-bit.
  The complete stack supports up to 16 PEs; larger devices need a protocol
  extension. See [the geometry contract](doc/mesh-generalization.md).
* The `sim:` emulator (`lib/src/emu.c`) is the golden reference for the RTL:
  it re-implements the PE ALU and the protocol FSM, so `make test` validates
  the host stack against the same semantics the hardware must have.
* Timing uses one clock domain, with UART/button exceptions in
  `hw/scr/constraints.tcl`. The controller spaces PE captures by `G_STEP_DIV`
  clocks (default 2), including a guard after reset/configuration changes.
  Matching setup/hold multicycle constraints express that schedule to Vivado.
  GHDL verifies enable spacing at factors 1–4. The actual Vivado 2026.1 run
  below separately checks one implemented design.
* `make fmax [STEP_DIV=N] [FLOORPLAN=1]` searches candidate constraint periods,
  reruns the selected candidate with the final implementation recipe, and
  requires finite, nonnegative setup and hold slack before reporting `1000/T`.
  This is a passing candidate, not a proof of a global optimum. Changing the
  constraint does not change the physical board clock or UART divisors.
  Historical frequencies in `hw/perf/PERFLOG` lack archived raw reports and
  predate current timing fixes; they are not validated results for this tree.
* The [Vivado 2026.1 CLI run](doc/verification/2026-09-23/vivado/README.md)
  passes synthesis and routed STA on Nexys A7, 4×4, `STEP_DIV=2`, 10 ns:
  setup WNS **+1.635 ns**, hold WHS **+0.138 ns**, zero unconstrained internal
  endpoints. The separate unflattened synthesis reports 5 016 LUTs, 1 034
  registers and 16 DSP48E1s. These are tool results for the archived sources,
  not a board measurement, a Fmax sweep or a comparison against the old RTL.
  Physical board tests and isolation of optimisation benefits remain pending.
