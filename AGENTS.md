# Working in ald-cgra

## Map

- `hw/rtl/`: production VHDL; `hw/sim/`: GHDL testbenches and C vector generators.
- `hw/formal/`: production RTL properties, adapters and executable proof runner.
- `hw/scr/`, `hw/con/`: Vivado batch flow and physical constraints.
- `lib/include/cgra.h`: public C ABI and contracts; `lib/src/`: transport, model,
  kernels and PTY service; `lib/test/`: C and transport regressions.
- `sw/cli.c`: command validation/dispatch; `vector_io.c`, `bench.c`, `device.c`,
  `shell.c`: separate CLI responsibilities; `cli.h`: private invocation context.
- `sw/dsl.c`, `sw/compile.c`: configuration parsing and geometry-aware mapping.
- `doc/`: report, design notes and evidence. Read `doc/AGENTS.md` for PDF work.
- `TASKS.md`: the single task tracker and resumable operational checkpoint.

## Commands (repository root)

```
make -j4
make -j4 test
make -j4 PROFILE=asan test
make test-install test-compdb test-build
make -C lib -f lib.mk analyze
make -C sw -f sw.mk analyze
make sim
make -C hw -f hw.mk test-scripts
nix develop .#formal -c make formal-check-deps formal-fast
make docs
```

Make fragments also work standalone with `make -C DIR -f DIR.mk TARGET`.
Use GCC for `-fanalyzer`; a compiler that lacks it is not a successful check.
Formal targets fail if tools or required proofs are missing. Every run keeps
its own manifest/logs; `full` and `cover` have the scope listed in the formal
contract. Vivado results require actual Vivado execution, separately from Tcl
mock tests. Do not infer FPGA throughput from the clockless C model.

## Contracts across layers

- Keep VHDL and C wire constants, row-major geometry and 16-bit modular
  arithmetic aligned. Legacy v2/v3 support at most 16 PEs.
- Legacy CFG/WR may update registers before checksum rejection; NACK is not
  rollback. An uncertain transfer poisons the session; retry must not duplicate
  effects. The C response timeout is distinct from a whole-transaction deadline.
- Preserve public ABI, capacities, ownership, aliasing and partial-result
  behavior. CLI argument errors must occur before external side effects.
- Compiler/options/generated paths participate in build signatures. Avoid
  manual-clean requirements or compilation databases with invented flags.

## Evidence and checkpoints

Record actual commands, exit status, tool versions, commit/dirty state and
source hashes in a new `doc/verification/YYYY-MM-DD/TOPIC/` directory. Keep
historical evidence unchanged. Counts are checks, not coverage percentages.
Record missing execution as NOT_RUN; distinguish bounded checks, induction,
counterexamples, solver timeouts and frontend errors. A reached cover is only
reachability. Never silently weaken input domains or turn assertions into
assumptions. Preserve failure logs and useful counterexamples.

Update existing TASKS IDs, add IDs only for new scope, and leave an exact
checkpoint after substantial work. Respect the current working tree and user
authorization; inspect it before changes. Do not infer permission for push,
release or programming hardware from a request to build or verify code.
