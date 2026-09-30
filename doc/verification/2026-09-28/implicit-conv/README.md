# Implicit convolution tiles (A09)

The before/after experiment is stored in sibling `geometry-before/` and
`geometry-after/` directories, with **50 independently measured points each**.
Every JSON point records source/library/binary hashes, compiler and flags,
protocol, geometry, counters and samples. The starting commit is `dc886d2`;
both experiments used the explicitly recorded dirty source snapshot.

`python3 scripts/measure-geometry.py NEW_DIRECTORY` reproduces the current
experiment. `--baseline` additionally checks the review's 15 v3 byte counts;
all were reproduced before the optimization. One warm-up and five measured
calls per point use the public API; all results are checked against scalar
references outside the timed region. These are host/emulator measurements,
not FPGA latency or CPU speedup comparisons. The separate CLI benchmark
consolidation (A10/A11) remains open.

## Results

For API convolution `n=4096, k=5`, requested temporary project heap storage
fell from **33,587,200 bytes to zero**. The implementation uses a bounded tile
on the stack (at most 16 words), alongside the existing configuration and
edge buffers. Allocator wrappers measure requested/live heap bytes, excluding
caller arrays, libc internals and stack. `ru_maxrss` in the raw points is the
whole process high-water mark, including warm-up and possible launch overhead;
it is **not** the resident size of the Toeplitz allocation.

V3 medians in milliseconds (public API call, including preparation,
configuration, transport and clockless model):

| Geometry | Before | After | Logical steps | Transactions | TX+RX bytes |
|---|---:|---:|---:|---:|---:|
| 1x16 | 78.363 | 1.844 | 5120 | 5376 | 396032 |
| 2x8 | 91.830 | 1.690 | 6144 | 6656 | 304640 |
| 4x4 | 104.037 | 2.156 | 8192 | 9216 | 330752 |
| 8x2 | 142.477 | 5.210 | 24576 | 26624 | 923648 |
| 16x1 | 201.637 | 16.275 | 81920 | 86016 | 3878912 |

**All 50 points preserve byte, transaction and step counts exactly.** The
change saves host allocation/fill/scanning work; it does not improve the
serial-link traffic. Raw points include p95 and process CPU time as well.

For tile dimensions R by C, the generator visits at most
`T = ceil(n/C) * (ceil((k+C-1)/R) + 1)` candidate tiles. Each costs O(RC);
zero column chunks and all-zero tiles are skipped. Including input scans,
output initialization and per-column mapping preparation, host work is
O(n + (n+k-1) + ceil(n/C) RC + T RC), workspace O(RC+R+C). It never walks the
full virtual quadratic matrix. The legacy virtual-matrix size bound remains
an API compatibility limit; the CLI output limit is a separate contract.

## Regression gates

- `make -j4 test`: **1240 checks**, exit 0 (`release.txt`).
- `make -j4 PROFILE=asan test`: **1240 checks**, exit 0, LSan enabled (`asan.txt`).
- `make sim`: full current GHDL suite, exit 0 (`ghdl.txt`).
- Part preview and `make docs`: exit 0; full log has no undefined references
  or overfull boxes. Page 5 was rendered with `pdftoppm` and visually inspected.

The added kernel cases exercise v2/v3 on eight geometries, long kernels,
partial tiles, zero/sparse weights and 16-bit wraparound against an independent
unsigned scalar reference. Allocation-failure tests now confirm convolution
needs no heap after opening the device. Existing aliasing, capacity, empty
input and partial-output failure tests remain active. Check counts are not
coverage percentages. No new physical hardware result is claimed.
